# Next Iteration Handoff (after context reset)

## Goal
Implement **Head Wi-Fi provisioning MVP** (industry-standard AP onboarding flow) **without breaking existing ESP-NOW pairing/telemetry stability**.

This document is the source of truth for the next implementation cycle.

---

## Current Baseline (already working)
- Multi-sensor pairing on HEAD is stable.
- Node/head NVS persistence is in place.
- Head registry uses atomic blob + CRC32.
- Observability MVP is implemented on branch `feature/head-observability-mvp`:
  - `/api/nodes`
  - per-node `lastSeen`, state, RX/ACK counters
- Offline/online transitions are confirmed on hardware.

> Important: keep this behavior unchanged while adding provisioning.

---

## Why this task now
Need a practical first-time setup UX via browser:
1. Device creates AP.
2. User joins AP.
3. User enters Wi-Fi credentials in local page.
4. Device stores creds in NVS.
5. Device connects to STA network.
6. On failure, device returns to AP provisioning.

This should be done with minimal scope and no protocol redesign.

---

## Scope (MVP only)
### In scope
- HEAD-only provisioning flow.
- AP onboarding page/API for SSID+password.
- Save/load credentials in NVS (`wifi_cfg` namespace).
- Controlled STA connect attempt + result reporting.
- Fallback to AP provisioning if STA connect fails.
- Keep `/api/nodes` working in AP mode.

### Out of scope (do not implement now)
- Rich web UI, charts, themes.
- Async server migration.
- Full captive portal/DNS hijack.
- Cloud sync logic.
- Sensor/control firmware changes.

---

## Critical constraints
1. **ESP-NOW must keep stable behavior** (channel-sensitive).
2. Provisioning mode and normal runtime must be explicit states.
3. No extra NVS write storms.
4. Keep RAM/flash overhead modest (current app already ~80% of app partition).

---

## Architecture notes to preserve
- Use **simple synchronous `WebServer`** (already chosen for MVP simplicity).
- No dynamic allocations in hot receive paths.
- Maintain existing telemetry/pairing APIs and semantics.
- Do not mix provisioning refactor with unrelated features.

---

## Proposed state model (HEAD)
- `PROV_AP_ACTIVE`: AP + local web config active.
- `PROV_CONNECTING_STA`: attempting STA connect using saved/submitted creds.
- `RUN_NORMAL`: existing system behavior (pairing + telemetry + observability).
- `PROV_FAILED`: optional transient state; quickly returns to `PROV_AP_ACTIVE`.

Transitions:
- First boot / no creds -> `PROV_AP_ACTIVE`
- Credentials submitted -> `PROV_CONNECTING_STA`
- STA success -> `RUN_NORMAL`
- STA timeout/fail -> `PROV_AP_ACTIVE`

---

## ESPNOW compatibility rules (must-follow)
- Do not run aggressive STA scans during normal ESP-NOW operation.
- Keep AP channel aligned with ESPNOW channel where possible.
- If STA attach requires channel compromises, gate it behind explicit mode and test hardware behavior carefully.

---

## Files likely to touch
- `src/head_unit.cpp` (state orchestration)
- `src/head_observability.cpp/.h` (server endpoints integration)
- new: `src/head_wifi_provisioning.cpp/.h` (credentials + state + connect attempts)
- maybe: `src/common_config.h` (small constants)
- maybe: `todo.md` (mark progress)

---

## Data model (NVS)
Namespace: `wifi_cfg`
- `ver` (u8)
- `ssid` (string)
- `pass` (string)
- `configured` (u8/bool)

Behavior:
- Write only on user submit / explicit reset.
- Avoid rewriting unchanged creds.

---

## API endpoints (MVP)
- `GET /api/nodes` (already exists; keep)
- `GET /api/provisioning/status`
- `POST /api/provisioning/config` (ssid/password)
- optional `POST /api/provisioning/reset`

Minimal response shape example:
```json
{
  "mode":"PROV_AP_ACTIVE",
  "staConnected":false,
  "staIp":"",
  "lastError":"AUTH_FAIL"
}
```

---

## Acceptance criteria
1. With no saved creds, HEAD starts AP provisioning and API responds.
2. Submitting valid creds stores them and transitions to STA connected mode.
3. Invalid password returns to AP provisioning mode with clear status.
4. Existing pairing/telemetry still works as before in normal mode.
5. `/api/nodes` still returns valid JSON.
6. No repeated NVS writes during steady operation.

---

## Hardware test checklist
1. Fresh flash + erased `wifi_cfg`: confirm AP appears.
2. Submit valid home Wi-Fi creds: confirm STA IP obtained.
3. Reboot: confirm creds persist and auto-connect behavior.
4. Submit wrong password: confirm timeout and AP fallback.
5. Pair sensors and verify telemetry/ACK unaffected.
6. Power-cycle stress x5 to ensure deterministic mode transitions.

---

## Known risks
- Wi-Fi mode/channel interaction with ESP-NOW.
- Blocking connect calls causing latency spikes in loop.
- Memory pressure if web payload grows.

Mitigation:
- Keep connect attempts bounded and state-driven.
- Keep endpoints simple and small JSON.
- Avoid feature creep in this iteration.

---

## Execution order (recommended)
1. Add `wifi_cfg` NVS helpers.
2. Add provisioning state machine in HEAD.
3. Add minimal provisioning endpoints.
4. Integrate with existing server lifecycle.
5. Build all envs.
6. Hardware tests from checklist.
7. Commit and push with short, focused message.
