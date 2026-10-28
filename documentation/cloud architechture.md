# Eyum Cloud IoT Platform — Production-Grade Architecture

> **Scope:** A universal, self-hosted cloud backend that can adopt _any_ offline ESP32/ESP8266 IoT project (Plant Watering, Water Tank Automation, and future projects) into a secure, remotely-accessible, observable cloud platform — without vendor lock-in.

---

## 1. The Problem Statement: Why Cloud?

| Current Offline Reality                          | Cloud Upgrade Goal                            |
| ------------------------------------------------ | --------------------------------------------- |
| Dashboard only on LAN (same Wi-Fi)               | Access from anywhere in the world             |
| No historical data — only live readings          | Full time-series telemetry history & trends   |
| Settings changed manually per device             | Remote configuration push from server         |
| No alerts — physical presence required           | Push notifications / email on critical events |
| OTA firmware updates need USB cable              | Signed OTA firmware delivery over the air     |
| Single device, single project                    | Multi-device, multi-project unified console   |
| Plain HTTP Basic Auth (password visible on wire) | TLS-encrypted connections end-to-end          |

---

## 2. Architecture Philosophy

The platform is designed around **three core principles**:

1. **Offline-First Edge Intelligence** — ESP32 retains all autonomous safety logic. The cloud layer adds remote visibility and control, it never becomes a single point of failure for local operations.
2. **Protocol Separation** — MQTT for device telemetry (low-latency, async), HTTPS/REST for user-facing management (stateless, secure).
3. **Universal Device Adapter** — Any ESP32/ESP8266 project registers as a "device" under a "project". The server is generic; project-specific meaning is decoded via registered _device schemas_.

---

## 3. High-Level System Architecture

```
╔══════════════════════════════════════════════════════════════════════════════════════╗
║                              EYUM CLOUD PLATFORM (VPS)                              ║
║                                                                                      ║
║  ┌──────────────────────────────────────────────────────────────────────────────┐   ║
║  │                     NGINX REVERSE PROXY  (TLS Termination)                   │   ║
║  │         yourdomain.com  ──► API Gateway   :443 (HTTPS)                       │   ║
║  │         mqtt.yourdomain.com ──► MQTT Broker  :8883 (MQTTS)                   │   ║
║  └──────┬──────────────────────────────────────────┬───────────────────────────┘   ║
║         │                                          │                                ║
║         ▼                                          ▼                                ║
║  ┌──────────────────┐                   ┌─────────────────────────┐                ║
║  │   EMQX Broker    │ ◄── MQTT/TLS ──   │    Eyum API Server      │                ║
║  │  (Port 8883 TLS) │                   │  (Node.js / Fastify)    │                ║
║  │  ┌─────────────┐ │                   │  ┌─────────────────────┐│                ║
║  │  │   ACL/Auth  │ │ ──MQTT Bridge──►  │  │  Device Registry    ││                ║
║  │  │   Plugin    │ │                   │  │  Command Dispatcher ││                ║
║  │  └─────────────┘ │                   │  │  OTA Job Manager    ││                ║
║  └──────┬───────────┘                   │  │  Alert Engine       ││                ║
║         │                               │  └─────────────────────┘│                ║
║         │  (internal MQTT subscribe)    └──────────┬──────────────┘                ║
║         └──────────────────────────────────────────┘                               ║
║                          │                                                          ║
║                          ▼                                                          ║
║  ┌───────────────────────────────────────────────────────────────────────────────┐  ║
║  │                          DATA LAYER                                           │  ║
║  │  ┌─────────────────────┐  ┌────────────────────┐  ┌────────────────────────┐ │  ║
║  │  │  TimescaleDB        │  │   PostgreSQL        │  │   Redis                │ │  ║
║  │  │  (Telemetry         │  │   (Devices, Users,  │  │   (Device Shadows,     │ │  ║
║  │  │   Time-Series)      │  │    Settings, OTA)   │  │    Real-time Cache,    │ │  ║
║  │  └─────────────────────┘  └────────────────────┘  │    Pub/Sub for WS)     │ │  ║
║  └───────────────────────────────────────────────────┴────────────────────────┘  ║
║                                                                                      ║
║  ┌─────────────────────────┐   ┌──────────────────────────────────────────────┐    ║
║  │   Grafana               │   │   MinIO / Local Storage                       │    ║
║  │   (Historical Dashboards│   │   (OTA Firmware Binaries)                     │    ║
║  │    & Alerting)          │   └──────────────────────────────────────────────┘    ║
║  └─────────────────────────┘                                                        ║
╚══════════════════════════════════════════════════════════════════════════════════════╝
          ▲                              ▲
          │  MQTT/TLS :8883              │  HTTPS :443
          │                              │
┌─────────┴────────────────┐   ┌─────────┴───────────────────────────┐
│  ESP32 / ESP8266 Devices  │   │  Web Dashboard / Mobile App          │
│  ┌──────────────────────┐ │   │  (Vanilla JS or React/Expo)          │
│  │  Eyum Plant Care     │ │   │  ┌───────────────────────────────┐   │
│  │  (Soil + Pump)       │ │   │  │  Live device status           │   │
│  ├──────────────────────┤ │   │  │  Historical charts (Grafana)  │   │
│  │  iYum Water Auto     │ │   │  │  Remote settings push         │   │
│  │  (Tank + Relay Cmd)  │ │   │  │  OTA firmware management      │   │
│  ├──────────────────────┤ │   │  │  Multi-project selector       │   │
│  │  Future Project N    │ │   │  └───────────────────────────────┘   │
│  └──────────────────────┘ │   └──────────────────────────────────────┘
└──────────────────────────┘
```

