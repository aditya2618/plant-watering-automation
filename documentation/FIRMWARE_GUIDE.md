# Eyum Plant Care — Firmware Developer Guide

> In-depth documentation of every C++ module in `src/`. Use this to understand, extend, or debug the firmware.

---

## Source File Map

```
src/
├── main.cpp              — Application entry point, HTTP routes, main loop
├── MoistureSensor.h/.cpp — ADC reading, median filter, calibration mapping
├── WateringController.h/.cpp — Pump GPIO control, safety timers, daily limit
├── Settings.h/.cpp       — NVS persistence, validation, provisioning
└── CloudClient.h/.cpp    — WebSocket cloud bridge (optional)
```

---

## 1. `main.cpp` — Application Entry Point

### Constants
```cpp
constexpr uint8_t SOIL_PIN      = 35;   // Analog moisture sensor (AO)
constexpr int8_t  SOIL_DO_PIN   = 32;   // Digital threshold from sensor (DO)
constexpr uint8_t PUMP_PIN      = 26;   // MOSFET gate / relay IN
constexpr int8_t  RESERVOIR_PIN = -1;   // Float switch (-1 = not used)
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000; // Sensor read every 2s
constexpr char DASHBOARD_USER[] = "admin";    // HTTP Basic Auth username
```

### Global Objects
| Object | Type | Purpose |
|---|---|---|
| `server` | `WebServer` | ESP32 Arduino HTTP server on port 80 |
| `settingsStore` | `SettingsStore` | NVS settings manager |
| `sensor` | `MoistureSensor` | Reads GPIO 35 / 32 |
| `watering` | `WateringController` | Controls GPIO 26 |
| `cloudClient` | `CloudClient` | WebSocket cloud bridge |
| `reading` | `SensorReading` | Latest sensor snapshot |
| `lastSampleAt` | `uint32_t` | Timestamp of last sensor read |
| `lastTelemetryAt` | `uint32_t` | Timestamp of last cloud telemetry push |
| `setupMode` | `bool` | `true` = SoftAP mode, `false` = STA mode |

### Startup Flow (`setup()`)
```
1. watering.begin()        → GPIO 26 OUTPUT LOW
2. Serial.begin(115200)
3. settingsStore.begin()   → Load NVS into memory
4. sensor.begin()          → Set ADC resolution 12-bit, 11dB attenuation
5. LittleFS.begin(true)    → Mount flash filesystem (format if needed)
6. Register cloud command handler (lambda in setup)
7. if provisioned → connectProvisionedNetwork()
   else          → startSetupAccessPoint()
8. setupRoutes()           → Register all HTTP handlers
9. server.begin()
10. Take first sensor reading
```

### Main Loop (`loop()`)
Every iteration (non-blocking):
1. `server.handleClient()` — process pending HTTP requests
2. `cloudClient.loop()` — advance WebSocket state machine
3. If `SAMPLE_INTERVAL_MS` elapsed → re-read sensor
4. If cloud connected and `telemetryIntervalMs` elapsed → push telemetry
5. `watering.update()` — check pump runtime expiry, sensor fault
6. If auto mode + dry + reservoir OK + not running → request water

### Wi-Fi Logic
- **SoftAP mode:** `WiFi.mode(WIFI_AP)` → SSID `Eyum-Setup-XXXX` / PW `12345678`
- **STA mode:** `WiFi.mode(WIFI_STA)` → `WiFi.begin(ssid, pass)` → 15s timeout
- On STA connected: mDNS `eyum-plant.local`, then `cloudClient.begin()` if enabled

### HTTP Routes
| Route | Auth | Description |
|---|---|---|
| `GET /` | ✅ (or setup mode) | Serves `index.html` or `setup.html` from LittleFS |
| `GET /style.css` | ❌ | LittleFS asset |
| `GET /app.js` | ✅ | LittleFS JavaScript |
| `POST /api/provision` | ❌ (setup) | Wi-Fi + dashboard password save → restart |
| `GET /api/status` | ✅ | JSON telemetry snapshot |
| `GET /api/settings` | ✅ | Read PlantSettings |
| `POST /api/settings` | ✅ | Update thresholds and timers |
| `POST /api/water` | ✅ | Manual pump trigger |
| `POST /api/stop` | ✅ | Immediate pump stop |
| `POST /api/auto` | ✅ | Enable/disable auto mode |
| `POST /api/calibration` | ✅ | Save dryRaw / wetRaw |
| `GET /api/cloud` | ✅ | Read cloud config (secret hidden) |
| `POST /api/cloud` | ✅ | Update cloud config → reconnect |

