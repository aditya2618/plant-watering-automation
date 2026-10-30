# Eyum Plant Care — Cloud Architecture

> How the offline ESP32 device connects to the Oracle Cloud server and how you control it remotely.

---

## Big Picture

```
                        INTERNET
  ┌─────────────────┐              ┌──────────────────────────────────┐
  │  ESP32 Device   │              │    Oracle Cloud Server           │
  │  (Plant Care)   │              │    (Django + Channels + Redis)   │
  │                 │  WebSocket   │                                  │
  │  CloudClient ───┼──────────────┼──► /ws/iot/?device_id=X&secret=Y│
  │                 │  (ws:// or   │        │                         │
  │  ◄ commands ────┼──wss://)─────┼────────┤  IoTDeviceConsumer      │
  │    telemetry ──►│              │        │                         │
  │                 │              │        ├─► Redis Channel Layer    │
  └─────────────────┘              │        │        │                │
                                   │        │        ▼                │
  ┌─────────────────┐              │  IoTDashboardConsumer            │
  │  You (Browser / │  WebSocket   │  /ws/iot/dashboard/eyum-plant/   │
  │  Mobile / API)  │◄─────────────┼─────────────────────────────────┤
  │                 │              │                                  │
  │  REST API calls │  HTTP REST   │  /api/iot/devices/<id>/command/  │
  │  ──────────────►│─────────────►│                                  │
  └─────────────────┘              └──────────────────────────────────┘
```

---

## Step-by-Step: How the Device Connects

### 1. Device Boots Up
The ESP32 reads **CloudConfig** from NVS (Non-Volatile Storage). This has:
| Field | Example Value |
|---|---|
| `host` | `80.225.212.191` (your Oracle server IP) |
| `port` | `80` (or `443` for WSS) |
| `device_id` | `esp32-plant-care-01` |
| `device_secret` | `ytm_tznt7WFCsNWTVpU9pjpbNVxXbGCi5JmMx9RelOg` |
| `enabled` | `true` |

### 2. Device Connects to Home Wi-Fi
After provisioning, it connects to your home Wi-Fi (`WIFI_STA` mode).

### 3. CloudClient Opens a WebSocket
As soon as Wi-Fi is up, it opens a WebSocket connection to:
```
ws://80.225.212.191:80/ws/iot/?device_id=esp32-plant-care-01&secret=<token>
```

### 4. Cloud Server Authenticates the Device
The Django `IoTDeviceConsumer` on the server:
- Looks up `device_id` in the database
- Calls `device.check_secret(secret)` — PBKDF2 hash comparison
- If valid → marks the device **online**, joins it to `iot_device_<device_id>` and `iot_dashboard_eyum-plant` channel groups
- Sends back: `{"type": "connected", "device_id": "esp32-plant-care-01"}`

### 5. Device Sends Telemetry Every 30 Seconds
The ESP32 sends JSON over the WebSocket automatically:
```json
{
  "type": "telemetry",
  "payload": {
    "moisture": 62,
    "raw": 1840,
    "pump": false,
    "auto": true,
    "reservoirOk": true,
    "rssi": -55,
    "uptimeSec": 3600,
    "fw": "1.3.0"
  }
}
```
The cloud saves this to the `IoTTelemetry` table and broadcasts it live to any connected dashboard browsers.

### 6. Device Heartbeat Ping
Every 30 seconds, the device also sends a `ping` → server replies `pong`. If the connection drops, `WebSocketsClient` auto-reconnects every 5 seconds.

---

## How You Control the Device From the Cloud

### Method A — REST API (from any HTTP client, Postman, curl, script)

**Send a command:**
```http
POST /api/iot/devices/esp32-plant-care-01/command/
Authorization: Token <your_api_token>
Content-Type: application/json

{
  "action": "water"
}
```

**Available actions:**
| Action | What it does | Extra params |
|---|---|---|
| `water` | Starts the pump | _(none)_ |
| `stop` | Stops the pump | _(none)_ |
| `auto` | Toggle auto-watering | `{"enabled": true}` |
| `set_threshold` | Change moisture threshold | `{"threshold": 45}` |