---

## 4. Layer-by-Layer Deep Dive

### 4.1. Edge Layer (ESP32 Firmware Changes)

This is the **minimal invasive upgrade** to the existing offline firmware.

#### 4.1.1. Dual-Mode Boot Strategy

The device still works 100% offline if cloud is unreachable. Cloud is an _enhancement layer_:

```
Boot
 ├── Connect to Home Wi-Fi (unchanged)
 ├── Start local WebServer on port 80 (unchanged - LAN still works)
 ├── Connect to Cloud MQTT Broker (NEW - non-blocking, background task)
 │    ├── SUCCESS → Cloud features enabled
 │    └── FAIL → Continue offline silently, retry every 30s
 └── All local safety logic unchanged
```

#### 4.1.2. MQTT Client Addition (New firmware module: `CloudClient.h`)

```cpp
// CloudClient.h - New module to add to existing projects
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

class CloudClient {
public:
    // Device identity
    const char* deviceId;      // Unique per device, stored in NVS
    const char* projectSlug;   // e.g., "eyum-plant" or "iyum-water"

    // Topic structure: eyum/{project}/{deviceId}/{direction}
    // Telemetry:  eyum/eyum-plant/esp32-abc123/telemetry
    // Commands:   eyum/eyum-plant/esp32-abc123/commands
    // Shadow:     eyum/eyum-plant/esp32-abc123/shadow/reported
    // OTA:        eyum/eyum-plant/esp32-abc123/ota/update

    void begin(const char* broker, int port, const char* caCert);
    void publishTelemetry(const String& jsonPayload);
    void publishShadow(const String& jsonPayload);
    void onCommand(std::function<void(String)> callback);
    void loop(); // Call from Arduino loop()
    bool isConnected();
};
```

#### 4.1.3. MQTT Topic Schema

```
eyum/{project_slug}/{device_id}/telemetry       ← Device publishes sensor data (QoS 1)
eyum/{project_slug}/{device_id}/shadow/reported ← Device publishes current state (QoS 1)
eyum/{project_slug}/{device_id}/shadow/desired  ← Server pushes config changes (QoS 1)
eyum/{project_slug}/{device_id}/commands        ← Server sends water/stop/auto commands (QoS 1)
eyum/{project_slug}/{device_id}/ota/update      ← Server pushes OTA job notification (QoS 2)
eyum/{project_slug}/{device_id}/lwt             ← Last Will Testament (offline alert)
```

