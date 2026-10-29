# Eyum Plant Care

Local-first ESP32 plant watering firmware with an elegant light botanical dashboard. The controller operates completely locally without cloud dependencies, running autonomous watering and safety algorithms directly on the ESP32.

---

## Technical Documentation & Architecture

Comprehensive technical specifications and system diagrams are available in the **[`documentation/`](file:///d:/PROJECT/IOT-Projects/plant-watering%20automation/documentation/)** folder:

- **[System Architecture & DFDs](file:///d:/PROJECT/IOT-Projects/plant-watering%20automation/documentation/SYSTEM_ARCHITECTURE.md)**: Hardware schematic, pinout assignments, Level 0 & Level 1 Data Flow Diagrams (Mermaid), State Machine workflows, REST API contracts, and NVS storage schemas.
- **[Master 1-Time Genesis Prompt](file:///d:/PROJECT/IOT-Projects/plant-watering%20automation/documentation/ONE_TIME_PROMPT.md)**: Master prompt to generate or rebuild this entire project from scratch in a single shot.

---

## Hardware Connections

### 4-Pin Capacitive Moisture Sensor

| Sensor Pin | ESP32 Pin | Purpose |
| :--- | :--- | :--- |
| **VCC** | **`3V3`** | 3.3V Power (do not connect to 5V to protect ESP32 ADC) |
| **GND** | **`GND`** | Ground reference |
| **AO** (Analog Out) | **`GPIO35`** | Primary continuous moisture reading (0%–100% calibrated) |
| **DO** (Digital Out) | **`GPIO32`** *(Optional)* | Hardware threshold trigger from onboard trimmer potentiometer |

### Pump & Actuator Connections

| Part | ESP32 Pin | Notes |
| --- | --- | --- |
| Relay / MOSFET Input | **`GPIO26`** | Active-high pump control (`HIGH` = ON, `LOW` = OFF) |
| Common Ground | **`GND`** | Connect ESP32 GND and Pump Power Supply GND |
| Optional Reservoir Switch | **`GPIO34`** | Float switch for dry-run protection |

> [!WARNING]
> Use a correctly rated external pump power supply (e.g., 5V or 12V adapter), flyback diode, fuse, and gate pulldown resistor. Never power inductive pump motors directly from the ESP32 GPIOs or 3V3 rail.

---

## Features

- **Light Botanical UI**: Calming, clean light theme inspired by organic horticulture (warm linen background, forest evergreen accents, zero blue, zero neon).
- **Circular SVG Moisture Gauge**: Real-time soil moisture percentage with live status zones (Optimal, Dry, Saturated).
- **mDNS Auto-Discovery**: Access directly via `http://eyum-plant.local` on your local network.
- **Autonomous & Manual Irrigation**: One-tap manual watering with automatic timer, plus autonomous mode triggered when moisture drops below threshold.
- **Hardware Flood Safety**:
  - Per-cycle pump run limit (0.25s - 10s).
  - Cooldown & soak period (0.5m - 24h) to avoid root over-saturation.
  - 24-hour cumulative pump limit to protect against line disconnections.
  - Low water reservoir sensing support.
- **CORS-Enabled REST API**: Ready for companion mobile app integration.
- **First-Run Captive Portal**: Built-in Wi-Fi provisioning on initial boot (`Eyum-Setup-XXXX` at `http://192.168.4.1`).

---

## Flashing Instructions (PlatformIO)

```bash
cd "d:\PROJECT\IOT-Projects\plant-watering automation"

# 1. Flash the C++ firmware
pio run --target upload

# 2. Flash the web dashboard to LittleFS
pio run --target uploadfs

# 3. Open Serial Monitor (115200 baud)
pio device monitor --baud 115200
```

---

## First-Run Commissioning

1. On first boot, connect to the setup Wi-Fi network: `Eyum-Setup-XXXX` using default password **`12345678`**.
2. Open `http://192.168.4.1` in your browser.
3. Enter your home 2.4 GHz Wi-Fi credentials and create a dashboard password.
4. Once connected, open `http://eyum-plant.local` (or the IP address from the serial log, e.g. `http://192.168.29.60`).
5. Log in with username `admin` and your chosen dashboard password.
6. Calibrate the probe using the live ADC buttons in dry and moist soil before enabling Auto Mode.
