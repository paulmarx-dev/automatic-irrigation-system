## TODO

## Update 2026-04-04 (implemented)

- [x] AUTO pulse/soak desync fix: keep-awake lease renewed every tick during PULSE_ACTIVE and SOAK_WAIT phases; soak no-data retry instead of immediate abort.
- [x] AUTO stop message fix: new `target_reached` phase distinguishes moisture-target-met from pulse-limit-reached in UI.
- [x] Dry/wet avg hold fix: frontend preserves last known dry/wet values when server sends null during soak sensor sleep.
- [x] Stats chart performance: single-pass flat record format + 2KB buffered sendContent reduces load time.
- [x] Control SUSPECT fix during irrigation: `telemetryHeadResendSleepPlansToOnlineNodes` skips control with active irrigation; BUSY sleep-ACK reject no longer marks SUSPECT.
- [x] Manual irrigation UX fixes: timer oscillation removed, startup window shrink fixed, dead code cleanup.

## Update 2026-03-30 (implemented)

- [x] Sleep coordination in production path: HEAD `SLEEP_PLAN` -> UNIT `SLEEP_ACK` -> HEAD `SLEEP_ACK_ACK` with sleep-aware node presence handling.
- [x] CONTROL switched to deep sleep policy with safe boot-time motor pin OFF default.
- [x] Irrigation lease sync corrected: repeated lease refreshes no longer cause noisy re-apply behavior on CONTROL.
- [x] HEAD command probe flow improved: pending start/stop can be closed from state-probe ACK when desired state is already reached.
- [x] HEAD battery observability refresh fixed for deep-sleep nodes with seq restart (`seq=1` each wake).

## Update 2026-03-15 (implemented)

- [x] Dashboard transport model migrated to SSE-first snapshots (`/api/events`) with polling as fallback only.
- [x] Fallback polling auto-disables on SSE recovery; reconnect loop and fallback state are visible in UI.
- [x] Transport indicator added (live/fallback/reconnect/offline) with diagnostics popover.
- [x] Home TIME countdown rendering switched to backend-authoritative values to eliminate oscillation.
- [x] Conflicting local timer mutations in UI removed (single source of truth from snapshots).
- [x] Stale transient fetch errors in Home tab now auto-clear on fresh snapshot after head reboot/reflash.
- [x] Manual pending/ACK presentation improved (pending guards and ACK precedence handling).
- [x] TIME mode stale-cycle protection added: missed window while control is offline is skipped, no delayed catch-up start.
- [x] Canonical sleep coordination rules fixed in one place: `docs/unit-sleep-coordination-spec.md`.

- [x] Project skeleton: common config structs, logging, build flags (roles: HEAD/SENSOR/CONTROL)
- [x] ESP-NOW basics: init, channel, send, receive callbacks, peer management
- [x] Pairing protocol (service messages): beacon/join/offer/confirm/ack
  - [x] Pairing 2.0 MVP: always-open handshake (no buttons, no NVS)
  - [x] Pairing receive router returns handled flag (bool pairingOnRecv)
  - [x] Session and step validation on handshake packets (offer/confirm/ack)
  - [x] Button + LED UX
  - [x] NVS persistence for paired state (Milestone 6: pair_node namespace, ver/paired/headMac)
  - [x] Safe rules: no accidental rebind; factory reset flow
  - [x] Multi-device pairing: head supports pairing/handling multiple nodes (SENSOR + CONTROL)
  - [x] NVS on HEAD: persist paired nodes registry (MAC/role/nodeId/lastSeen)
  - [x] NVS on HEAD: atomic registry save/load (single blob + CRC32)
  - [x] NVS on CONTROL: persist paired head state and restore after reboot
- [x] Base message definitions (telemetry, battery, cmd, cmd_ack), protocol versioning
- [x] Sensor -> head telemetry (happy path) + ack policy + retries
- [x] Power management for sensor: deep sleep cycle, wake -> measure -> transmit -> sleep
  - [ ] Make node presence thresholds configurable for field mode (SUSPECT/OFFLINE), not debug-fixed seconds
  - [x] Tie presence thresholds to real telemetry period after sleep/power management is implemented