#### 4.1.4. Telemetry Payload (Plant Care Example)

```json
{
  "ts": 1745000000,
  "deviceId": "esp32-abc123",
  "project": "eyum-plant",
  "fw": "1.2.0",
  "moisture": 42,
  "raw": 2415,
  "pump": false,
  "auto": true,
  "reservoirOk": true,
  "rssi": -62,
  "uptimeSec": 4320,
  "dailyUsedMs": 6000
}
```

#### 4.1.5. Security on the Edge

- **TLS 1.2+** with the server's Root CA certificate burned into firmware
- **Device credentials** stored in NVS (never in source code)
- **Client ID** = `{project}-{deviceId}` — unique and ACL-enforced on broker
- **QoS 1** for telemetry (at-least-once delivery guarantee)
- **Last Will & Testament** — broker auto-publishes offline event if device drops

---

### 4.2. Communication Layer: EMQX MQTT Broker

**Why EMQX over Mosquitto?**

- Built-in REST API for dynamic ACL management
- Rule engine for routing MQTT → database without extra code
- Supports 100K+ concurrent connections on a modest VPS
- Dashboard UI for monitoring connections

#### 4.2.1. Authentication & Authorization Model

```
Device connects with:
  ClientID: eyum-plant-esp32-abc123
  Username:  esp32-abc123
  Password:  <per-device-token stored in server DB>

ACL Rules (auto-generated per device registration):
  PUBLISH    eyum/eyum-plant/esp32-abc123/telemetry     ✓
  PUBLISH    eyum/eyum-plant/esp32-abc123/shadow/reported ✓
  SUBSCRIBE  eyum/eyum-plant/esp32-abc123/commands      ✓
  SUBSCRIBE  eyum/eyum-plant/esp32-abc123/shadow/desired ✓
  SUBSCRIBE  eyum/eyum-plant/esp32-abc123/ota/update    ✓
  PUBLISH    eyum/#   (other devices)                   ✗ DENIED
```

---

### 4.3. Backend API Server (Node.js / Fastify)

**Responsibilities:**

1. User authentication (JWT tokens)
2. Device registration and credential issuing
3. MQTT subscription → TimescaleDB ingestion
4. Command dispatch (water, stop, auto, settings push)
5. Device Shadow management (Redis)
6. OTA job management
7. Alert/notification engine

#### 4.3.1. REST API Design

```
# Authentication
POST   /api/auth/login              → JWT token
POST   /api/auth/refresh            → Refresh JWT

# Projects
GET    /api/projects                → List user's projects
POST   /api/projects                → Create project
GET    /api/projects/:slug          → Project details + devices

# Devices
GET    /api/devices                 → All devices (filtered by project)
POST   /api/devices/register        → Register new device, get credentials
GET    /api/devices/:id             → Device details + live shadow
GET    /api/devices/:id/telemetry   → Historical telemetry (paginated)
DELETE /api/devices/:id             → Remove device

# Remote Control
POST   /api/devices/:id/command     → Send command {action: "water"|"stop"|"auto"}
POST   /api/devices/:id/settings    → Push settings to device shadow

# OTA
POST   /api/ota/firmware            → Upload new firmware binary
POST   /api/ota/jobs                → Assign OTA job to device(s)
GET    /api/ota/jobs/:id            → OTA job status

# Alerts
GET    /api/alerts                  → Alert history
POST   /api/alerts/rules            → Create alert rule
```

#### 4.3.2. Device Shadow (Redis)

A real-time snapshot of each device's current state — updated every time a telemetry message arrives:

```json
// Redis key: shadow:eyum-plant:esp32-abc123
{
  "reported": {
    "moisture": 42,
    "pump": false,
    "auto": true,
    "reservoirOk": true,
    "online": true,
    "lastSeen": "2026-10-28T20:45:00Z",
    "fw": "1.2.0"
  },
  "desired": {
    "auto": true,
    "threshold": 35,
    "pumpRuntimeMs": 3500
  },
  "delta": {
    "threshold": 35
  }
}
```

---

### 4.4. Data Layer

#### 4.4.1. TimescaleDB Schema (Time-Series Telemetry)

```sql
-- Hypertable for all IoT telemetry across all projects
CREATE TABLE telemetry (
    time         TIMESTAMPTZ     NOT NULL,
    device_id    VARCHAR(64)     NOT NULL,
    project_slug VARCHAR(64)     NOT NULL,
    metric       VARCHAR(64)     NOT NULL,
    value        DOUBLE PRECISION NOT NULL
);
SELECT create_hypertable('telemetry', 'time');
CREATE INDEX ON telemetry (device_id, time DESC);

-- Query: Last 24h moisture for a plant device
SELECT time, value FROM telemetry
WHERE device_id = 'esp32-abc123'
  AND metric = 'moisture'
  AND time > NOW() - INTERVAL '24 hours'
ORDER BY time DESC;
```

#### 4.4.2. PostgreSQL Schema (Relational)

```sql
-- Users
CREATE TABLE users (
  id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  email       VARCHAR(255) UNIQUE NOT NULL,
  password_hash VARCHAR(255) NOT NULL,
  created_at  TIMESTAMPTZ DEFAULT NOW()
);

-- Projects (e.g., "eyum-plant", "iyum-water")
CREATE TABLE projects (
  id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id     UUID REFERENCES users(id),
  slug        VARCHAR(64) UNIQUE NOT NULL,
  name        VARCHAR(255) NOT NULL,
  description TEXT,
  created_at  TIMESTAMPTZ DEFAULT NOW()
);

-- Device Registry
CREATE TABLE devices (
  id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  project_id   UUID REFERENCES projects(id),
  device_id    VARCHAR(64) UNIQUE NOT NULL,  -- e.g., "esp32-abc123"
  name         VARCHAR(255),
  mqtt_password_hash VARCHAR(255) NOT NULL,
  fw_version   VARCHAR(32),
  last_seen    TIMESTAMPTZ,
  created_at   TIMESTAMPTZ DEFAULT NOW()
);

-- OTA Firmware Versions
CREATE TABLE firmware (
  id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  project_id   UUID REFERENCES projects(id),
  version      VARCHAR(32) NOT NULL,
  binary_url   TEXT NOT NULL,
  sha256       VARCHAR(64) NOT NULL,
  release_notes TEXT,
  created_at   TIMESTAMPTZ DEFAULT NOW()
);

-- OTA Job Assignments
CREATE TABLE ota_jobs (
  id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  device_id    UUID REFERENCES devices(id),
  firmware_id  UUID REFERENCES firmware(id),
  status       VARCHAR(32) DEFAULT 'pending', -- pending/in_progress/success/failed/rolled_back
  started_at   TIMESTAMPTZ,
  completed_at TIMESTAMPTZ
);

-- Alert Rules
CREATE TABLE alert_rules (
  id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  device_id    UUID REFERENCES devices(id),
  metric       VARCHAR(64) NOT NULL,
  condition    VARCHAR(8) NOT NULL,  -- '<', '>', '==', etc.
  threshold    DOUBLE PRECISION NOT NULL,
  notify_email VARCHAR(255),
  notify_webhook TEXT
);
```

---

### 4.5. OTA Firmware Update Flow