**The command flow:**
```
Your API call
    │
    ▼
Django REST view (IoTDeviceCommandView)
    │ checks device is online
    ▼
Redis Channel Layer
    │  group_send("iot_device_esp32-plant-care-01", {...})
    ▼
IoTDeviceConsumer (the live WebSocket to the ESP32)
    │  pushes JSON down the wire
    ▼
ESP32 CloudClient.onCommand() callback in main.cpp
    │  handles: water / stop / auto / set_threshold
    ▼
Pump turns on/off, settings updated in NVS
    │
    ▼ ACK sent back
CloudClient.sendAck(cmdId, "acknowledged", "Watering started")
    │
    ▼
Server updates IoTCommandLog, broadcasts to dashboard
```

### Method B — WebSocket Dashboard (real-time UI)
Connect from a browser to:
```
ws://80.225.212.191:80/ws/iot/dashboard/eyum-plant/
```
You will receive live events:
```json
{"type": "telemetry",      "device_id": "esp32-plant-care-01", "payload": {...}}
{"type": "device_status",  "device_id": "esp32-plant-care-01", "is_online": true}
{"type": "command_ack",    "device_id": "esp32-plant-care-01", "status": "acknowledged"}
```

---

## What Happens When Device Goes Offline?

- `IoTDeviceConsumer.disconnect()` marks the device **offline** in the DB.
- Dashboard gets a `{"type": "device_status", "is_online": false}` event.
- Commands sent via REST API while offline return `HTTP 202 Accepted` with `"status": "queued"` — they are logged in `IoTCommandLog` with status `pending`.
- **The device continues to operate locally** — local auto-watering, local dashboard at `http://eyum-plant.local` — 100% offline-safe.

---

## REST API Quick Reference

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/iot/projects/` | GET / POST | List or create IoT projects |
| `/api/iot/devices/register/` | POST | Register a new device |
| `/api/iot/devices/?project=eyum-plant` | GET | List devices |
| `/api/iot/devices/<device_id>/` | GET | Device details + shadow state |
| `/api/iot/devices/<device_id>/command/` | POST | Send a command |
| `/api/iot/devices/<device_id>/telemetry/` | GET | Telemetry history |
| `/api/iot/devices/<device_id>/shadow/` | GET | Latest live device state |

---

## Device Provisioning (First-Time Setup)

### On the Cloud Server — Register the Device
Run on your Oracle server:
```bash
cd /var/www/cloud
python provision_iot_device.py \
  --project eyum-plant \
  --device-id esp32-plant-care-01 \
  --name "Balcony Plant Monitor" \
  --type esp32_plant_care
```
This prints a `device_secret` — **copy it, you only see it once**.

### On the ESP32 — Enter Cloud Settings
1. Connect to the ESP32 local dashboard at `http://eyum-plant.local`
2. Scroll to the **Eyum Cloud Settings** card at the bottom
3. Fill in:
   - **Host:** `80.225.212.191`
   - **Port:** `80`
   - **Device ID:** `esp32-plant-care-01`
   - **Device Secret:** _(paste the token from above)_
   - Toggle **Enable Cloud** → ON
4. Hit **Save** → the device connects immediately

### Verify Connection
Check the ESP32 Serial Monitor — you should see:
```
[Cloud] Connecting to 80.225.212.191:80 (SSL: NO) -> /ws/iot/?device_id=esp32-plant-care-01&secret=...
[Cloud] Connected to Eyum Cloud: /ws/iot/
[Cloud] Cloud server confirmed device session: esp32-plant-care-01
```

---

## Credential Summary

| Item | Value |
|---|---|
| Device ID | `esp32-plant-care-01` |
| Device Secret | `ytm_tznt7WFCsNWTVpU9pjpbNVxXbGCi5JmMx9RelOg` |
| Project slug | `eyum-plant` |
| WS Device URL | `ws://80.225.212.191/ws/iot/?device_id=esp32-plant-care-01&secret=<token>` |
| WS Dashboard URL | `ws://80.225.212.191/ws/iot/dashboard/eyum-plant/` |
| SoftAP password | `12345678` |
| Local dashboard | `http://eyum-plant.local` |