### Cloud Command Handler (registered in `setup()`)
```cpp
cloudClient.onCommand([](const String& cmdId, const String& action, JsonObjectConst params) {
    if (action == "water")         → requestWater() + sendAck
    if (action == "stop")          → stop() + sendAck
    if (action == "auto")          → toggle autoMode via settingsStore + sendAck
    if (action == "set_threshold") → update threshold (5–80) + sendAck
    else                           → sendAck("failed", "Unknown action")
});
```

---

## 2. `MoistureSensor.h/.cpp` — Soil Sensor Module

### Struct `SensorReading`
```cpp
struct SensorReading {
    int raw = 0;              // Raw ADC value (0–4095)
    int moisture = 0;         // Calibrated moisture percentage (0–100%)
    bool valid = false;       // False if sensor disconnected or uncalibrated
    bool digitalTrigger = false; // Digital pin state (if wired)
    String fault = "";        // Human-readable error description
};
```

### Class `MoistureSensor`
```cpp
MoistureSensor(uint8_t analogPin, int8_t digitalPin = -1);
void begin();
SensorReading sample(int dryRaw, int wetRaw);
```

#### `begin()`
```cpp
analogReadResolution(12);          // 12-bit ADC (0–4095)
analogSetAttenuation(ADC_11db);    // 0–3.3V input range (full scale)
// Note: ADC_11db replaces per-pin analogSetPinAttenuation to avoid
// ESP32 Arduino Core 3.x periman bus check error on boot.
```

#### `sample(dryRaw, wetRaw)`
1. Take **7 successive readings** with 2ms delay between each.
2. **Sort** the 7 values (removes spike noise and Wi-Fi interference bursts).
3. Pick the **median** (index 3) — robust against outliers.
4. **Fault detection:** raw < 5 → probe disconnected.
5. **Calibration check:** dryRaw == wetRaw → needs calibration.
6. **Percentage formula:**
   ```
   percent = 100 × (dryRaw - raw) / (dryRaw - wetRaw)
   moisture = constrain(round(percent), 0, 100)
   ```
   Higher raw = drier (less conductivity), lower raw = wetter.

#### Why Median Filter?
- Eliminates single-point spikes (Wi-Fi TX bursts, motor switching noise).
- More reliable than averaging because it is unaffected by extreme outliers.

---

## 3. `WateringController.h/.cpp` — Pump Control Module

### Enum `PumpStopReason`
```cpp
enum class PumpStopReason {
    None,        // Not stopped yet
    Completed,   // Normal timer expiry
    Manual,      // User pressed Stop
    Safety,      // Sensor became invalid mid-cycle
    DailyLimit   // Would exceed daily quota
};
```

### Class `WateringController`

#### `begin()`
Sets `PUMP_PIN` as `OUTPUT`, drives it `LOW`, records `dayStartedAt_`.

#### `canWater(settings, sensorValid, error, isManual)`
Returns `true` only if ALL of these pass:

| Check | Applies To | Description |
|---|---|---|
| Not already running | Always | Prevents double-start |
| Daily limit | Always | `dailyUsedMs_ + pumpRuntimeMs <= maxDailyPumpMs` |
| Sensor valid | Auto only | Blocked if probe disconnected |
| Cooldown elapsed | Auto only | `(now - stoppedAt) >= cooldownMs` |

Manual calls bypass sensor + cooldown checks — operator always has control.

#### `requestWater(settings, sensorValid, error, isManual)`
1. `resetDailyLimitIfNeeded()` — resets accumulator after 24h.
2. `canWater()` check.
3. `digitalWrite(pin_, HIGH)` → pump ON.
4. Records `startedAt_`.

#### `update(settings, sensorValid)` — called every loop()
- If running and sensor becomes invalid → `stop(Safety)`.
- If running and `millis() - startedAt_ >= pumpRuntimeMs` → `stop(Completed)`.

#### `stop(reason)`
1. Accumulates `dailyUsedMs_ += (now - startedAt_)`.
2. Records `stoppedAt_` (used for cooldown).
3. `digitalWrite(pin_, LOW)` → pump OFF.

#### `resetDailyLimitIfNeeded()`
If `millis() - dayStartedAt_ >= 86400000` (24h), resets `dailyUsedMs_ = 0` and starts new day.

---

## 4. `Settings.h/.cpp` — NVS Persistence Module

### Structs

