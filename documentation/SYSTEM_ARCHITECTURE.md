# Eyum Plant Care - System Architecture & Engineering Documentation

## 1. System Overview

**Eyum Plant Care** is an autonomous, local-first IoT irrigation and soil monitoring system powered by an **ESP32 microcontroller**. It operates completely independently of external cloud services or internet dependencies, executing sensor reading, flood prevention safety algorithms, and local web serving directly on the chip.

```
+-----------------------------------------------------------------------------------+
|                              LOCAL BROWSER / CLIENT                               |
|                     (Light Botanical UI · SVG Moisture Gauge)                     |
+-----------------------------------------+-----------------------------------------+
                                          | HTTP / JSON REST & WebSockets (Port 80)
                                          v
+-----------------------------------------------------------------------------------+
|                                ESP32 CONTROLLER                                   |
|                                                                                   |
|  +--------------------+  +--------------------+  +-----------------------------+  |
|  |     WebServer      |  |   LittleFS Flash   |  |        ESP-mDNS             |  |
|  |  (REST API + CORS) |  | (HTML/CSS/JS Assets)|  |   (eyum-plant.local)        |  |
|  +---------+----------+  +---------+----------+  +--------------+--------------+  |
|            |                       |                            |                 |
|            +-----------------------+----------------------------+                 |
|                                    |                                              |
|  +---------------------------------+-------------------------------------------+  |
|  |                       Core Application Engine                               |  |
|  |  +-------------------+  +--------------------+  +------------------------+  |  |
|  |  |  MoistureSensor   |  | WateringController |  |     SettingsStore      |  |  |
|  |  | (Median Filtered) |  | (Safety & Limits)  |  |  (ESP32 NVS Storage)   |  |  |
|  |  +---------+---------+  +---------+----------+  +------------+-----------+  |  |
|  +------------|----------------------|--------------------------|--------------+  |
+---------------|----------------------|--------------------------|-----------------+
                |                      |                          |
                v                      v                          v
       Capacitive Probe           Pump MOSFET / Relay        Flash EEPROM (NVS)
        (GPIO 35 / 32)                (GPIO 26)             (Wi-Fi & Calibration)
```

---

## 2. Hardware Wiring Schematic

```mermaid
flowchart LR
    subgraph Power["Power Supply"]
        USB["USB 5V / VIN"]
        ExtPwr["External 5V / 12V Pump Adapter"]
    end

    subgraph ESP32["ESP32 Dev Module"]
        P3V3["3V3 Pin"]
        PGND["GND Pin"]
        GPIO35["GPIO 35 (ADC1)"]
        GPIO32["GPIO 32 (Digital In)"]
        GPIO26["GPIO 26 (Digital Out)"]
        GPIO34["GPIO 34 (Optional Float Switch)"]
    end

    subgraph Sensor["4-Pin Capacitive Moisture Sensor"]
        SVCC["VCC"]
        SGND["GND"]
        SAO["AO (Analog Output)"]
        SDO["DO (Digital Output)"]
    end

    subgraph Actuator["Relay / MOSFET Pump Driver"]
        RIN["IN / Gate"]
        RVCC["VCC"]
        RGND["GND"]
        RCOM["COM Terminal"]
        RNO["NO Terminal"]
    end

    subgraph PumpUnit["Submersible Water Pump"]
        PPOS["Pump (+) Lead"]
        PNEG["Pump (-) Lead"]
    end

    P3V3 --> SVCC
    PGND --> SGND
    SAO --> GPIO35
    SDO --> GPIO32

    USB --> RVCC
    PGND --> RGND
    GPIO26 --> RIN

    ExtPwr --> RCOM
    RNO --> PPOS
    PNEG --> ExtPwr
```

### Complete Pin Assignment Table

| Function | Pin Name | Physical Pin | Electrical Spec | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Soil Moisture Analog** | `SOIL_PIN` | **GPIO 35** | Input only, ADC1, 0–3.3V | 12-bit ADC reading with 7-sample median filter. |
| **Soil Moisture Digital** | `SOIL_DO_PIN` | **GPIO 32** | Input, 3.3V logic | Optional digital threshold trigger from sensor potentiometer. |
| **Pump Output** | `PUMP_PIN` | **GPIO 26** | Output, 3.3V logic | Active-HIGH signal to relay or N-channel MOSFET gate. |
| **Reservoir Float Switch** | `RESERVOIR_PIN` | **GPIO 34** | Input, pulled high | Active-LOW: LOW = Water OK, HIGH = Reservoir Empty. |

---

## 3. Data Flow Diagrams (DFD)

### Level 0 DFD: Context Diagram

