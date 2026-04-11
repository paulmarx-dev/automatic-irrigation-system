# Head Web UI Spec (AP-first, field-ready)

Status: Draft (implementation plan)
Owner: HEAD firmware/web

Implementation update (2026-03-15):
- Dashboard now uses SSE-first snapshot stream (`/api/events`) with automatic polling fallback.
- Transport state indicator/popover is implemented in UI.
- Home TIME countdown/status uses backend-authoritative snapshot values (no local ticker drift).
- Home transient fetch errors auto-clear when fresh snapshots return after reboot/reflash.

Implementation update (2026-03-30):
- Control battery values in cards now refresh correctly across deep-sleep wake cycles (`seq` restart handling fixed on HEAD).
- Irrigation lease sync and command probe handling were stabilized for control wake/sleep operation.

Implementation update (2026-04-04):
- Stats tab MVP implemented with chart endpoint, per-node visibility toggles, and period selector.
- Stats chart API optimized: single-pass flat record format + 2KB buffered sendContent.
- Manual irrigation UX stabilized: timer oscillation removed, countdown uses backend-authoritative values only.
- AUTO pulse/soak cycle stabilized: keep-awake lease renewal, target_reached phase, dry/wet avg hold.

## 1) Product concept

Head provides a **local web console** over its own AP for:
- operator monitoring,
- operator control,
- field maintenance,
- engineering diagnostics.

Core principle:
- irrigation runtime + ESP-NOW reliability are primary,
- web UX is secondary and must not degrade primary behavior.

Reference policy: `docs/head-web-operating-model.md`.

---

## 2) Scope and boundaries

### In scope now
- AP-first local console for users and debug.
- Tabs: Home, Sensors, This Unit.
- Pairing management from web.
- Basic control modes: Auto / Time / Manual / OFF.
- Basic settings persistence in NVS.
- Status updates with low radio overhead.
  - Implemented as SSE snapshot stream with fallback polling.

### Deferred explicitly (do not implement in this phase)
- Internet/cloud sync.
- Router-dependent always-on STA web hosting.
- Full long-term analytics backend.

---

## 3) UX model

## 3.1 Access / login
- AP SSID default: `Irrigation-Setup-XXXX`.
- AP password exists and is user-changeable in This Unit settings.
- No extra in-page login in MVP (AP auth is the gate).

## 3.2 Navigation (MVP)
- Home
- Sensors
- This Unit
- Stats tab is phase 2 (history/graphs)

---

## 4) Functional spec by tab

## 4.1 Home
Show:
- Sensors online: XX
- Current soil moisture avg: XX% (or N/A)
- Last watering: duration since last run
- If running: running time + estimated time left

Control:
- Mode switch: Auto | Time | OFF | Manual

Auto:
  - Start watering below: YY%
  - Stop watering above: XX%
  - Context hint with current average moisture

Time:
  - Water for N minutes every M hours
  - Decimal inputs with `.` and `,`
  - Human-readable interval/duration hints
  - Next-start / active-cycle countdown status
- Save button state:
  - `Save` (blue) when dirty
  - `Saved` (green) when persisted
- Reset button restores current persisted values

Manual:
- Start watering for N minutes
- Running status + Stop

OFF:
- Informational text only, no start actions

## 4.2 Sensors
Show:
- Average soil moisture
- Next expected sensor update (based on current policy)
- Sensor cards list:
  - Name
  - Moisture
  - State (ONLINE/SUSPECT/OFFLINE)
  - Battery: X.XXV (OK / CRITICAL / NEEDS_REPLACEMENT)
  - Last seen
  - Rename
  - Unpair (with confirmation)

Sensor card visual priority:
- `CRITICAL` battery: show battery status label in red (danger text/badge).
- `NEEDS_REPLACEMENT` battery: render the entire sensor card in danger style (red-tinted card with clear emphasis).
- Use semantic theme danger tokens (no ad-hoc colors).

Actions:
- Add new -> opens pairing window (120s)
- Banner while pairing window is open
- Success banner on pair complete
- Auto refresh list after pair

## 4.3 This Unit
Show/edit:
- Unit name (default from SSID)
- AP password
- Firmware version
- Uptime

Danger zone:
- Unpair all sensors (confirm)
- Factory reset (confirm)

---

## 5) Technical constraints

- AP channel fixed to ESPNOW channel.
- AP can remain visible always (field usability), but web traffic must be throttled.
- Limit active clients in MVP (target: 1, max: 2).
- Default status refresh interval: 2-5 seconds.
- Heavy operations are explicit and confirmed.
- Any web overload must not block irrigation runtime loop.
- Startup ordering must restore pairing/runtime state before emitting NOT_PAIRED decisions to web-visible presence paths.

