# Eyum Plant Care — Quick Reference

## SoftAP Setup
| Item | Value |
|---|---|
| Wi-Fi SSID | `Eyum-Setup-XXXX` (XXXX = last 4 hex of chip ID) |
| Password | `12345678` |
| Setup page | `http://192.168.4.1` |
| Local dashboard | `http://eyum-plant.local` |

---

## Flash & Upload Commands

```powershell
# Build firmware
pio run

# Upload firmware to ESP32
pio run -t upload

# Build LittleFS web dashboard image
pio run -t buildfs

# Upload LittleFS web dashboard to ESP32 flash
pio run -t uploadfs

# Monitor serial output
pio device monitor --baud 115200

# Build and upload everything in one go
pio run -t upload ; pio run -t uploadfs
```

---

## Pin Reference

| Function | GPIO | Notes |
|---|---|---|
| Soil Moisture (Analog) | GPIO 35 | Input-only ADC1, 12-bit, 11dB atten |
| Soil Moisture (Digital) | GPIO 32 | Optional sensor threshold trigger |
| Water Pump / MOSFET Gate | GPIO 26 | Active HIGH |
| Reservoir Float Switch | GPIO 34 | Optional, active LOW = water OK |

---

## Default Settings

| Parameter | Default |
|---|---|
| Dry calibration (dryRaw) | 3000 ADC |
| Wet calibration (wetRaw) | 1500 ADC |
| Auto-water threshold | 30% moisture |
| Pump runtime | 3000 ms (3 sec) |
| Cooldown after watering | 300,000 ms (5 min) |
| Soak delay | 30,000 ms (30 sec) |
| Max daily pump time | 30,000 ms (30 sec total/day) |
| Auto mode | OFF (manual default) |
| Telemetry interval (cloud) | 30,000 ms (30 sec) |

---

## REST API Quick Reference

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/status` | GET | Live moisture, pump state, auto mode |
| `/api/settings` | GET / POST | Read/update thresholds and timers |
| `/api/water` | POST | Manually trigger watering |
| `/api/stop` | POST | Immediately stop pump |
| `/api/auto` | POST | Enable/disable auto mode |
| `/api/calibration` | POST | Save dry/wet ADC calibration values |
| `/api/cloud` | GET / POST | Read/update cloud connection settings |
| `/api/provision` | POST | Set Wi-Fi credentials (setup mode) |

---

## Calibration Procedure

1. Put the sensor in **completely dry soil** → record `raw` value from `/api/status` → that is `dryRaw`
2. Put the sensor in **fully watered soil** → record `raw` value → that is `wetRaw`
3. POST to `/api/calibration`:
```json
{"dryRaw": 3150, "wetRaw": 1420}
```

---

## NVS Keys (namespace: `"eyum"`)

| Key | Type | Description |
|---|---|---|
| `dry` | int32 | Dry ADC reference |
| `wet` | int32 | Wet ADC reference |
| `threshold` | uint8 | Auto trigger threshold % |
| `runtime` | uint32 | Pump cycle duration ms |
| `cooldown` | uint32 | Cooldown timer ms |
| `soak` | uint32 | Soak delay ms |
| `daily` | uint32 | Daily pump time limit ms |
| `auto` | bool | Auto mode enabled flag |
| `ssid` | string | Home Wi-Fi SSID |
| `wifiPass` | string | Home Wi-Fi password |
| `dashPass` | string | Dashboard admin password |
| `initialized` | bool | Provisioning complete flag |
| `cloudHost` | string | Cloud server host/IP |
| `cloudPort` | uint16 | Cloud server port |
| `cloudId` | string | Cloud device ID |
| `cloudSecret` | string | Cloud authentication token |
| `cloudEnabled` | bool | Cloud integration on/off |
| `cloudSsl` | bool | Use WSS (TLS) flag |
| `cloudTelMs` | uint32 | Cloud telemetry interval ms |

---

## Cloud Quick Commands (curl)

```bash
# Check device shadow state
curl -H "Authorization: Token <token>" \
  http://<cloud-ip>/api/iot/devices/esp32-plant-care-01/shadow/

# Start watering remotely
curl -X POST -H "Authorization: Token <token>" \
  -H "Content-Type: application/json" \
  -d '{"action": "water"}' \
  http://<cloud-ip>/api/iot/devices/esp32-plant-care-01/command/

# Stop watering remotely
curl -X POST -H "Authorization: Token <token>" \
  -H "Content-Type: application/json" \
  -d '{"action": "stop"}' \
  http://<cloud-ip>/api/iot/devices/esp32-plant-care-01/command/

# Enable auto mode remotely
curl -X POST -H "Authorization: Token <token>" \
  -H "Content-Type: application/json" \
  -d '{"action": "auto", "params": {"enabled": true}}' \
  http://<cloud-ip>/api/iot/devices/esp32-plant-care-01/command/

# Set moisture threshold remotely
curl -X POST -H "Authorization: Token <token>" \
  -H "Content-Type: application/json" \
  -d '{"action": "set_threshold", "params": {"threshold": 45}}' \
  http://<cloud-ip>/api/iot/devices/esp32-plant-care-01/command/

# Get telemetry history (last 24h)
curl -H "Authorization: Token <token>" \
  "http://<cloud-ip>/api/iot/devices/esp32-plant-care-01/telemetry/?hours=24&limit=100"
```

---

## Cloud Credentials

| Item | Value |
|---|---|
| Device ID | `esp32-plant-care-01` |
| Device Secret | `ytm_tznt7WFCsNWTVpU9pjpbNVxXbGCi5JmMx9RelOg` |
| Project slug | `eyum-plant` |
| Dashboard WS URL | `ws://<host>/ws/iot/dashboard/eyum-plant/` |

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| Moisture always 0% or 100% | Not calibrated | Run calibration with dry/wet soil |
| Pump not starting | Daily limit or cooldown active | Check `/api/status` → `lastStop` |
| ADC error in serial | Wrong `analogSetPinAttenuation` call | Fixed in v1.3 (use `analogSetAttenuation`) |
| No AP after boot | Already provisioned | Hold reset or erase NVS |
| Cloud not connecting | Wrong host/device_id/secret | Re-enter in Cloud Settings card on dashboard |
| Dashboard 401 error | Wrong password | Use the password set during provisioning |
| Flash > 100% | Default partition | Fixed in v1.3 with `min_spiffs.csv` |