```mermaid
flowchart TD
    User(("User / Browser"))
    Sensors["Soil Moisture Probe"]
    Reservoir["Reservoir Float Switch"]
    Pump["Water Pump Motor"]
    NVS[("ESP32 NVS Flash Storage")]

    Sensors -->|"Analog Voltage (ADC)"| System["Eyum Plant Care System (ESP32)"]
    Reservoir -->|"Digital Water Level (HIGH/LOW)"| System
    System -->|"Relay Control Signal (HIGH/LOW)"| Pump

    User -->|"HTTP GET /api/status"| System
    User -->|"HTTP POST /api/water (Manual Trigger)"| System
    User -->|"HTTP POST /api/settings (Thresholds)"| System
    User -->|"HTTP POST /api/calibration (Dry/Wet ADC)"| System

    System -->|"JSON Telemetry & Moisture Data"| User
    System <-->|"Read / Write Persistent Config"| NVS
```

### Level 1 DFD: Subsystem Process Decomposition

```mermaid
flowchart TD
    subgraph Sensing["1.0 Soil Sensing Subsystem"]
        P1["Read Analog Samples (7x)"]
        P2["Median Sorting & Fault Check"]
        P3["Calibration % Formula Map"]
    end

    subgraph Decision["2.0 Watering Decision Engine"]
        P4["Compare Moisture with Threshold"]
        P5["Check Safety Timers (Cooldown, Daily Max)"]
        P6["Check Water Reservoir Level"]
        P7["Issue Relay Actuation Command"]
    end

    subgraph Web["3.0 Local Web & API Subsystem"]
        P8["HTTP Server & Basic Auth"]
        P9["LittleFS Static Asset Streamer"]
        P10["REST API Endpoint Dispatcher"]
    end

    subgraph Storage["4.0 Persistence Engine"]
        P11["Load Settings on Boot"]
        P12["Commit Changes to NVS"]
    end

    GPIO35["GPIO 35"] --> P1
    P1 --> P2 --> P3
    P3 -->|"Moisture %"| P4
    P4 --> P5 --> P6 --> P7
    P7 -->|"GPIO 26 HIGH/LOW"| Relay["Relay / Pump"]

    Client["Web Browser"] <-->|"HTTP Requests"| P8
    P8 --> P9
    P8 --> P10
    P10 -->|"Trigger Manual Cycle"| P7
    P10 -->|"Update Thresholds"| P12
    P10 -->|"Fetch Live Status"| P3
    P11 <--> NVS[("NVS Flash")]
    P12 <--> NVS
```

---

## 4. State Machine & Workflow Diagrams

### 4.1. Autonomous Irrigation Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Idle

    state Idle {
        [*] --> Monitoring
        Monitoring --> SoilDryDetected : Moisture < Threshold AND AutoMode = TRUE
        SoilDryDetected --> CheckingSafety : Evaluate Safety Rules
    }

    state CheckingSafety {
        [*] --> CheckReservoir
        CheckReservoir --> ReservoirLow : Reservoir Empty
        CheckReservoir --> CheckDailyLimit : Reservoir OK
        CheckDailyLimit --> DailyLimitExceeded : Daily Pump Time >= Max
        CheckDailyLimit --> CheckCooldown : Daily Time OK
        CheckCooldown --> CooldownActive : (Now - LastStop) < CooldownMs
        CheckCooldown --> SafetyClear : Cooldown Expired
    }

    SafetyClear --> Watering : Set GPIO 26 HIGH
    CooldownActive --> Idle : Wait Next Interval
    DailyLimitExceeded --> Idle : Blocked
    ReservoirLow --> Idle : Alert Flagged

    state Watering {
        [*] --> PumpRunning
        PumpRunning --> Completed : Timer >= RuntimeMs
        PumpRunning --> EmergencyHalted : Stop API Called
        PumpRunning --> SensorFaultHalted : Probe Fault
    }

    Completed --> CooldownTimer : Set GPIO 26 LOW
    EmergencyHalted --> Idle : Set GPIO 26 LOW
    SensorFaultHalted --> Idle : Set GPIO 26 LOW

    state CooldownTimer {
        [*] --> RestingSoak : Start Cooldown Clock
        RestingSoak --> Idle : Cooldown Elapsed
    }
```

### 4.2. First-Run Wi-Fi Provisioning Flow

```mermaid
sequenceDiagram
    autonumber
    actor User
    participant ESP as ESP32 SoftAP
    participant Browser as Setup Portal (192.168.4.1)
    participant NVS as Flash NVS
    participant Router as Home Wi-Fi Router

    ESP->>ESP: Boot: Check NVS for saved Wi-Fi
    alt Credentials Not Found
        ESP->>ESP: Launch SoftAP "Eyum-Setup-XXXX"
        User->>ESP: Connect to Setup Wi-Fi
        User->>Browser: Open http://192.168.4.1
        Browser->>User: Display setup.html form
        User->>Browser: Enter SSID, Wi-Fi Password, Admin Password
        Browser->>ESP: POST /api/provision
        ESP->>NVS: Save Credentials & Mark Initialized
        ESP->>Browser: 200 OK {"ok": true}
        ESP->>ESP: ESP.restart()
    end
    ESP->>Router: Connect to Home Wi-Fi (STA Mode)
    ESP->>ESP: Register mDNS "eyum-plant.local"
    User->>Browser: Open http://eyum-plant.local
    Browser->>ESP: GET /api/status (Basic Auth)
    ESP->>Browser: 200 OK (Telemetry Data)