---

## 6) Data/API surface (MVP)

Existing:
- `GET /api/nodes`
- `GET /api/provisioning/status`
- `POST /api/provisioning/config`
- `POST /api/provisioning/reset`

New/extended for Web UI MVP:
- `GET /api/system/summary`
- `GET /api/irrigation/config`
- `POST /api/irrigation/config`
- `POST /api/irrigation/manual/start`
- `POST /api/irrigation/manual/stop`
- `POST /api/pairing/open`
- `POST /api/pairing/close`
- `POST /api/sensors/{id}/rename`
- `POST /api/sensors/{id}/unpair`
- `POST /api/unit/rename`
- `POST /api/unit/password`
- `POST /api/unit/unpair-all`
- `POST /api/unit/factory-reset`

Note:
- endpoint names can be adjusted during implementation,
- payload/response schemas must be documented when endpoint is implemented.

---

## 7) Implementation plan with checkboxes

## Phase A — foundation (must-have)
- [x] Define and freeze MVP API contracts (request/response JSON)
- [x] Add head runtime summary endpoint (`/api/system/summary`)
- [x] Add irrigation config read/write endpoints + NVS persistence
- [x] Add manual start/stop endpoints with safety guards
- [x] Add pairing open/close endpoints
- [x] Add sensor rename/unpair endpoints
- [x] Add unit rename/password endpoints
- [ ] Add danger-zone actions with confirmation tokens
  - Status: minimal web factory-reset endpoint is implemented with `confirm=RESET`; tokenized confirmation is still deferred.

## Phase B — UI MVP
- [x] Create tabbed shell (Home / Sensors / This Unit)
- [x] Implement Home data binding + mode controls
- [x] Implement Save/Saved dirty-state UX
- [x] Implement Sensors cards + add-new + banners
- [x] Implement battery status rendering in sensor cards (`OK` / `CRITICAL` / `NEEDS_REPLACEMENT`)
- [ ] Implement This Unit form + danger zone flows
  - Status: This Unit minimal is implemented, including a minimal danger-zone factory reset flow.
- [x] Implement low-frequency auto-refresh (2-5s)

## Phase C — reliability hardening
- [ ] Add API rate limits / client limit
- [ ] Add defensive timeouts for handlers
- [ ] Add non-blocking safeguards in runtime loop paths
- [ ] Add serial diagnostics for web load impact
- [ ] Validate no ESP-NOW regressions under active web usage

## Phase D — stats (phase 2)
- [x] Design moisture history ring buffer
- [x] Add irrigation event markers ON/OFF
- [x] Add `GET /api/stats/chart` endpoint (flat record format with 2KB buffered output)
- [x] Implement Stats tab graph with filters per sensor + avg + period selector

---

## 8) Acceptance checklist (MVP done criteria)

- [x] User can connect to AP and open UI without button interaction.
- [x] Home tab correctly shows online count and avg moisture.
- [x] Auto/Time/Manual/OFF controls behave as expected and persist.
  - Includes manual start/stop controls available only in `MANUAL` mode.
  - Includes TIME mode decimal inputs, human-readable hints, and countdown status.
- [x] Sensor add/rename/unpair works via UI.
- [x] Each sensor card shows battery voltage + battery state.
- [x] Battery severity visuals are correct: `CRITICAL` = red label; `NEEDS_REPLACEMENT` = entire card danger style.
- [x] This Unit rename/password works and persists.
- [x] Under one active web client, ESP-NOW telemetry remains stable.
- [ ] Under stress (frequent refresh), system remains safe (no runtime lockups).
- [x] After reboot/reset, startup ordering prevents false NOT_PAIRED/offline artifacts before pairing registry restore completes.
- [x] Implementation stays within sections 2/6/7 of this spec (no internet sync, no router-dependent UX, no extra tabs/features in MVP).

---

## 9) Open decisions

- [ ] Final AP password policy (default value + first-change flow)
- [ ] Exact max clients allowed in AP mode
- [ ] Polling-only in MVP or SSE for read-only status stream
- [x] Polling-only in MVP or SSE for read-only status stream
  - Resolved: SSE snapshot stream is primary; polling remains fallback only.
- [ ] Final wording and localization (RU/EN) for user-facing text
- [x] Calibration reset from web UI in MVP
  - Decision: do not add reset-calibration action to web UI in MVP; keep it sensor-local only.
  - Note: "если юзерам надо будет, а мы как инженеры решили, что не надо" -> revisit only after real user demand.

---

## 10) Out-of-scope reminder

- Internet sync is intentionally postponed and must not be introduced in this phase.
- Scope guard: do not add APIs/features outside MVP contracts in section 6 unless this spec is explicitly revised first.
