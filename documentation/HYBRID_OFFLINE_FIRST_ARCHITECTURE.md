# Eyum Smart Home — International-Grade Hybrid Offline-First Architecture

> **Classification:** Production Design Specification  
> **Version:** 1.0  
> **Status:** Design Phase  
> **Audience:** Engineering, Security, Product

---

## 📋 Table of Contents

1. [The Core Problem](#1-the-core-problem)
2. [Real-World Industry References](#2-real-world-industry-references)
3. [Architecture Overview](#3-architecture-overview)
4. [Problem 1 — Account Identity & Zero Re-Registration](#4-problem-1--account-identity--zero-re-registration)
5. [Problem 2 — Automatic Network Failover (Mobile App)](#5-problem-2--automatic-network-failover-mobile-app)
6. [Problem 3 — Offline Command Queue & State Sync](#6-problem-3--offline-command-queue--state-sync)
7. [Security Hardening Specification](#7-security-hardening-specification)
8. [Implementation Roadmap](#8-implementation-roadmap)
9. [Current Code Gap Analysis](#9-current-code-gap-analysis)

---

## 1. The Core Problem

Your system has **three deeply linked problems** that must be solved together:

```
PROBLEM TRIANGLE
                     ┌────────────────────────────┐
                     │  1. IDENTITY REDUNDANCY     │
                     │  Local user ≠ Cloud user    │
                     │  → User must register TWICE │
                     └─────────────┬──────────────┘
                                   │
               ┌───────────────────┴───────────────────┐
               │                                       │
   ┌───────────▼───────────┐             ┌─────────────▼──────────┐
   │  2. NO AUTO-FAILOVER  │             │  3. NO OFFLINE QUEUE   │
   │  Mobile stays on      │             │  Commands lost when     │
   │  local even when away │             │  switching modes        │
   └───────────────────────┘             └────────────────────────┘
```

### Your Exact Architecture Today

```
[Phone on Home WiFi] ──Local API──► [Raspberry Pi / PC Server] ──MQTT──► [ESP32]
[Phone Away]         ──Manual──────► [Cloud eyum.adityapech.site]

Problems:
  - User registered locally CANNOT use cloud without re-registering
  - Phone never auto-switches to cloud when away from home WiFi
  - Commands sent while switching modes may be lost
```

---

## 2. Real-World Industry References

These production systems solved the exact same problem. Study them:

| System | What They Do | What We Learn |
|--------|-------------|---------------|
| **Home Assistant + Nabu Casa** | Local HA is primary. Nabu Casa is a TCP tunnel relay — NOT a copy of your system. Cloud account links to local instance via a one-time `auth_token`. | SNI tunnel architecture, outbound-only connections, E2EE relay |
| **Philips Hue** | Bridge has a local API. Remote access via Hue Cloud token. Same account, linked via bridge serial number + claim token during onboarding | Gateway serial → cloud account binding |
| **Tesla** | Car operates offline fully. When connected, syncs state delta to cloud. Mobile app hits cloud, cloud tunnels commands to car via persistent outbound connection | Bidirectional state delta sync |
| **AWS IoT Greengrass** | Edge device runs Lambda functions locally. Syncs device shadow (desired/reported state) to AWS IoT Core when connected | Device shadow / desired-reported pattern |
| **Tuya Smart (OEM)** | Local LAN API on same WiFi, cloud control when away. Zero re-registration — device is claimed once via QR → cloud account binding | QR-code pairing claim token flow |

### Key Pattern: "Claim Once, Use Everywhere"

Every production system uses **one-time gateway registration** that binds hardware to a cloud account. After that, the cloud account IS the local account — no separate credentials.

---

## 3. Architecture Overview

### The Target Architecture (What We Build Toward)

```
╔══════════════════════════════════════════════════════════════════════╗
║                      CLOUD CONTROL PLANE                            ║
║  [eyum.adityapech.site — Django + Channels + Redis]                 ║
║                                                                      ║
║  ┌─────────────────┐  ┌────────────────┐  ┌──────────────────────┐ ║
║  │  Identity       │  │  Gateway       │  │  Command Queue /     │ ║
║  │  Service (JWT)  │  │  Registry      │  │  Device Shadow Store │ ║
║  │                 │  │  (UUID binding)│  │                      │ ║
║  └─────────────────┘  └────────────────┘  └──────────────────────┘ ║
║           ▲                    ▲                      ▲             ║
╚═══════════╪════════════════════╪══════════════════════╪═════════════╝
            │ HTTPS/WSS          │ Claim Token          │ State Sync
            │ (outbound from     │ (one-time setup)     │ (delta push)
            │  gateway)          │                      │
╔═══════════╪════════════════════╪══════════════════════╪═════════════╗
║           │         LOCAL EDGE GATEWAY                │             ║
║  [Raspberry Pi / PC — Django local server]            │             ║
║                                                                      ║
║  ┌─────────────────┐  ┌────────────────┐  ┌──────────────────────┐ ║
║  │  Local Auth     │  │  Sync Daemon   │  │  Offline Command     │ ║
║  │  (JWT cached    │  │  (background   │  │  Queue (SQLite WAL)  │ ║
║  │   from cloud)   │  │   heartbeat)   │  │                      │ ║
║  └─────────────────┘  └────────────────┘  └──────────────────────┘ ║
║           ▲                                           ▲             ║
╚═══════════╪═══════════════════════════════════════════╪═════════════╝
            │ MQTT / WebSocket                          │ REST
            │                                           │
   ┌────────┴───────┐                        ┌──────────┴──────────┐
   │  ESP32 Devices │                        │  React Native App   │
   │  (MQTT + Local │                        │  (SmartClient with  │
   │   WebSocket)   │                        │   Auto-Failover)    │
   └────────────────┘                        └─────────────────────┘
```

### Network Mode Decision Tree (Mobile App)

```
App Starts / Network Changes
           │
           ▼
    Is WiFi connected?
     NO ──────────────────────────────────► [CLOUD MODE] if token valid
     YES
           │
           ▼
    Can I reach local server? (HEAD /health/ — 1.5s timeout)
     YES ─────────────────────────────────► [LOCAL MODE] ← PRIORITY
     NO
           │
           ▼
    Am I on WiFi but local unreachable?
    (Away from home network)
           │
           ▼
    Do I have a valid cloud token?
     YES ─────────────────────────────────► [CLOUD RELAY MODE]
     NO ──────────────────────────────────► [OFFLINE MODE]
           │
           ▼
    Background retry every 30s
    → Re-evaluate mode automatically
```

---

## 4. Problem 1 — Account Identity & Zero Re-Registration

### The Problem Precisely

```
TODAY:
  Local Server DB:  user { id:1, username:"aditya", password:hash }
  Cloud Server DB:  user { id:99, email:"aditya@gmail.com", password:hash }

  → These are COMPLETELY SEPARATE identities.
  → No link exists between them.
  → User must remember TWO sets of credentials.
  → If cloud token expires, user must re-login to BOTH systems.
```

### The Solution: Gateway-Claim Token Pairing (One-Time Setup)

Inspired by: Nabu Casa, Tuya, Hue, AWS IoT Thing registration.

#### Step 1 — Gateway Generates a Claim Token (One-Time)

When the local server is first set up (or when user wants to link cloud):

```python
# On local Django server — new endpoint
# GET /api/auth/cloud-pair-token/
# Requires: local user is logged in

import secrets
import hashlib
from datetime import datetime, timedelta

def generate_pair_token(request):
    raw_token = secrets.token_urlsafe(32)
    token_hash = hashlib.sha256(raw_token.encode()).hexdigest()
    
    # Store hash in local DB (never the raw token)
    GatewayPairToken.objects.create(
        user=request.user,
        token_hash=token_hash,
        expires_at=datetime.utcnow() + timedelta(minutes=15),
        is_used=False
    )
    
    # Return raw token to client ONCE — never stored in plaintext
    return Response({
        "pair_token": raw_token,
        "gateway_id": settings.GATEWAY_UUID,
        "expires_in": 900,  # 15 minutes
    })
```

#### Step 2 — Mobile App Shows "Link to Cloud" Screen

```
┌─────────────────────────────────────┐
│  Link Your Gateway to Cloud         │
│                                     │
│  This connects your local home      │
│  server to your cloud account.      │
│  You only need to do this ONCE.     │
│                                     │
│  Step 1: Log into your Cloud account│
│  Email: [aditya@gmail.com    ]      │
│  Pass:  [•••••••••••••       ]      │
│                                     │
│  Step 2: Click Link Gateway         │
│  ┌─────────────────────────────┐   │
│  │  🔗  Link Gateway to Cloud  │   │
│  └─────────────────────────────┘   │
│                                     │
│  ✅ Only needed once per home       │
└─────────────────────────────────────┘
```

#### Step 3 — Mobile App Performs the Pairing Handshake

```typescript
async function linkGatewayToCloud(cloudEmail: string, cloudPassword: string) {
  // 1. Login to cloud → get cloud JWT token
  const cloudToken = await cloudApi.login(cloudEmail, cloudPassword);
  
  // 2. Request one-time pair token FROM LOCAL SERVER
  //    (must be on same WiFi as local server)
  const pairData = await localApi.get('/api/auth/cloud-pair-token/');
  const { pair_token, gateway_id } = pairData.data;
  
  // 3. Send pair token TO CLOUD SERVER → cloud validates + binds
  await cloudApi.post('/api/gateways/claim/', {
    gateway_id,
    pair_token,
    local_server_url: localServerUrl,
  }, { headers: { Authorization: `Token ${cloudToken}` } });
  
  // 4. Store binding
  await AsyncStorage.setItem('@gateway_cloud_linked', 'true');
  await AsyncStorage.setItem('@cloud_token', cloudToken);
  
  // Done! No re-registration ever needed again.
}
```

#### Step 4 — Cloud Server Validates and Binds

```python
# On cloud Django server — new endpoint
# POST /api/gateways/claim/

class GatewayClaimView(APIView):
    permission_classes = [IsAuthenticated]
    
    def post(self, request):
        gateway_id = request.data['gateway_id']
        pair_token = request.data['pair_token']
        local_url = request.data['local_server_url']
        
        # Verify pair token by calling back to local server
        verify_response = requests.post(
            f"{local_url}/api/auth/verify-pair-token/",
            json={'token': pair_token, 'cloud_user_id': request.user.id},
            timeout=10
        )
        
        if verify_response.status_code != 200:
            return Response({'error': 'Invalid or expired pair token'}, status=400)
        
        local_user_data = verify_response.json()
        
        # Create identity mapping: cloud_user ↔ local_user
        gateway, _ = Gateway.objects.get_or_create(uuid=gateway_id)
        GatewayUserMapping.objects.update_or_create(
            gateway=gateway,
            cloud_user=request.user,
            defaults={
                'local_user_id': local_user_data['user_id'],
                'local_username': local_user_data['username'],
                'local_server_url': local_url,
                'linked_at': timezone.now()
            }
        )
        
        return Response({'status': 'linked', 'gateway_uuid': gateway_id})
```

### The Final Identity Model

```
After One-Time Pairing:
┌───────────────────────────────────────────────────┐
│  GatewayUserMapping (Cloud DB)                    │
│                                                   │
│  cloud_user_id: 99 (aditya@gmail.com)             │
│  gateway_uuid:  "abc-123-def-456"                 │
│  local_user_id: 1  (username: "aditya")           │
│  linked_at:     2024-01-01                        │
│                                                   │
│  ✅ One account. One identity. Zero redundancy.   │
└───────────────────────────────────────────────────┘

Result:
  - Login to cloud → controls local via relay tunnel
  - Login to local → works offline, no cloud needed
  - Cloud credentials NEVER stored on local server
  - Local credentials NEVER sent to cloud
```

---

## 5. Problem 2 — Automatic Network Failover (Mobile App)

### Current State (What's Missing)

```typescript
// networkMode.ts — CURRENT (manual, called once per screen load)
detectNetworkMode()
// ❌ No background monitoring
// ❌ No auto-switch when you leave home WiFi
// ❌ No circuit breaker for failed requests
// ❌ No NetInfo OS-level listener
```

### Solution A: NetworkMonitorService (Background Singleton)

```typescript
// NEW FILE: src/api/NetworkMonitorService.ts

import NetInfo from '@react-native-community/netinfo';
import AsyncStorage from '@react-native-async-storage/async-storage';
import { EventEmitter } from 'events';

export type NetworkEnvironment = 'LOCAL' | 'CLOUD_RELAY' | 'OFFLINE';

class NetworkMonitorService extends EventEmitter {
  private static instance: NetworkMonitorService;
  private environment: NetworkEnvironment = 'OFFLINE';
  private localServerUrl: string | null = null;
  private probeInterval: ReturnType<typeof setInterval> | null = null;
  private isProbing = false;

  static getInstance() {
    if (!this.instance) this.instance = new NetworkMonitorService();
    return this.instance;
  }

  start(localServerUrl: string) {
    this.localServerUrl = localServerUrl;

    // OS-level network change listener
    NetInfo.addEventListener(async (state) => {
      if (!state.isConnected) {
        this.setEnv('OFFLINE');
      } else {
        await this.probe(); // Re-probe immediately on change
      }
    });

    // Active probe every 30 seconds
    this.probeInterval = setInterval(() => this.probe(), 30_000);
    this.probe(); // Immediate on start
  }

  async probe() {
    if (this.isProbing) return;
    this.isProbing = true;
    try {
      // 1. Try local first (1.5s timeout)
      const localOk = await this.ping(
        `${this.localServerUrl}/api/health/`, 1500
      );
      if (localOk) { this.setEnv('LOCAL'); return; }

      // 2. Try cloud (5s timeout, only if token exists)
      const token = await AsyncStorage.getItem('@cloud_token');
      if (token) {
        const cloudOk = await this.ping(
          'https://eyum.adityapech.site/api/health/', 5000
        );
        if (cloudOk) { this.setEnv('CLOUD_RELAY'); return; }
      }

      this.setEnv('OFFLINE');
    } finally {
      this.isProbing = false;
    }
  }

  private async ping(url: string, timeoutMs: number): Promise<boolean> {
    try {
      const ctrl = new AbortController();
      const t = setTimeout(() => ctrl.abort(), timeoutMs);
      const res = await fetch(url, { method: 'HEAD', signal: ctrl.signal });
      clearTimeout(t);
      // 200 = up, 401 = up but needs auth (server is responding)
      return res.ok || res.status === 401;
    } catch { return false; }
  }

  private setEnv(env: NetworkEnvironment) {
    const prev = this.environment;
    this.environment = env;
    if (prev !== env) {
      console.log(`🌐 Network: ${prev} → ${env}`);
      this.emit('modeChanged', env, prev);
    }
  }

  getEnvironment() { return this.environment; }
  stop() {
    if (this.probeInterval) clearInterval(this.probeInterval);
  }
}

export const networkMonitor = NetworkMonitorService.getInstance();
```

### Solution B: Circuit Breaker

```typescript
// NEW FILE: src/api/CircuitBreaker.ts

enum State { CLOSED, OPEN, HALF_OPEN }

export class CircuitBreaker {
  private state = State.CLOSED;
  private failures = 0;
  private nextRetry = 0;

  constructor(
    private readonly threshold = 3,
    private readonly timeout = 15_000
  ) {}

  async execute<T>(action: () => Promise<T>, fallback: () => Promise<T>): Promise<T> {
    if (this.state === State.OPEN) {
      if (Date.now() < this.nextRetry) return fallback(); // Fail fast
      this.state = State.HALF_OPEN;
    }
    try {
      const result = await action();
      this.failures = 0;
      this.state = State.CLOSED;
      return result;
    } catch {
      this.failures++;
      if (this.failures >= this.threshold || this.state === State.HALF_OPEN) {
        this.state = State.OPEN;
        this.nextRetry = Date.now() + this.timeout;
      }
      return fallback();
    }
  }
}
```

### Updated SmartClient Flow

```
Request comes in
      │
      ▼
networkMonitor.getEnvironment()
      │
      ├─ LOCAL ──────► localBreaker.execute(localApi.call, → queueOffline)
      │
      ├─ CLOUD_RELAY ► cloudBreaker.execute(cloudApi.call, → queueOffline)
      │
      └─ OFFLINE ────► queueOffline(command) + return optimistic response
```

---

## 6. Problem 3 — Offline Command Queue & State Sync

### Device Shadow Pattern (Industry Standard)

```json
{
  "state": {
    "desired":  { "pump": true, "auto": false, "threshold": 40 },
    "reported": { "pump": false, "auto": true,  "threshold": 45 },
    "delta":    { "pump": true, "threshold": 40 }
  },
  "version": 42
}
```

**Delta** = what hasn't been applied yet. Device applies delta on reconnect.

### Offline Queue Implementation

```typescript
// NEW FILE: src/api/OfflineQueue.ts

import AsyncStorage from '@react-native-async-storage/async-storage';

interface QueuedCommand {
  id: string;
  entityId: number;
  command: string;
  value: any;
  timestamp: number;
  retryCount: number;
}

const KEY = '@offline_command_queue';

class OfflineQueueManager {
  async enqueue(cmd: Omit<QueuedCommand, 'id' | 'retryCount'>) {
    const queue = await this.load();
    queue.push({ ...cmd, id: crypto.randomUUID(), retryCount: 0 });
    await AsyncStorage.setItem(KEY, JSON.stringify(queue));
  }

  async replayAll(executor: (cmd: QueuedCommand) => Promise<void>) {
    const queue = await this.load();
    if (!queue.length) return;
    
    console.log(`🔄 Replaying ${queue.length} offline commands...`);
    const failed: QueuedCommand[] = [];

    for (const cmd of queue) {
      try {
        await executor(cmd);
      } catch {
        cmd.retryCount++;
        if (cmd.retryCount < 3) failed.push(cmd);
        else console.warn(`❌ Dropped command after 3 retries: ${cmd.id}`);
      }
    }
    await AsyncStorage.setItem(KEY, JSON.stringify(failed));
  }

  private async load(): Promise<QueuedCommand[]> {
    const raw = await AsyncStorage.getItem(KEY);
    return raw ? JSON.parse(raw) : [];
  }

  async size() { return (await this.load()).length; }
}

export const offlineQueue = new OfflineQueueManager();
```

### Auto-Replay When Network Restores

```typescript
// In App.tsx — wire up once at startup

networkMonitor.on('modeChanged', async (newMode, prevMode) => {
  if (prevMode === 'OFFLINE' && newMode !== 'OFFLINE') {
    // Network restored — replay any queued commands
    await offlineQueue.replayAll(async (cmd) => {
      await smartApi.controlEntity(cmd.entityId, cmd.command, cmd.value);
    });
  }
});
```

---

## 7. Security Hardening Specification

### Security Level Map

```
TRUST LEVELS:
  Level 0 (Highest) — ESP32 Device Secret (PBKDF2 stored, never in plaintext)
  Level 1           — Local Server JWT (24h access / 30d refresh)
  Level 2           — Cloud JWT (1h access / 30d refresh)
  Level 3 (Lowest)  — Mobile App Session (bound to Level 1 or 2)
```

### Required Security Controls

| # | Control | Standard | Implementation |
|---|---------|----------|---------------|
| 1 | **HTTPS everywhere** | TLS 1.3 | Nginx + Let's Encrypt on cloud. Local HTTP on LAN only |
| 2 | **JWT short expiry** | OWASP | Local: 24h + 30d refresh. Cloud: 1h + 30d refresh |
| 3 | **Pair token single-use** | Best practice | SHA256 hash stored. Marked used instantly on redemption |
| 4 | **Pair token TTL** | Best practice | 15-minute hard expiry |
| 5 | **Rate limiting on pairing** | OWASP | Max 5 pair attempts/hour/IP. Exponential backoff |
| 6 | **Gateway UUID non-guessable** | Best practice | UUIDv4, generated once at install, stored in `.env` |
| 7 | **CSRF protection** | Django | `CSRF_TRUSTED_ORIGINS` set ✅ (already fixed) |
| 8 | **Token storage** | OWASP Mobile | AsyncStorage → upgrade to `react-native-keychain` |
| 9 | **Audit log** | ISO 27001 | Log: pair attempts, claims, mode switches, commands |
| 10 | **Secure headers** | OWASP | `X-Frame-Options`, `HSTS`, `X-Content-Type-Options` on cloud |

### Pair Token Security Flow

```
PAIR TOKEN LIFECYCLE (15-minute window, single-use):

  1. User clicks "Link to Cloud" (must be on home WiFi)
  2. Local server: raw_token = secrets.token_urlsafe(32)
  3. Local server stores: DB ← SHA256(raw_token), expires_at, is_used=False
  4. App receives raw_token (HTTPS only — never in logs)
  5. App sends raw_token → Cloud server
  6. Cloud server → calls local server: "Is SHA256(token) valid & unused?"
  7. Local server: hash matches + not used + not expired → mark is_used=True
  8. Cloud creates GatewayUserMapping. Token permanently invalidated.

ATTACKS MITIGATED:
  ✅ Token interception: Single-use, 15-min TTL. Useless after first use.
  ✅ Replay attack: is_used=True blocks all re-use instantly.
  ✅ Brute force: Rate limited to 5/hour. UUIDv32 = 256 bits of entropy.
  ✅ Man-in-middle: HTTPS required end-to-end.
```

### Production Upgrade Path

| Priority | Upgrade | Library |
|----------|---------|---------|
| HIGH | Secure token storage | `react-native-keychain` (iOS Keychain / Android Keystore) |
| HIGH | `GATEWAY_UUID` in `.env` | `python-decouple` or `django-environ` |
| MEDIUM | Certificate pinning | `react-native-ssl-pinning` |
| MEDIUM | Biometric lock for cloud access | `react-native-biometrics` |
| LOW | mTLS for local API | Custom cert provisioning |

---

## 8. Implementation Roadmap

### Phase 1 — Foundation (Week 1–2) ← START HERE

**High Priority | Low-Medium Effort | Unblocks everything**

- [ ] Add `/api/health/` endpoint to local server (returns `{"status":"ok"}`)
- [ ] Add `GATEWAY_UUID` to local server `.env` / `settings.py`
- [ ] Add `GatewayPairToken` model to local server
- [ ] Add `/api/auth/cloud-pair-token/` endpoint (local server)
- [ ] Add `/api/auth/verify-pair-token/` endpoint (local server)
- [ ] Add `GatewayUserMapping` model to cloud server
- [ ] Add `/api/gateways/claim/` endpoint (cloud server)

### Phase 2 — Mobile Auto-Failover (Week 2–3)

**High Priority | Medium Effort | Core UX Feature**

- [ ] Create `NetworkMonitorService.ts` (background probing singleton)
- [ ] Create `CircuitBreaker.ts`
- [ ] Create `OfflineQueueManager.ts`
- [ ] Update `smartClient.ts` to use `NetworkMonitorService`
- [ ] Create `networkStore.ts` (Zustand store for network state)
- [ ] Add offline command auto-replay on mode restoration (in `App.tsx`)
- [ ] Add network status indicator in mobile app header

### Phase 3 — Gateway Pairing UI (Week 3–4)

**High Priority | Medium Effort | Eliminates Redundancy**

- [ ] Build `GatewayLinkScreen.tsx` in mobile app
- [ ] Implement `linkGatewayToCloud()` flow in mobile
- [ ] Store `@gateway_cloud_linked` flag in AsyncStorage
- [ ] Show "Cloud Linked ✅" in Profile/Settings screen
- [ ] Add "Unlink Gateway" with confirmation dialog

### Phase 4 — Security Hardening (Week 4–5)

**Medium Priority | Low Effort | Production Readiness**

- [ ] Migrate token storage from AsyncStorage → `react-native-keychain`
- [ ] Add rate limiting middleware on pair token endpoints
- [ ] Add audit log table (cloud DB) — log all claim attempts
- [ ] Add HSTS + security headers to Nginx config
- [ ] Set `is_used=True` atomically with DB transaction

### Phase 5 — Device Shadow & State Sync (Week 5–6)

**Medium Priority | High Effort | Full Production Grade**

- [ ] Add `DeviceShadow` model to cloud DB (desired/reported/delta)
- [ ] Local server pushes state delta to cloud on entity changes
- [ ] Cloud stores desired state for offline-queued commands
- [ ] ESP32 pulls pending delta from cloud on reconnect

---

## 9. Current Code Gap Analysis

### What Exists Today ✅

```
✅ networkMode.ts         — manual mode detection
✅ smartClient.ts         — local vs cloud routing
✅ cloudClient.ts         — cloud API wrapper
✅ Cloud CSRF fixed        — eyum.adityapech.site trusted ✅
✅ JWT token in AsyncStorage (@cloud_token)
✅ Gateway UUID in cloud DB
✅ SwitchCard/LightCard state normalization (isEntityOn) ✅
✅ PlantCareScreen cloud sign-in UI
```

### What's Missing ❌

```
❌ Background NetworkMonitorService (NetInfo + active probing)
❌ Circuit breaker (CloudBreaker / LocalBreaker)
❌ Automatic mode-change events (networkMonitor.on('modeChanged'))
❌ Offline command queue (persistent AsyncStorage queue)
❌ Offline command auto-replay on network restoration
❌ Gateway-to-cloud pair token endpoints (both servers)
❌ GatewayUserMapping model (cloud DB)
❌ GatewayPairToken model (local DB)
❌ GATEWAY_UUID in local server config
❌ /api/health/ endpoint (local server)
❌ Gateway pairing UI screen (mobile)
❌ Network status indicator in app header
```

### Implementation File Map

```
Mobile App (esp32-flasher/mobile/src/)
  api/
    NetworkMonitorService.ts    ← NEW (Phase 2)
    CircuitBreaker.ts           ← NEW (Phase 2)
    OfflineQueueManager.ts      ← NEW (Phase 2)
    smartClient.ts              ← MODIFY (Phase 2)
  screens/
    GatewayLinkScreen.tsx       ← NEW (Phase 3)
  store/
    networkStore.ts             ← NEW (Phase 2)

Local Server
  api/views/auth_views.py       ← NEW endpoints (Phase 1)
  api/models/gateway.py         ← NEW GatewayPairToken model (Phase 1)
  smarthome/settings.py         ← ADD GATEWAY_UUID (Phase 1)

Cloud Server (esp32-flasher/cloud/)
  api/views/gateway_views.py    ← NEW GatewayClaimView (Phase 1)
  api/models/gateway.py         ← NEW GatewayUserMapping (Phase 1)
```

---

## Summary

```
BEFORE (Today):
  👤 Register local  ─┐
  👤 Register cloud  ─┘  Two separate accounts, manual switching
  📱 User must manually decide "am I home or away?"
  💀 Commands may be lost when switching networks

AFTER (This Architecture):
  👤 Register local ONCE
  🔗 Link to cloud ONCE via 15-min claim token (on home WiFi)
  📱 App auto-detects: home WiFi → LOCAL  |  away → CLOUD  |  no net → OFFLINE
  📬 Commands queued offline → auto-replayed when network restores
  🔒 Security: single-use tokens, short TTL, rate limiting, audit logs

PRODUCTION EQUIVALENTS:
  ✅ Identity pattern   → Philips Hue, Tuya, Nabu Casa
  ✅ Failover pattern   → Tesla, Ring, Nest
  ✅ Queue pattern      → AWS IoT Greengrass, Azure IoT Hub
  ✅ Security standard  → OWASP IoT Top 10, NIST SP 800-213
```

---

*Sources: Home Assistant / Nabu Casa architecture, AWS IoT Greengrass docs, OWASP IoT Top 10 (2023), RFC 8693 OAuth 2.0 Token Exchange, NIST SP 800-213 IoT Device Cybersecurity Guidance, Tuya / Philips Hue / Tesla architecture analysis.*