```

---

## 5. Memory & NVS Storage Schema

Settings are saved in the ESP32 Non-Volatile Storage (NVS) using `Preferences.h` under the namespace `"eyum"`:

| NVS Key | Type | Default Value | Description |
| :--- | :--- | :--- | :--- |
| `dry` | `int32` | `3000` | ADC reading representing 0% moisture (bone-dry soil). |
| `wet` | `int32` | `1500` | ADC reading representing 100% moisture (saturated soil). |
| `threshold` | `uint8` | `30` | Auto-mode watering threshold (triggers below this %). |
| `runtime` | `uint32` | `3000` (ms) | Single pump cycle runtime in milliseconds. |
| `cooldown` | `uint32` | `300000` (ms)| Rest time after watering (default 5 minutes). |
| `soak` | `uint32` | `30000` (ms) | Minimum soak absorption delay. |
| `daily` | `uint32` | `30000` (ms) | Maximum cumulative pump runtime in 24 hours (30s). |
| `auto` | `bool` | `false` | Autonomous irrigation enable flag. |
| `ssid` | `string`| `""` | Home Wi-Fi network name. |
| `wifiPass`| `string`| `""` | Home Wi-Fi network password. |
| `dashPass`| `string`| `""` | Administrator dashboard password. |
| `initialized`| `bool`| `false` | First-run provisioning completion flag. |

---

## 6. REST API Specification

All endpoints support HTTP Basic Authentication (Username: `admin`, Password: user-configured) and return standard CORS headers (`Access-Control-Allow-Origin: *`).

### 6.1. `GET /api/status`
Returns real-time telemetry, sensor readings, and actuator states.

```json
{
  "valid": true,
  "raw": 2415,
  "moisture": 42,
  "fault": "",
  "digitalTrigger": 1,
  "pump": false,
  "remainingMs": 0,
  "auto": true,
  "lastStop": "completed",
  "reservoirOk": true,
  "uptimeSec": 4320,
  "rssi": -62,
  "ip": "192.168.29.60",
  "mdns": "eyum-plant.local"
}
```

### 6.2. `POST /api/water`
Triggers an immediate safety-limited manual watering cycle.
- **Request Body**: `{}`
- **Response**: `200 OK {"ok": true}` or `409 Conflict {"error": "Pump already running"}`

### 6.3. `POST /api/stop`
Immediately halts the pump and records stop reason as `"manual"`.
- **Request Body**: `{}`
- **Response**: `200 OK {"ok": true}`

### 6.4. `POST /api/auto`
Enables or disables autonomous threshold watering.
- **Request Body**: `{"enabled": true}`
- **Response**: `200 OK {"ok": true}`

### 6.5. `POST /api/settings`
Updates watering thresholds and safety limits.
- **Request Body**:
```json
{
  "threshold": 35,
  "pumpRuntimeMs": 3500,
  "cooldownMs": 180000,
  "soakMs": 30000,
  "maxDailyPumpMs": 45000
}
```
- **Response**: `200 OK {"ok": true}`

### 6.6. `POST /api/calibration`
Saves dry and wet ADC values for percentage interpolation.
- **Request Body**: `{"dryRaw": 3150, "wetRaw": 1420}`
- **Response**: `200 OK {"ok": true}`

---

## 7. Safety & Anti-Flooding Algorithms

1. **Anti-Hammering Cooldown**: After any watering cycle, a mandatory cooldown (default: 5 minutes) prevents the pump from triggering repeatedly before water has soaked down to root depth.
2. **24-Hour Rolling Pump Allowance**: A cumulative runtime accumulator tracks total pump activation time. If the pump exceeds `maxDailyPumpMs` (default: 30 seconds), all further auto and manual watering requests are blocked with `Daily watering limit reached`.
3. **Median Noise Rejection**: Sensor sampling takes 7 successive analog readings separated by 2ms delays, sorts them, and extracts the median sample (index 3). This discards high-frequency electrical noise from nearby motors or Wi-Fi bursts.
4. **Independent Manual Override**: Manual `Water Now` commands operate with safety timers, but are never blocked by uncalibrated sensors or cooldown delays, guaranteeing operator control during testing.