- [x] Battery-driven behavior: low battery thresholds, "critical" mode, messaging to head
  - [x] SENSOR critical battery intent/ack flow and final protective deep sleep fallback
  - [x] CONTROL battery telemetry and low-battery lockout flags reach HEAD via shared telemetry path
  - [x] USB/near-zero battery ADC fallback handling prevents false low-battery decisions
- [x] Control unit: duty-cycle listen vs active mode, heartbeat, command execution state machine
- [x] Head logic: command state machine and remaining runtime semantics
  - [x] Command decisions use sensor data
  - [x] Manual/web commands are accepted as decision inputs
  - [x] Time-based schedule decisions are implemented
  - [x] Manual commands override automatic mode
- [ ] Reliability: rejoin after head reboot, stale peer cleanup, registry maintenance
  - [x] Release smoke checklist (HEAD+2 SENSOR): pair -> reboot -> auto-restore -> telemetry ACK=OK
  - [x] Presence smoke checklist: node offline timeout log + online recovery log after return
  - [ ] Registry slot policy: slot0 is currently NOT reserved; when reserving slot0 for CONTROL, add explicit migration/backward-compat flow
- [ ] Optional: time sync, irrigation-phase sampling
- [x] Web server for head: status page, manual control, pairing management
  - [x] JSON API for node presence (`lastSeenMs`, `online`, `nodeId`, `mac`) for UI integration
  - [x] Sensors UI cards with moisture/state/battery/lastSeen/MAC
  - [x] Sensors actions API+UI: rename/unpair (+ confirmation)
  - [x] Sensor rename persistence in NVS (head)
  - [x] Irrigation mode UI for `AUTO` / `TIME` / `MANUAL` / `OFF`
  - [x] Manual irrigation duration input + start/stop countdown UX
  - [x] TIME mode decimal inputs + next-start/active-cycle status UX
  - [x] AUTO mode threshold hints + live moisture-context status UX
  - [x] Irrigation UI: show CONTROL availability / next wake / blocked watering state
- [ ] WiFi upload of data to server on availability
- [ ] LoRa backup communication channel for critical messages (e.g. low battery alert)

## Decision Log
ESPNOW_CHANNEL = 6 (non overlapping channels are 1/6/11)
ACK policy = application-level ACK
Dedup = last seq per nodeId
Rebind policy = only after node factory reset
Pairing multi-head = refuse if >1 head pairingOpen
Head networking mode = SoftAP always (local UI), STA hotspot only for upload (may pause ESPNOW) или “hotspot must match channel”

### Head Web Interface или Reliability:
“Head must not start hotspot sync while irrigation RUNNING.”
“UI shows irrigation state + time remaining.”
“Control executes irrigation cycle autonomously after CMD_START.”
### Control Unit:
“Control completes START cycle without further head communication.”
“On reboot pump defaults OFF and reports interruption when back online.”

### Open Issues:
- [x] Sensor UX on head factory reset resolved: sensor handles NOT_PAIRED ACK, clears local pairing, enters join mode; head opens short candidate rebind window.
- [x] Calibration reset remains sensor-local in MVP (no web reset action).
  - "если юзерам надо будет, а мы как инженеры решили, что не надо" — revisit only when user demand appears.
- [ ] UX polish (deferred): preserve sensor display names across re-pair by MAC mapping.
  - Scope: at least in-memory for current runtime session; optional NVS persistence later.
  - Goal: if same physical sensor re-pairs (same MAC), restore previous custom name automatically.
- [ ] Residual (deferred): add explicit sensor calibration status/result signal for web flow confirmation.
  - Current compromise uses telemetry freshness (`rxPackets`) as completion heuristic.
- [ ] Residual (deferred): add soft informational timeout for `sensor_finishing` UI phase.
  - Goal: avoid indefinite "waiting" banner without escalating to hard error.
- [ ] Residual (deferred): add regression smoke case for calibration lock release.
  - Scenarios: node offline, unpair during active calibration, and page refresh/reconnect.

### Head Web Operating Policy (current):
- AP-first local web console for user operations and debug; router/internet not required for normal work.
- Internet sync is explicitly deferred to a late phase and must be implemented as a short, explicit mode that may pause ESPNOW.
- See: `docs/head-web-operating-model.md`


## ACCEPTANCE CRITERIA

[x] “HEAD waits for serial in dev builds only”