#### `PlantSettings`
```cpp
struct PlantSettings {
    int dryRaw = 3000;           // ADC reading = bone dry
    int wetRaw = 1500;           // ADC reading = fully saturated
    uint8_t threshold = 30;      // Auto-trigger below this % (5–80)
    uint32_t pumpRuntimeMs = 3000;     // One pump cycle duration (250–5000ms)
    uint32_t cooldownMs = 300000;      // Rest after watering (30s – 24h)
    uint32_t soakMs = 30000;           // Soak absorption delay
    uint32_t maxDailyPumpMs = 30000;   // 24h pump quota (≥ one cycle, ≤ 5min)
    bool autoMode = false;       // Autonomous irrigation enabled
};
```

#### `NetworkSettings`
```cpp
struct NetworkSettings {
    String wifiSsid;            // Home Wi-Fi SSID
    String wifiPassword;        // Home Wi-Fi password
    String dashboardPassword;   // HTTP Basic Auth password (10–63 chars)
};
```

#### `CloudConfig` (in `CloudClient.h`)
```cpp
struct CloudConfig {
    bool enabled = false;
    String host = "";           // IP or hostname of cloud server
    uint16_t port = 80;         // 80 = WS, 443 = WSS
    bool useSsl = false;        // Use TLS (WSS)
    String deviceId = "";       // Registered device ID
    String deviceSecret = "";   // Auth token from provisioning
    uint32_t telemetryIntervalMs = 30000; // How often to push telemetry
};
```

### Class `SettingsStore`

#### `begin()`
Opens NVS namespace `"eyum"` read-only. Loads all keys. If any setting fails `valid()`, resets to defaults and saves.

#### `update(settings, error)`
Validates, then calls `save()`. Returns false + populates `error` on any constraint violation.

#### `updateCloud(config, error)`
Validates that host, deviceId, and deviceSecret are all non-empty when `enabled = true`.

#### `provision(network, error)`
Validates:
- SSID: 1–32 chars, no quotes/backslashes
- Wi-Fi password: ≤ 63 chars
- Dashboard password: **10–63 chars** (minimum 10, enforced)

#### `isProvisioned()`
Returns `true` only if both SSID is non-empty AND dashboard password ≥ 10 chars.

#### `valid()` — Settings Constraints
| Setting | Min | Max |
|---|---|---|
| `dryRaw` / `wetRaw` | 0 | 4095, must differ |
| `threshold` | 5% | 80% |
| `pumpRuntimeMs` | 250ms | 5000ms |
| `cooldownMs` | 30,000ms | 86,400,000ms (24h) |
| `soakMs` | — | ≤ cooldownMs |
| `maxDailyPumpMs` | ≥ pumpRuntimeMs | 300,000ms (5min) |

#### NVS Key Map (namespace: `"eyum"`)
| Key | Type | Setting |
|---|---|---|
| `dry` | int32 | dryRaw |
| `wet` | int32 | wetRaw |
| `threshold` | uint8 | threshold |
| `runtime` | uint32 | pumpRuntimeMs |
| `cooldown` | uint32 | cooldownMs |
| `soak` | uint32 | soakMs |
| `daily` | uint32 | maxDailyPumpMs |
| `auto` | bool | autoMode |
| `ssid` | string | wifiSsid |
| `wifiPass` | string | wifiPassword |
| `dashPass` | string | dashboardPassword |
| `cloudEn` | bool | cloud.enabled |
| `cloudHost` | string | cloud.host |
| `cloudPort` | uint16 | cloud.port |
| `cloudSsl` | bool | cloud.useSsl |
| `cloudDevId` | string | cloud.deviceId |
| `cloudSec` | string | cloud.deviceSecret |
| `cloudInt` | uint32 | cloud.telemetryIntervalMs |
| `initialized` | bool | provisioning complete flag |

---

## 5. `CloudClient.h/.cpp` — Cloud WebSocket Module

### Class `CloudClient`

#### `begin(config)`
If cloud is enabled and credentials are present, calls `connect()`.

#### `connect()`
Builds URL:
```
/ws/iot/?device_id=<deviceId>&secret=<deviceSecret>
```
Uses `WebSocketsClient`:
- Plain WS: `wsClient_.begin(host, port, url)`
- SSL/TLS: `wsClient_.beginSSL(host, port, url)`
- Auto-reconnect interval: 5 seconds

#### `loop()`
- Calls `wsClient_.loop()` — must be called every iteration of `loop()`.
- Sends heartbeat `ping` every 30 seconds while connected.
- Skips if Wi-Fi is disconnected.

#### `publishTelemetry(doc)`
Wraps payload in:
```json
{"type": "telemetry", "payload": {...}}
```

#### `sendAck(commandId, status, message)`
Sends:
```json
{"type": "ack", "command_id": "...", "status": "acknowledged", "payload": {"message": "..."}}
```

