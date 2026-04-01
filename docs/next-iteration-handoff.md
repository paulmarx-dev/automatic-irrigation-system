# Next Iteration Handoff (after context reset)

## Archive update (2026-04-01)

Auto-watering Phase 3 stabilization is complete on branch `feature/auto-watering-ui-first`.

Closed outcomes:
- Keep-awake orchestration is active around AUTO decision windows.
- Pulse checkpoint stop logic is quorum-aware and uses defer/retry under low sample count.
- Fallback path remains explicit degraded behavior with throttled logs.
- CompletedByLimit post-limit wait gating prevents immediate stale-path restart.

Validation status:
- Full hardware monitor runs confirm deterministic flow: `Idle -> PulseActive -> SoakWait -> CompletedByLimit`.
- No false early stop from single-sensor checkpoint samples.
- Build smoke test passed for both environments via `PIO Build All`.

Key stabilization commits:
- `39057d2` Implement Phase 3 keep-awake sleep-plan override (head-side).
- `f0c97a2` Window keep-awake near checkpoints and preserve sleep slot staggering.
- `2d0fee0` Prime keep-awake immediately after pulse start.
- `260f061` Tune keep-awake windows for continuous pulse checkpoint coverage.
- `3451f55` Harden pulse checkpoint fallback and throttle fallback logs.
- `72db90c` Stabilize pulse checkpoint retries and stop quorum.
- `413d569` Fix post-limit wait gating in auto idle.
- `4db9227` Throttle repeated pulse checkpoint defer logs.
- `9421b0c` Finalize Phase 3 keep-awake validation in spec.

Next recommended focus (post-closeout):
- Stats view MVP endpoint + minimal tab wiring.
- Optional anti-noise guards only if needed after longer field soak tests.

## Archive update (2026-03-15)

This handoff is largely completed in implementation:
- Head web provisioning MVP is active.
- AP-first UI flow is active with SSE-first status updates and polling fallback.
- Irrigation Home status UX has been stabilized (authoritative countdown source, stale error recovery).

Keep this file as historical handoff context; use `docs/todo.md` and `docs/next-steps-todo.md` as active planning sources.

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

Operator policy decisions (confirmed):
- Provisioning AP name: `Irrigation-Setup-XXXX` (last 4 HEX from HEAD MAC).
- Provisioning AP should be temporary and user-triggered by the same button used for sensor pairing.
- AP open window: 2 minutes by default (press button again to reopen).
- If provisioning was started but not completed, close session after 5-10 minutes.
- Main irrigation/sensor function always has higher priority than web/provisioning activity.

---

## Scope (MVP only)
### In scope
- HEAD-only provisioning flow.
- AP onboarding page/API for SSID+password.
- Static provisioning UI in filesystem files under `data/` (no inline HTML in C++ handlers).
- Save/load credentials in NVS (`wifi_cfg` namespace).
- Controlled STA connect attempt + result reporting.
- Fallback to AP provisioning if STA connect fails.
- Keep `/api/nodes` working in AP mode.
- Wi-Fi credential reset by button pattern only during active pairing/provisioning window (triple press).

### Out of scope (do not implement now)
- Rich web UI, charts, themes.
- Async server migration.
- Full captive portal/DNS hijack.
- Cloud sync logic.
- Sensor/control firmware changes.
- Do NOT wipe Wi-Fi credentials in the existing long-press sensor reset flow.

---

## Critical constraints
1. **ESP-NOW must keep stable behavior** (channel-sensitive).
2. Provisioning mode and normal runtime must be explicit states.
3. No extra NVS write storms.
4. Keep RAM/flash overhead modest (current app already ~80% of app partition).
5. Wi-Fi credentials must not be erased when user performs sensor reset flows.

---

## Architecture notes to preserve
- Use simple synchronous `WebServer` in this iteration.
- Keep a clear seam so migration to async server later is low-risk (same endpoint contracts, isolated provisioning module).
- Pin filesystem choice explicitly in config (avoid relying on implicit defaults).
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
- Pairing button press in normal mode -> open temporary provisioning AP (2 min window)
- Credentials submitted -> `PROV_CONNECTING_STA`
- STA success -> `RUN_NORMAL`
- STA timeout/fail -> back to `PROV_AP_ACTIVE` while provisioning window remains active
- Provisioning inactivity timeout (5-10 min) -> close AP and return to `RUN_NORMAL`

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
- new: `data/provisioning/index.html`
- new: `data/provisioning/app.js`
- new: `data/provisioning/style.css`
- `platformio.ini` (filesystem type and upload flow)
- maybe: `src/common_config.h` (small constants)
- maybe: `docs/todo.md` (mark progress)

Note: keep provisioning logic in dedicated module files; avoid spreading state transitions across unrelated units.

---

## Filesystem + static delivery rules
- Serve provisioning UI from filesystem files (`/provisioning/index.html`, etc.), not C++ string literals.
- Keep API routes and static route handling separate.
- Add a repeatable upload step for FS image (`uploadfs`) for head environment.
- During dev and release validation, run both firmware upload and FS upload when UI changes.

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
- Explicit reset path: triple button press during active pairing/provisioning window only.

---

## API endpoints (MVP)
- `GET /api/nodes` (already exists; keep)
- `GET /api/provisioning/status`
- `POST /api/provisioning/config` (ssid/password)
- optional `POST /api/provisioning/reset` (debug only; button flow is primary UX)

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
7. Long-press sensor reset does not clear Wi-Fi credentials.
8. Triple-press reset during pairing/provisioning clears only Wi-Fi credentials.

---

## Hardware test checklist
1. Fresh flash + erased `wifi_cfg`: confirm AP appears.
2. Submit valid home Wi-Fi creds: confirm STA IP obtained.
3. Reboot: confirm creds persist and auto-connect behavior.
4. Submit wrong password: confirm timeout and AP fallback.
5. Pair sensors and verify telemetry/ACK unaffected.
6. Power-cycle stress x5 to ensure deterministic mode transitions.
7. Verify long-press sensor reset keeps Wi-Fi credentials intact.
8. Verify triple press during pairing/provisioning clears Wi-Fi credentials and reopens setup AP.

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
2. Add provisioning state machine in HEAD (button-triggered temporary AP windows).
3. Add triple-press Wi-Fi credential reset path scoped to pairing/provisioning window.
4. Add static provisioning UI files under `data/provisioning`.
5. Configure filesystem explicitly in `platformio.ini` and ensure `uploadfs` flow exists for head.
6. Add minimal provisioning endpoints.
7. Integrate with existing server lifecycle.
8. Build all envs.
9. Upload firmware + FS image to head for UI validation.
10. Hardware tests from checklist.
11. Commit and push with short, focused message.