### 0. Project Skeleton

**Done when:**

- [x] Firmware builds for HEAD, SENSOR and CONTROL (PlatformIO envs or build flags)
- [x] Startup log prints:
  - [x] consistent startup block on all roles
  - [x] device role
  - [x] protocol version
  - [x] deviceUID (factory MAC)
  - [x] WiFi channel
  - [x] paired status
- [x] Logging levels exist (INFO/WARN/ERROR)

### 1. ESP-NOW Basics

**Done when:**

- [x] ESP-NOW initializes reliably on fixed channel
- [x] Head and node exchange 100 packets without failure
- [x] Basic API exists:
  - [x] espnowInit()
  - [x] espnowSend()
  - [x] onReceive()
  - [x] onSend()
- [x] Peer can be added safely multiple times
- [x] Communication resumes after head reboot

### 2. Pairing Protocol

**Done when:**

- [x] Pairing 2.0 MVP (always-open, no buttons, no NVS):
  - [x] Head sends periodic BEACON (broadcast)
  - [x] Node sends JOIN_REQ (broadcast)
  - [x] Head replies OFFER (unicast)
  - [x] Node sends CONFIRM (unicast)
  - [x] Head sends ACK (unicast)
  - [x] Both sides store paired state in RAM until reboot
  - [x] Receive routing API returns handled flag for protocol multiplexing
  - [x] Handshake sessionId/nodeId checks reject stale or out-of-step packets

- [x] UX
  - [x] Head short press -> pairing open 120s
  - [x] Head short press again -> pairing closes
  - [x] Head long press -> factory reset
  - [x] Node short press -> join mode 60s
  - [x] Node short press again -> pairing closes
  - [x] Node long press -> factory reset
  - [x] LED patterns implemented:
    - [x] pairing open: double tap repeat (100 ON / 100 OFF / 100 ON / 700 OFF loop)
    - [x] joining:      slow blink repeat (300 ON / 700 OFF loop)
    - [x] success:      1 long blink (1500 ON / 300 OFF once)
    - [x] error:        3 fast blinks (100 ON / 100 OFF ×3, repeat if persistent)
    - [x] factory reset: 6 rapid blinks (80 ON / 80 OFF ×6 once) + success
- [x] Functional
  - [x] Unpaired node pairs in <10 seconds
  - [x] Node stores in NVS:
    - [x] paired flag
    - [x] headMAC
    - [x] nodeId
  - [x] Node reconnects after reboot
  - [x] Head accepts node after reboot
- [x] Safety
  - [x] Paired node does NOT rebind by short press
  - [x] Rebind only after factory reset

- [x] Milestone 5.1: Sensor auto-enters join mode on boot when unpaired (one-shot trigger, existing 60s join window and LED JOINING behavior)
- [x] Milestone 6: Sensor node pairing persistence in NVS (pair_node schema ver=1, paired/headMac load/save/clear)
- [x] Milestone 6.2: NOT_PAIRED rebind flow (head short candidate window + sensor immediate rejoin)
  - [x] Implementation note: `TELEMETRY_ACK status=NOT_PAIRED` triggers sensor local unpair + immediate join
  - [x] Implementation note: head candidate rebind window = 10s, cooldown = 30s, candidate-MAC filter
  - [x] Implementation note: detailed reproducible flow documented in `pairing.md`
- [x] Multi-head safety
  - [x] Node refuses pairing if multiple heads in pairing mode
- [x] Milestone 6.3: Multi-device pairing + persistence beyond SENSOR
  - [x] Head supports multiple paired devices concurrently (at least SENSOR + CONTROL) (code ready)
  - [x] Head stores paired devices registry in NVS and restores on reboot
  - [x] Control stores paired HEAD info in NVS and restores on reboot (code ready)
  - [x] CONTROL hardware validation completed:
    - [x] Pair CONTROL with HEAD, verify join success + NVS save
    - [x] Reboot CONTROL and confirm auto-restore from NVS without manual join
    - [x] Reboot HEAD and confirm CONTROL is accepted from restored head registry
    - [x] Factory reset HEAD and verify CONTROL gets NOT_PAIRED and requires manual pair

### 2.1. Moisture Sensor Calibrarion 