#### WebSocket Event Handler
| Event | Action |
|---|---|
| `WStype_CONNECTED` | Sets `connected_ = true`, logs URL |
| `WStype_DISCONNECTED` | Sets `connected_ = false` |
| `WStype_TEXT` | Parses JSON, routes by `type` field |
| `WStype_ERROR` | Logs error |

#### Inbound Message Types
| `type` | Handling |
|---|---|
| `"connected"` | Log server ACK |
| `"pong"` | Ping acknowledged (no-op) |
| `"command"` | Extract `command_id`, `action`, `params` → call `commandHandler_` |

#### `updateConfig(config)`
Disconnects current session, applies new config, reconnects if enabled. Used by `POST /api/cloud`.

---

## 6. Web Dashboard (LittleFS)

Files stored in `data/` folder, compiled into `littlefs.bin`:

| File | Purpose |
|---|---|
| `index.html` | Main operator dashboard (served after provisioning) |
| `app.js` | Dashboard JavaScript — polls `/api/status`, handles all card actions |
| `style.css` | Shared CSS for both dashboard and setup page |
| `setup.html` | First-run Wi-Fi provisioning form |
| `setup.js` | Setup form submission JavaScript |

### Dashboard Cards
1. **Soil Moisture** — Radial gauge, moisture %, raw ADC, fault state
2. **Watering Status** — Pump running indicator, remaining time, last stop reason
3. **Auto Mode** — Toggle switch, threshold display
4. **Settings** — Pump runtime, cooldown, daily limit sliders
5. **Calibration** — Dry/wet ADC capture, live percentage preview
6. **Eyum Cloud Settings** — Host, port, device ID, secret, enable toggle, connection status

### Build Commands
```powershell
# Build LittleFS image from data/ folder
pio run -t buildfs

# Upload LittleFS image to ESP32
pio run -t uploadfs
```

---

## 7. Build Configuration (`platformio.ini`)

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs
board_build.partitions = min_spiffs.csv   ; 4MB flash: 1.9MB app + 190KB SPIFFS
lib_deps =
    bblanchon/ArduinoJson@^7.0.4
    Links2004/WebSockets@^2.4.1
```

### Flash Partition (`min_spiffs.csv`)
```
nvs,      data, nvs,      0x9000,  0x5000,
otadata,  data, ota,      0xe000,  0x2000,
app0,     app,  ota_0,    0x10000, 0x1E0000,
spiffs,   data, spiffs,   0x1F0000,0x10000,
```
This gives ~1.9MB for application code (was 1MB default) and ~190KB for LittleFS.
Flash usage: **65.9%** (was 98.8% on default partitions).

---

## 8. Known Issues & Fixes

| Version | Issue | Fix |
|---|---|---|
| v1.3.0 | `analogSetPinAttenuation()` causes ADC error on core 3.x | Replaced with global `analogSetAttenuation(ADC_11db)` in `MoistureSensor::begin()` |
| v1.3.0 | Default partition too small for WebSockets + ArduinoJson | Added `board_build.partitions = min_spiffs.csv` |
| v1.2.x | No cloud integration | Added `CloudClient`, `CloudConfig`, `/api/cloud` endpoints, Cloud Settings UI card |

---

## 9. Extending the Firmware

### Adding a New Cloud Command
In `main.cpp` inside `cloudClient.onCommand()`:
```cpp
} else if (action == "set_runtime") {
    int ms = params["ms"] | -1;
    if (ms >= 250 && ms <= 5000) {
        PlantSettings updated = settingsStore.get();
        updated.pumpRuntimeMs = ms;
        String err;
        if (settingsStore.update(updated, err)) {
            cloudClient.sendAck(cmdId, "acknowledged", "Runtime updated");
        } else {
            cloudClient.sendAck(cmdId, "failed", err);
        }
    } else {
        cloudClient.sendAck(cmdId, "failed", "Runtime must be 250-5000ms");
    }
}
```

### Adding a New REST Endpoint
```cpp
server.on("/api/reset", HTTP_POST, [] {
    if (!requireAuth()) return;
    String error;
    if (!settingsStore.reset(error)) { sendError(400, error); return; }
    sendJson(200, "{\"ok\":true}");
});
```
Register it in `setupRoutes()` before `server.begin()`.

### Adding a New NVS Key
1. Add field to `PlantSettings` or `NetworkSettings` struct in `Settings.h`.
2. Add `preferences.getXxx("key", default)` in `SettingsStore::begin()`.
3. Add `preferences.putXxx("key", value)` in `SettingsStore::save()`.
4. Add validation rule in `SettingsStore::valid()`.