```
1. Developer uploads firmware.bin to API server
   POST /api/ota/firmware { version: "1.3.0", binary: <file> }
   → Server stores binary in MinIO/local storage
   → Server computes SHA-256 hash
   → Records in firmware table

2. Admin assigns OTA job
   POST /api/ota/jobs { deviceId: "esp32-abc123", firmwareId: "..." }
   → Server publishes MQTT message to device:
     Topic: eyum/eyum-plant/esp32-abc123/ota/update
     Payload: { "version": "1.3.0", "url": "https://api.yourdomain.com/ota/download/abc", "sha256": "..." }

3. ESP32 receives OTA notification
   → Downloads binary via HTTPS (TLS-secured, Root CA verified)
   → Writes to inactive OTA partition
   → Verifies SHA-256 checksum
   → Marks partition valid, reboots

4. ESP32 reports success
   → Publishes to shadow/reported: { "fw": "1.3.0" }
   → Server updates ota_jobs.status = 'success'

5. Rollback (if firmware crashes on boot)
   → ESP-IDF bootloader auto-rolls back to previous partition
   → Device publishes: { "fw": "1.2.0", "otaRollback": true }
   → Server marks job status = 'rolled_back', sends alert
```

---

## 5. Security Architecture (Production Hardening)

```
┌─────────────────────────────────────────────────────────────────────┐
│                        SECURITY LAYERS                              │
│                                                                     │
│  Layer 1: Transport Security                                        │
│  ├── All MQTT connections: TLS 1.2+ on port 8883                    │
│  ├── All HTTP connections: TLS 1.2+ (Let's Encrypt) on port 443     │
│  └── Root CA cert embedded in ESP32 firmware binary                 │
│                                                                     │
│  Layer 2: Authentication                                            │
│  ├── Device → Broker: Per-device username + hashed password + ACL  │
│  ├── User → API: JWT (RS256, 1h expiry) + refresh token (7 days)   │
│  └── Device credentials: Issued at registration, stored hashed      │
│                                                                     │
│  Layer 3: Authorization                                             │
│  ├── EMQX ACL: Device can ONLY pub/sub its own topic namespace      │
│  ├── API: JWT claims enforce user can only see their own devices     │
│  └── DB: User-scoped queries (WHERE user_id = $jwt_user_id)         │
│                                                                     │
│  Layer 4: Data Integrity                                            │
│  ├── OTA: SHA-256 checksum verification before flashing             │
│  ├── OTA URLs: Time-limited signed URLs (expire after 1 hour)       │
│  └── Firmware signing (ESP32 Secure Boot V2 - advanced)            │
│                                                                     │
│  Layer 5: Infrastructure Hardening                                  │
│  ├── VPS: UFW firewall — only ports 22, 80, 443, 8883 open         │
│  ├── EMQX dashboard (18083) NOT exposed to internet                 │
│  ├── PostgreSQL/Redis: Internal Docker network only                 │
│  ├── Rate limiting: 100 req/min per device on REST API              │
│  └── Fail2ban on SSH                                                │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 6. Complete Docker Compose Stack

```yaml
# docker-compose.yml — Full Eyum Cloud IoT Platform
version: "3.9"