- [x] cal_enter         : 5 fast blinks (80 ON / 80 OFF ×4 once)
- [x] cal_measure_dry   : slow pulse repeat (500 ON / 500 OFF loop)
- [x] cal_prompt_wet    : double tap repeat (400 ON / 400 OFF / 400 ON / 800 OFF loop, timeout 20s)
- [x] cal_measure_wet   : slow pulse repeat (500 ON / 500 OFF loop)
- [x] cal_done          : success
- [x] cal_error         : error once (e.g. when the difference between wet and dry is too low)
- [x] cal_cancel        : error once


### 3. Base Message Protocol

**Done when:**

- [x] All messages contain:
  - [x] protocolVersion
  - [x] messageType
  - [x] seq
  - [x] nodeId (except service messages)
- [ ] Head rejects:
  - [ ] invalid version
  - [ ] invalid size
  - [ ] unknown type
- [ ] Telemetry includes:
  - [x] moisture raw
  - [x] battery voltage
  - [x] status flags
- [x] Commands include:
  - [x] cmdId
- [x] Command ACK includes:
  - [x] cmdId
  - [x] status

### 4. Sensor Telemetry

**Done when:**

- [x] Sensor sends telemetry successfully
- [x] ACK received from head
- [ ] Transmission cycle <500 ms
- [ ] Retries:
  - [x] Retries implemented
  - [ ] Sensor sleeps after failure
- [x] Duplicates:
  - [x] Head ignores duplicate packets
- [x] Failure test:
  - [x] Sensor survives head being offline

### 5. Sensor Power Management

**Done when:**

- [x] Sensor cycle:
  - [x] Wake -> Measure -> Send -> Sleep
- [x] Wake interval configurable
- [ ] Sensor survives 100 sleep cycles
- [ ] No crashes after repeated deep sleep

### 6. Battery Behaviour

**Done when:**

- [x] Thresholds:
  - [x] LOW/NEEDS_REPLACEMENT threshold implemented
  - [x] CRITICAL threshold implemented
- [x] Sensor:
  - [x] Sends critical battery intent to head (`MSG_CRITICAL_SLEEP_INTENT`)
  - [x] Enters protective deep sleep fallback on persistent critical battery
- [x] Control:
  - [x] Pump/Sensor disabled on critical battery
  - [x] Alert sent to head
- [ ] Charging state (also send in Flags)


### 7. Control Unit

**Done when:**

- [ ] Modes:
  - [ ] Idle duty-cycle listen works
  - [ ] Active mode works
- [x] Heartbeat:
  - [x] Status sent periodically
  - [x] Head detects online/offline
- [ ] Commands:
  - [x] START command works
  - [x] STOP command works
  - [ ] ACK messages correct
- [x] Safety:
  - [x] Pump OFF after reboot

### 8. Head Logic

**Done when:**

- [ ] State machine exists:
  - [ ] IDLE
  - [ ] STARTING
  - [ ] RUNNING
  - [ ] STOPPING
- [x] Decision inputs:
  - [x] Sensor data
  - [x] Manual commands
  - [x] Schedule
- [x] Manual commands override automatic mode

### 9. Reliability

**Done when:**

- [x] Recovery:
  - [x] Sensors reconnect after head reboot
  - [x] Control reconnects after head reboot
- [ ] Registry:
  - [ ] lastSeen stored
  - [x] Offline devices detected
- [x] Maintenance:
  - [x] Nodes can be removed

### 10. Optional Features

**Done when:**

- [ ] Time Sync:
  - [ ] Head provides time
  - [ ] Nodes store timestamps
- [ ] Irrigation Sampling:
  - [ ] Faster sampling during irrigation

### 11. Head Web Interface

**Done when:**

- [x] Status page shows:
  - [x] Devices list
  - [x] lastSeen
  - [x] battery
  - [x] moisture
  - [x] irrigation state
- [x] Controls:
  - [x] Start irrigation
  - [x] Stop irrigation
  - [x] Open pairing
  - [x] Close pairing

### 12. Data Upload

**Done when:**

- [ ] Data uploaded when WiFi available
- [ ] Data buffered offline
- [ ] No blocking of main logic

### 13. LoRa Backup

**Done when:**

- [ ] Critical messages supported
- [ ] Fallback works
- [ ] Can disable LoRa without breaking build
