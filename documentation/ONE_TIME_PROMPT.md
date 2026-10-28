# Eyum Plant Care - Master 1-Time Genesis Prompt

> **Purpose:** This file contains the complete, battle-tested "1-Time Prompt" that can be used with any advanced AI coding agent (e.g. Antigravity, Claude, ChatGPT) to reproduce, rebuild, or extend the entire **Eyum Plant Care** project from scratch in a single shot.

---

```markdown
You are an expert embedded systems and full-stack IoT engineer. Build a complete, production-grade, local-first plant watering and soil moisture automation system for an ESP32 using PlatformIO and Arduino framework, with an embedded web dashboard served from LittleFS.

### 1. Architectural & Technology Requirements
- **Framework**: PlatformIO + Arduino C++ on `esp32dev` board.
- **Filesystem**: LittleFS for serving web assets (`board_build.filesystem = littlefs`).
- **Web Stack**: Vanilla HTML5, Vanilla CSS3, Vanilla modern ES6+ JavaScript (no heavy frameworks, zero external CDN dependencies so it functions 100% offline).
- **Network Resolution**: Implement ESP-mDNS so users can access `http://eyum-plant.local` without hunting for DHCP IP addresses.
- **CORS Support**: All REST API endpoints must output `Access-Control-Allow-Origin: *` headers and handle HTTP `OPTIONS` preflight requests to support external companion apps.

---

### 2. Hardware Connections & Pinouts
- **Capacitive Soil Moisture Sensor (4-pin)**:
  - `VCC` -> `3V3`
  - `GND` -> `GND`
  - `AO` (Analog Out) -> `GPIO 35` (ADC1 channel, 12-bit resolution)
  - `DO` (Digital Out) -> `GPIO 32` (optional digital threshold reading)
- **Actuator / Pump Driver**:
  - MOSFET Gate / Relay `IN` -> `GPIO 26` (Active-HIGH: 3.3V = ON, 0V = OFF)
  - Common Ground between ESP32 and pump power supply.
- **Optional Water Reservoir Sensor**:
  - Float switch -> `GPIO 34` (Active-LOW, pullup: LOW = Water OK, HIGH = Reservoir Empty). Default pin constant configurable to -1 if uninstalled.

---

### 3. Firmware Core Subsystems & Safety Constraints
1. **Moisture Sensor (`MoistureSensor.h/.cpp`)**:
   - Sample ADC 7 times with 2ms intervals, sort readings, and extract the median to eliminate electrical noise.
   - Percentage formula: `Moisture% = 100.0 * (dryRaw - raw) / (dryRaw - wetRaw)`, constrained between 0% and 100%.
   - In dry air, ADC reads ~4095; do NOT treat this as a fault! Report 0% moisture with valid status. Only flag disconnection if `raw < 5`.
2. **Watering Controller (`WateringController.h/.cpp`)**:
   - Autonomous triggering: If `autoMode` is enabled, soil is valid, reservoir is OK, and moisture < threshold.
   - Per-cycle runtime limit: Defaults to 3.0s (configurable 0.25s - 10s).
   - Anti-hammering cooldown: Mandatory soak delay (default 5 minutes) after watering to prevent re-triggering before water reaches roots.
   - 24-Hour Rolling Pump Allowance: Cumulative pump runtime accumulator. If pump runtime exceeds 30 seconds in 24 hours, halt watering with `DailyLimit` reason to prevent catastrophic line-break flooding.
   - Independent Manual Override: A manual "Water Now" command must ALWAYS be permitted if pump is idle (never blocked by cooldown or uncalibrated probe).
3. **Persistent Settings (`Settings.h/.cpp`)**:
   - Store calibration (`dryRaw`, `wetRaw`), `threshold`, `pumpRuntimeMs`, `cooldownMs`, `maxDailyPumpMs`, `autoMode`, and Wi-Fi credentials in ESP32 NVS via `Preferences.h`.
4. **Provisioning & Access Point (`main.cpp`)**:
   - If unconfigured, launch captive SoftAP `Eyum-Setup-XXXX` on `192.168.4.1` serving `setup.html`.
   - Once provisioned, connect to home Wi-Fi in Station mode and start dashboard on Port 80 with HTTP Basic Auth.

---

### 4. REST API Contract
- `GET /api/status`: Returns JSON `{valid, raw, moisture, fault, digitalTrigger, pump, remainingMs, auto, lastStop, reservoirOk, uptimeSec, rssi, ip, mdns}`.
- `GET /api/settings`: Returns JSON with threshold, dryRaw, wetRaw, pumpRuntimeMs, cooldownMs, maxDailyPumpMs, dailyUsedMs.
- `POST /api/settings`: Updates thresholds and timing values.
- `POST /api/water`: Triggers immediate single-cycle watering.
- `POST /api/stop`: Immediate pump emergency halt.
- `POST /api/auto`: Body `{"enabled": boolean}`.
- `POST /api/calibration`: Body `{"dryRaw": int, "wetRaw": int}`.

---

### 5. UI/UX Design System Requirements (CRITICAL)
- **Light Theme Only**: Bright, clean, warm botanical aesthetic. Background in warm linen/porcelain `#F7F9F6`, pristine white cards `#FFFFFF`.
- **STRICT PROHIBITION**:
  - **NO blue colors** (no royal blue, sky blue, cyan, indigo).
  - **NO neon colors** (no radioactive greens, no fluorescent lime).
- **Palette**:
  - Primary: Deep forest evergreen `#2D6A4F` (hover: `#1F4D38`)
  - Leaf Accent: Natural sage green `#40916C` and `#74C69D`
  - Subtle Tint: Soft dewdrop pastel green `#E8F4EC`
  - Warning/Dry: Warm terracotta clay `#B85D3B` (subtle: `#FDF4F0`)
  - Danger/Stop: Deep brick rust `#8B3A2B`
  - Text: Deep organic evergreen ink `#172C21`, muted slate `#4D6154`
- **Centerpiece UI Elements**:
  - **Circular SVG Moisture Gauge**: Smooth animated leafy stroke-dashoffset transition, displaying large percentage and status badge ("Optimal", "Soil Dry", "Moist Soil").
  - **Pump & Reservoir Status**: Visual pulse animation when watering, countdown seconds remaining, and daily water allowance progress bar.
  - **Direct Controls**: Auto-mode toggle pill, "Water Now" button, and "Emergency Stop" button. "Water Now" must always be clickable whenever pump is idle.
  - **Quick Calibration Section**: Displays live probe ADC reading with quick "Use Current ADC" buttons for Dry and Wet values.
  - **Diagnostics Footer**: Local IP, live system uptime, and `eyum-plant.local` host tag.

---

### 6. Expected File Structure
```
plant-watering automation/
├── platformio.ini
├── README.md
├── src/
│   ├── main.cpp
│   ├── MoistureSensor.h
│   ├── MoistureSensor.cpp
│   ├── Settings.h
│   ├── Settings.cpp
│   ├── WateringController.h
│   └── WateringController.cpp
├── data/
│   ├── index.html
│   ├── style.css
│   ├── app.js
│   ├── setup.html
│   └── setup.js
└── documentation/
    ├── SYSTEM_ARCHITECTURE.md
    └── ONE_TIME_PROMPT.md
```
```