services:
  # ── NGINX Reverse Proxy ──────────────────────────────────────────
  nginx:
    image: nginx:alpine
    restart: always
    ports:
      - "80:80"
      - "443:443"
    volumes:
      - ./nginx/nginx.conf:/etc/nginx/nginx.conf:ro
      - ./certbot/conf:/etc/letsencrypt:ro
      - ./certbot/www:/var/www/certbot:ro
    depends_on:
      - api
      - grafana
    networks:
      - iot-net

  certbot:
    image: certbot/certbot
    volumes:
      - ./certbot/conf:/etc/letsencrypt
      - ./certbot/www:/var/www/certbot

  # ── EMQX MQTT Broker ─────────────────────────────────────────────
  emqx:
    image: emqx/emqx:5.8.0
    restart: always
    environment:
      - EMQX_ALLOW_ANONYMOUS=false
      - EMQX_LOADED_PLUGINS=emqx_auth_http
      - EMQX_AUTH__HTTP__AUTH_REQ__URL=http://api:3000/internal/mqtt/auth
      - EMQX_AUTH__HTTP__ACL_REQ__URL=http://api:3000/internal/mqtt/acl
    ports:
      - "8883:8883" # MQTT over TLS (external devices)
      - "1883:1883" # MQTT plain (internal only - firewall blocks external)
    volumes:
      - emqx_data:/opt/emqx/data
      - ./certs:/opt/emqx/etc/certs:ro
    networks:
      - iot-net

  # ── Node.js API Server ───────────────────────────────────────────
  api:
    build: ./backend
    restart: always
    environment:
      - NODE_ENV=production
      - DATABASE_URL=postgresql://eyum:${DB_PASSWORD}@timescaledb:5432/eyumdb
      - REDIS_URL=redis://redis:6379
      - MQTT_BROKER_URL=mqtt://emqx:1883
      - JWT_SECRET=${JWT_SECRET}
      - OTA_STORAGE_PATH=/app/firmware
    volumes:
      - firmware_store:/app/firmware
    depends_on:
      - timescaledb
      - redis
      - emqx
    networks:
      - iot-net

  # ── TimescaleDB ──────────────────────────────────────────────────
  timescaledb:
    image: timescale/timescaledb:latest-pg16
    restart: always
    environment:
      - POSTGRES_DB=eyumdb
      - POSTGRES_USER=eyum
      - POSTGRES_PASSWORD=${DB_PASSWORD}
    volumes:
      - pg_data:/var/lib/postgresql/data
      - ./db/init.sql:/docker-entrypoint-initdb.d/01_init.sql
    networks:
      - iot-net

  # ── Redis (Device Shadows + Pub/Sub) ─────────────────────────────
  redis:
    image: redis:7-alpine
    restart: always
    command: redis-server --requirepass ${REDIS_PASSWORD} --appendonly yes
    volumes:
      - redis_data:/data
    networks:
      - iot-net

  # ── Grafana (Historical Dashboards) ──────────────────────────────
  grafana:
    image: grafana/grafana:latest
    restart: always
    environment:
      - GF_SECURITY_ADMIN_PASSWORD=${GRAFANA_PASSWORD}
      - GF_SERVER_ROOT_URL=https://yourdomain.com/grafana/
      - GF_SERVER_SERVE_FROM_SUB_PATH=true
    volumes:
      - grafana_data:/var/lib/grafana
      - ./grafana/dashboards:/etc/grafana/provisioning/dashboards
      - ./grafana/datasources:/etc/grafana/provisioning/datasources
    networks:
      - iot-net

volumes:
  emqx_data:
  pg_data:
  redis_data:
  grafana_data:
  firmware_store:

networks:
  iot-net:
    driver: bridge
```

---

## 7. Firmware Migration Plan (Existing Projects)

### 7.1. Eyum Plant Care (plant-watering automation)

**Files to ADD to existing project:**

```
src/
├── CloudClient.h          ← NEW: MQTT cloud connection manager
├── CloudClient.cpp        ← NEW: TLS connect, pub/sub, reconnect logic
└── main.cpp               ← MODIFY: Add CloudClient, telemetry publish in loop()
```

**Changes to `main.cpp`** (additions only, all existing logic untouched):

```cpp
// Add to setup():
cloudClient.begin(MQTT_BROKER, MQTT_PORT, ROOT_CA_CERT);
cloudClient.onCommand([](String cmd) {
    // Handle: {"action": "water"} or {"action": "stop"} or {"action": "auto", "enabled": true}
    // This calls the same watering.requestWater() etc. that the local API calls
});

// Add to loop():
cloudClient.loop();
if (cloudClient.isConnected() && (now - lastCloudPublish >= CLOUD_INTERVAL_MS)) {
    lastCloudPublish = now;
    cloudClient.publishTelemetry(buildTelemetryJson());
    cloudClient.publishShadow(buildShadowJson());
}
```

### 7.2. iYum Water Automation (eyum-water-automation)

**Same pattern — add `CloudClient` module to both ESP32 controller and relay:**

```
ESP32 controller publishes:
  - Tank water level %
  - Pump state (ON/OFF)
  - Relay acknowledgement status
  - Ultrasonic distance raw reading

ESP32 controller subscribes:
  - commands: {"action": "pump_on"} / {"action": "pump_off"} / {"action": "auto"}
  - shadow/desired: {"startThreshold": 25, "stopThreshold": 95}
```

### 7.3. Any Future Project

1. Create a new "project" via API: `POST /api/projects { slug: "my-new-device" }`
2. Register device: `POST /api/devices/register` → get `deviceId` + `mqttPassword`
3. Store credentials in ESP32 NVS
4. Add `CloudClient.h` to firmware
5. Define custom telemetry JSON schema in project settings
6. Done — full cloud visibility from day one

---

## 8. Cloud Dashboard Features

### 8.1. Web Dashboard (Vanilla JS - matches existing aesthetic)

```
┌──────────────────────────────────────────────────────────────┐
│  EYUM IoT CLOUD                          [admin@eyum.local]  │
├────────────────────┬─────────────────────────────────────────┤
│  Projects          │  Eyum Plant Care  ●Online               │
│  ├ Eyum Plant Care │ ─────────────────────────────────────── │
│  └ iYum Water Auto │  [Moisture Gauge SVG]  [Tank Level SVG] │
│                    │                                         │
│  [+ Add Project]   │  📊 History: [1H] [24H] [7D] [30D]     │
│                    │  [Grafana embedded chart]               │
│                    │                                         │
│                    │  Remote Controls:                       │
│                    │  [Water Now] [Stop] [Auto: ON]          │
│                    │                                         │
│                    │  ⚠ Alerts: Reservoir empty (2h ago)     │
│                    │  📡 OTA: v1.2.0 → v1.3.0 [Deploy]      │
└────────────────────┴─────────────────────────────────────────┘
```

### 8.2. Alert System

```
Trigger conditions:
  - Moisture < 15% for > 30 minutes (plant critically dry)
  - Reservoir empty (reservoirOk = false)
  - Device offline for > 5 minutes
  - Daily pump limit reached
  - OTA update failed / rolled back

Notification channels:
  - Email (Nodemailer + SMTP)
  - Webhook (POST to user-defined URL — supports Telegram bots, etc.)
  - In-dashboard notification badge
```

---

## 9. VPS Deployment Checklist

### Minimum VPS Spec (for personal/small scale)

```
CPU:    2 vCPU
RAM:    4 GB
Disk:   40 GB SSD
OS:     Ubuntu 22.04 LTS
Ports:  22 (SSH), 80 (HTTP→redirect), 443 (HTTPS), 8883 (MQTTS)
```

### Step-by-Step Deployment

```bash
# 1. Secure the server
sudo ufw allow 22/tcp   # SSH
sudo ufw allow 80/tcp   # HTTP (for cert renewal)
sudo ufw allow 443/tcp  # HTTPS
sudo ufw allow 8883/tcp # MQTT over TLS
sudo ufw enable

# 2. Install Docker
curl -fsSL https://get.docker.com | sh
sudo usermod -aG docker $USER

# 3. Clone/upload project
git clone https://github.com/your-username/eyum-cloud.git
cd eyum-cloud

# 4. Set environment variables
cp .env.example .env
# Edit .env: DB_PASSWORD, JWT_SECRET, REDIS_PASSWORD, GRAFANA_PASSWORD

# 5. Generate TLS certs (Let's Encrypt)
docker compose run --rm certbot certonly --webroot \
  -w /var/www/certbot \
  -d yourdomain.com \
  -d mqtt.yourdomain.com

# 6. Launch full stack
docker compose up -d

# 7. Verify
docker compose ps       # All services should show "Up"
docker compose logs api  # Check for connection errors
```

### Data Retention Policy (TimescaleDB)

```sql
-- Compress data older than 7 days (saves ~90% disk space)
SELECT add_compression_policy('telemetry', INTERVAL '7 days');

-- Drop raw data older than 90 days
SELECT add_retention_policy('telemetry', INTERVAL '90 days');
```

---

## 10. Connectivity Modes Summary

| Mode                   | ESP32 Wi-Fi      | Cloud          | Local Dashboard | Behaviour                                                  |
| ---------------------- | ---------------- | -------------- | --------------- | ---------------------------------------------------------- |
| **Full Cloud**         | ✅ Connected     | ✅ Reachable   | ✅ Active       | All features enabled                                       |
| **LAN Only**           | ✅ Connected     | ❌ Unreachable | ✅ Active       | Local works, cloud retries silently                        |
| **Offline / No Wi-Fi** | ❌ Not connected | ❌ N/A         | ❌ N/A          | Autonomous mode runs entirely from NVS settings            |
| **First Boot**         | SoftAP           | ❌ N/A         | Setup portal    | Provision via SoftAP → provision Wi-Fi + cloud credentials |

The device **never becomes a cloud-dependent brick**. Safety algorithms and autonomous logic always run locally.

---

## 11. Recommended Implementation Order

```
Phase 1 — Cloud Backend (Server-Side)           Estimated: 3-5 days
  ├── Set up VPS, Docker, NGINX, Let's Encrypt
  ├── Deploy EMQX + TimescaleDB + Redis + Grafana
  ├── Build Node.js API (auth, device registry, MQTT ingestion)
  └── Test with MQTT client tools (MQTTX)

Phase 2 — Firmware Integration (Eyum Plant Care) Estimated: 1-2 days
  ├── Write CloudClient.h / CloudClient.cpp
  ├── Register device, get credentials
  ├── Add telemetry publish + command subscribe to main.cpp
  └── Test: Verify data appears in TimescaleDB + Grafana

Phase 3 — iYum Water Automation Cloud Upgrade    Estimated: 1-2 days
  ├── Register new project + device on cloud
  ├── Add CloudClient to ESP32 firmware
  └── Test: Remote pump control from cloud dashboard

Phase 4 — Web Dashboard                          Estimated: 3-4 days
  ├── Multi-project device list
  ├── Real-time device shadow display (WebSocket)
  ├── Remote control buttons
  └── Grafana embed for historical charts

Phase 5 — OTA + Alerts                          Estimated: 2-3 days
  ├── Firmware upload + OTA job system
  ├── Alert rule engine
  └── Email/webhook notifications
```

---

## 12. Technology Summary

| Component           | Technology         | Reason                                                     |
| ------------------- | ------------------ | ---------------------------------------------------------- |
| **MQTT Broker**     | EMQX 5.x           | Built-in auth HTTP plugin, ACL, rule engine, cluster-ready |
| **API Server**      | Node.js + Fastify  | Fast async, native MQTT/WS support, minimal overhead       |
| **Time-Series DB**  | TimescaleDB (pg16) | PostgreSQL-compatible, hypertables, compression, retention |
| **Relational DB**   | PostgreSQL 16      | Same instance as Timescale — users, devices, OTA, alerts   |
| **Cache / Pub-Sub** | Redis 7            | Device shadow store, real-time WebSocket fanout            |
| **Dashboards**      | Grafana            | Best-in-class time-series visualization, alert rules       |
| **Reverse Proxy**   | NGINX + Certbot    | TLS termination, Let's Encrypt auto-renewal                |
| **OTA Storage**     | MinIO / Local FS   | Firmware binary storage with SHA-256 verification          |
| **Containers**      | Docker Compose     | Single-command deployment, service isolation               |
| **Edge Protocol**   | MQTT/TLS :8883     | Lightweight, QoS guaranteed, LWT for offline detection     |
| **User Protocol**   | HTTPS/WSS :443     | Secure REST + real-time WebSocket for dashboard            |
