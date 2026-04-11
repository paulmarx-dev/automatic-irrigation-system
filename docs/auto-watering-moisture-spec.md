# Auto Watering By Moisture - Spec (v1)

Status: In progress. Phase 1 (UI + config persistence) is complete. Phase 2/3 runtime logic is stabilized and field-validated on Apr 1, 2026; AUTO decision paths now run on strict fresh telemetry semantics. Phase 4 (Apr 4, 2026): keep-awake lease fix, target_reached phase, dry/wet avg hold, control SUSPECT fix during irrigation.

## Implementation Progress

- Phase 1: Done, test passed.
  - AUTO tab has Advanced settings accordion (collapsed by default).
  - `Dry avg` / `Wet avg` are always visible placeholders.
  - Advanced fields are saved with Activate flow.
  - Backend persists advanced fields in irrigation config (NVS blob) and returns them in config snapshot/API.
- Phase 2: Done, test passed.
  - Pulse/soak state machine execution on head side implemented and validated in monitor runs.
  - Fresh-first decision path with online-snapshot fallback implemented to prevent false abort during sensor sleep windows.
  - Runtime logs de-spammed (one-shot fallback log, throttled idle no-start logs).
  - CompletedByLimit restart guard validated (no immediate same-loop restart on stale snapshot).
- Phase 3: Done, test passed.
  - Keep-awake orchestration contract and scheduling implemented on head runtime path.
  - Pulse checkpoint stop decision hardened with quorum-aware defer/retry behavior.
  - Fallback and defer observability de-spammed (throttled repeated lines per phase/pulse).
  - Post-limit wait gating fixed (no immediate re-entry through regular Idle path).
  - AUTO start path now requires fresh telemetry plus direct CONTROL reachability/lockout gating.
  - Intra-pulse checkpoint now uses strict fresh-only decisioning (no degraded fallback to stale snapshot).

Phase 2 validation summary (hardware monitor):
- Verified state transitions in multiple runs: `Idle -> PulseActive -> SoakWait -> PulseActive`.
- Verified terminal paths: `CompletedByLimit` and return to `Idle` with restart guard behavior.
- Verified no log flooding from fallback/no-start checkpoints.
- Verified builds remained green after each firmware change.

Phase 3 validation summary (hardware monitor):
- Verified keep-awake override toggles around decision windows and returns to regular cadence.
- Verified no false early stop on single-sensor checkpoint sample (`STOP_DEFER_LOW_N` with retry until pulse end or quorum).
- Verified `CompletedByLimit` returns to `Idle` with guarded wait-before-restart behavior.
- Verified fallback remains explicit and bounded (no uncontrolled fallback/defer log spam).

## 1) Goal

Implement automatic watering decisions on the head unit based on moisture telemetry from sensor nodes.

## 2) Decision Owner

Head unit is the single decision maker for auto watering:
- Sensors provide telemetry.
- Control unit executes irrigation commands.

## 3) Evaluation Timing

When sensors wake up (planned near-synchronously), the head opens a short collection window:
- `T_collect = 1s`

During collection and decision calculation, head should keep sensors awake (defer sleep ack / lease behavior), so they do not immediately go back to long sleep before irrigation decision is sent.

Status:
- [x] `T_collect = 1s` locked.
- [x] Exact keep-awake API contract locked (HEAD uses short sleep-plan base override with bounded lease refresh around AUTO checkpoints).

## 4) Candidate Sensor Set

A sensor reading is considered valid for this cycle if all conditions are true:
- Telemetry arrived within `T_collect`.
- Sensor moisture is at least `3%`.

Notes:
- Missing telemetry in this cycle -> sensor is ignored for this decision.
- Moisture `< 3%` is treated as likely out-of-ground outlier and ignored.

Minimum valid sensor count:
- `N_min = 1`

If valid count is below minimum:
- Do not start/continue auto watering.
- If watering is already active, stop and wait for next telemetry cycle.

Status:
- [x] Valid sample rules fully match implementation.
- [x] `N_min = 1` locked.
- [x] Stop on `< N_min` during active watering locked.

Implementation note:
- AUTO start paths (`Idle` and `SoakWait -> next pulse`) now require fresh samples.
- Intra-pulse checkpoint also requires fresh samples; stale snapshot fallback removed.

## 5) Zone Calculation

Let `N` be valid sensor count and `k = ceil(N/2)`.

From valid moisture values sorted ascending:
- `dryZone` = average of `k` lowest values.
- `wetZone` = average of `k` highest values.

Status:
- [x] Zone split formula locked (`k = ceil(N/2)`).

## 6) Start Condition (AUTO mode)

Start watering when all are true:
- `dryZone < startThreshold`
- `wetZone <= stopThreshold`
- No blocking conditions (e.g. control unavailable / battery lockout / mode conflict)

Status:
- [x] Start condition fully matches plan.

Implementation note:
- Implemented moisture predicate is `dryZone < startThreshold && wetZone <= stopThreshold`.
- AUTO start path now directly enforces blocking conditions for CONTROL reachability and CONTROL battery lockout.

## 7) Stop Condition (AUTO mode)

Stop watering when any is true:
- `dryZone >= stopThreshold`
- `wetZone >= stopThreshold + X`

Where:
- `X` = `wetTolerance` — configurable margin in percentage points. Default: `5`.

Status:
- [x] Stop condition logic locked.
- [x] `wetTolerance` default locked (`5`).

## 8) Data Loss During Active Watering

If valid set collapses (no valid decision basis in current cycle):
- Stop watering immediately.
- Re-evaluate on next telemetry cycle.

## 9) Advanced Configuration Parameters

All four parameters are stored in NVS alongside `startThreshold` and `stopThreshold`. Saved via the same Activate flow in the UI.

| Parameter | NVS key | Unit | Default | Description |
|---|---|---|---|---|
| `wetTolerance` | tbd | % | 5 | Wet-zone overshoot margin X in stop condition |
| `pulseIntervalSec` | tbd | seconds | 120 | Duration of a single irrigation pulse |
| `soakDelaySec` | tbd | seconds | 120 | Soak wait time between pulses |
| `maxPulses` | tbd | count | 3 | Maximum irrigation pulses per auto cycle |

Status:
- [x] Defaults locked: `5 / 120 / 120 / 3`.
- [x] Values are persisted together with irrigation config.
- [x] NVS storage strategy locked for this phase (single irrigation config blob fields; standalone human-readable keys deferred by design).

## 10) Debug/Bring-up Notes

Current debug sleep period may be short (about 1 minute) until full implementation is validated.

## 11) UI — Home Tab (Auto mode config)

Layout when AUTO tab is selected:

```
[ Start watering below (%) ] [ Stop watering above (%) ]   ← existing inputs

Start below X%, stop above Y%.                             ← status hint (existing)

Dry avg: --   Wet avg: --                                  ← always visible, placeholders until firmware ready

▸ Advanced settings                                        ← chevron toggle, collapsed by default
  ┌────────────────────────────────────────────────┐
  │  Wet tolerance %  │  Pulse interval sec        │
  │  [ 5            ] │  [ 120             ]       │
  │  Soak delay sec   │  Max pulses                │
  │  [ 120          ] │  [ 3               ]       │
  └────────────────────────────────────────────────┘

[ Activate ]
```

Behavior:
- Accordion starts collapsed; state is not persisted across page reloads.
- All 4 advanced fields update `pending*` state on `input` event.
- They are included in the dirty-check and saved together with start/stop thresholds when Activate is clicked.
- Dry avg / Wet avg display `--` as placeholders until firmware sends zone values.

## 12) Irrigation Process State Machine (Pulse/Soak)

Auto watering run is a bounded state machine managed by the head.

States:
- `Idle`
- `PrePulseDecision`
- `PulseActive`
- `SoakWait`
- `CheckpointEval`
- `Completed`
- `CompletedByLimit`
- `Aborted`

Definitions:
- One irrigation run starts when AUTO start condition is met and ends in one of terminal states (`Completed`, `CompletedByLimit`, `Aborted`).
- `pulseIntervalSec` controls one watering pulse length.
- `soakDelaySec` controls pause between pulses for moisture stabilization.
- `maxPulses` limits pulses in a single run.

Rules:
- At run start, execute `PrePulseDecision` from fresh telemetry.
- If start condition is true, enter `PulseActive`; otherwise return to `Idle`.
- During `PulseActive`, sensors may sleep for most of the interval.
- Head schedules a short pre-end wake so fresh telemetry arrives near pulse end.
- Intra-pulse checkpoint: one checkpoint at `pulseEnd - 10s` with `T_collect = 1s`.
- If stop condition is met at intra-pulse checkpoint, stop watering immediately and end run as `Completed`.
- If not stopped, pulse ends normally and flow goes to `SoakWait`.
- During `SoakWait`, sensors may sleep; head schedules wake near soak end.
- At soak end checkpoint, evaluate stop/start continuation from fresh telemetry.
- If stop condition is met, end as `Completed`.
- If stop condition is not met and used pulses `< maxPulses`, start next `PulseActive`.
- If used pulses reaches `maxPulses` and stop condition is still not met, end as `CompletedByLimit`.

After terminal states:
- Return sensors to regular sleep cadence (future target 1 hour; current debug cadence 1 minute).
- For `CompletedByLimit`, do not immediately restart watering; next start decision is allowed only on the next regular telemetry wake cycle.

Safety/priority behavior:
- If valid sensors drop below `N_min`, stop immediately and end as `Aborted`.
- If AUTO mode is left (switch to `OFF`, `TIME`, or `MANUAL`) at any moment, abort immediately and end as `Aborted`.
- Safety stop means conservative immediate stop on uncertainty/conflict to avoid overwatering.

Status:
- [x] State set and terminal states locked.
- [x] Intra-pulse checkpoint timing locked (`pulseEnd - 10s`).
- [x] `CompletedByLimit` behavior locked (restart only on next regular wake).
- [x] Mode-change abort priority locked.

Implementation note:
- Runtime behavior matches the pulse/soak flow well for active runs.
- Initial `Idle -> PulseActive` entry is still allowed via degraded fallback path, which is weaker than the planned fresh-only `PrePulseDecision` model.

## 13) Transition Table (Deterministic)

| Current state | Event/Condition | Action | Next state |
|---|---|---|---|
| `Idle` | Fresh telemetry checkpoint, AUTO selected, start condition true | Start watering pulse #1 | `PulseActive` |
| `Idle` | Fresh telemetry checkpoint, start condition false | No watering action | `Idle` |
| `Idle` | Mode changed from AUTO to `OFF`/`TIME`/`MANUAL` | Ensure watering OFF | `Aborted` |
| `PrePulseDecision` | Start condition true | Start watering pulse | `PulseActive` |
| `PrePulseDecision` | Start condition false | Keep watering OFF | `Idle` |
| `PulseActive` | Reached intra-pulse checkpoint (`pulseEnd - 10s`) and stop condition true | Stop watering immediately | `Completed` |
| `PulseActive` | Reached intra-pulse checkpoint and stop condition false | Continue current pulse | `PulseActive` |
| `PulseActive` | Pulse timer elapsed | Stop watering for soak phase | `SoakWait` |
| `PulseActive` | Valid sensor count `< N_min` at checkpoint | Safety stop | `Aborted` |
| `PulseActive` | Mode changed from AUTO | Abort watering immediately | `Aborted` |
| `SoakWait` | Soak timer elapsed; stop condition true at soak checkpoint | Keep watering OFF | `Completed` |
| `SoakWait` | Soak timer elapsed; stop condition false and `usedPulses < maxPulses` | Start next pulse (`usedPulses + 1`) | `PulseActive` |
| `SoakWait` | Soak timer elapsed; stop condition false and `usedPulses >= maxPulses` | End run by limit | `CompletedByLimit` |
| `SoakWait` | Valid sensor count `< N_min` at checkpoint | Safety stop | `Aborted` |
| `SoakWait` | Mode changed from AUTO | Abort sequence | `Aborted` |
| `CheckpointEval` | (logical helper only) evaluate conditions | Route by conditions above | `PulseActive`/`SoakWait`/`Completed`/`CompletedByLimit`/`Aborted` |
| `Completed` | Run ended | Restore regular sensor sleep cadence | `Idle` |
| `CompletedByLimit` | Run ended by pulse cap | Restore regular cadence; forbid immediate restart until next regular wake | `Idle` |
| `Aborted` | Run aborted | Ensure watering OFF; restore regular cadence | `Idle` |

Notes:
- `PrePulseDecision` and `CheckpointEval` may be implemented as explicit states or as logical evaluation steps; behavior must stay equivalent to the table.
- If two events conflict simultaneously, priority is: mode change abort > safety stop (`N < N_min`) > stop condition > continue.

## 14) Runtime Logging (State Machine)

Purpose:
- Provide deterministic bring-up/debug traces for AUTO moisture process on real hardware.

Log format:
- Single-line structured log (key=value pairs).
- Required prefix: `AUTO_SM`.
- One log line per transition and one log line per checkpoint evaluation.

Mandatory fields on each log line:
- `ts_ms`: monotonic timestamp (milliseconds).
- `run_id`: unique irrigation run id (incrementing counter).
- `state`: current state before action.
- `event`: trigger name.
- `next`: next state after action.
- `mode`: current irrigation mode.
- `pulse_idx`: current pulse index (0 when idle/outside run).
- `max_pulses`: configured max pulses.
- `dry`: latest dry zone value (%).
- `wet`: latest wet zone value (%).
- `start_th`: start threshold (%).
- `stop_th`: stop threshold (%).
- `wet_tol`: wet tolerance X (%).
- `n_valid`: valid sensor count.
- `n_min`: minimum valid sensor count.
- `decision`: `start|continue|stop|abort|limit`.
- `reason`: short reason code.

Recommended extra fields:
- `cp`: checkpoint type (`pre_pulse|intra_pulse|soak_end|regular_wake`).
- `pulse_remain_s`: seconds to pulse end when checkpoint was evaluated.
- `soak_remain_s`: seconds to soak end when checkpoint was evaluated.
- `wake_eta_s`: estimated seconds until control/sensor wake (if relevant).
- `nodes`: compact list of node ids used in calculation.

Reason codes (initial set):
- `START_OK`
- `STOP_DRY_GE_STOP`
- `STOP_WET_GE_STOP_PLUS_X`
- `ABORT_N_LT_MIN`
- `ABORT_MODE_CHANGED`
- `COMPLETE_BY_LIMIT`
- `NO_START_CONDITION`

Required log points:
- AUTO run created (`Idle -> PrePulseDecision`).
- Pulse start.
- Intra-pulse checkpoint evaluation.
- Pulse end.
- Soak checkpoint evaluation.
- Transition to each terminal state (`Completed`, `CompletedByLimit`, `Aborted`).
- Return to `Idle` and restoration of regular sleep cadence.

Example lines:
- `AUTO_SM ts_ms=128004 run_id=17 state=PrePulseDecision event=checkpoint next=PulseActive mode=AUTO pulse_idx=1 max_pulses=3 dry=32 wet=41 start_th=35 stop_th=45 wet_tol=5 n_valid=4 n_min=1 decision=start reason=START_OK cp=pre_pulse`
- `AUTO_SM ts_ms=188119 run_id=17 state=PulseActive event=checkpoint next=Completed mode=AUTO pulse_idx=1 max_pulses=3 dry=46 wet=51 start_th=35 stop_th=45 wet_tol=5 n_valid=4 n_min=1 decision=stop reason=STOP_DRY_GE_STOP cp=intra_pulse pulse_remain_s=9`

Status:
- [x] Logging contract locked in spec.
- [x] Firmware emission of operational `AUTO_SM` event set validated in hardware monitor runs (including checkpoint/terminal/degraded reasons used in Phase 2/3 flow).

## 15) UI Diagnostics Contract (Auto Tab)

Purpose:
- Keep user-facing status text aligned with state machine and reason codes.
- Ensure quick debugging without opening serial logs.

Always-visible values (AUTO tab):
- `Start below` and `Stop above` thresholds.
- `Dry avg` and `Wet avg` placeholders (`--`) until zone data is available.

Status hint model:
- UI should show one primary status line derived from machine state and latest checkpoint.
- Primary line is informational, not authoritative control logic.

Recommended status templates:
- Idle/no start: `Dry {dry}% / Wet {wet}% - waiting (start below {start}%).`
- Starting pulse: `Dry {dry}% / Wet {wet}% - starting pulse {i}/{max}.`
- Pulse active: `Pulse {i}/{max} - next check in {eta}s.`
- Soak active: `Soak after pulse {i}/{max} - next check in {eta}s.`
- Completed by moisture: `Stopped: moisture reached target (dry {dry}%, wet {wet}%).`
- Completed by limit: `Stopped: reached max pulses ({max}).`
- Aborted by data loss: `Stopped: insufficient valid sensors ({n_valid}/{n_min}).`
- Aborted by mode change: `Stopped: AUTO cancelled by mode change.`

Reason code to UI mapping (minimum):
- `START_OK` -> "starting pulse"
- `STOP_DRY_GE_STOP` -> "dry zone reached stop threshold"
- `STOP_WET_GE_STOP_PLUS_X` -> "wet zone exceeded stop+tol"
- `ABORT_N_LT_MIN` -> "insufficient valid sensors"
- `ABORT_MODE_CHANGED` -> "mode changed"
- `COMPLETE_BY_LIMIT` -> "max pulses reached"
- `NO_START_CONDITION` -> "waiting for dry zone"

Tone and behavior requirements:
- Keep messages short; one sentence preferred.
- Never show contradictory state and action in same refresh.
- If telemetry is stale, append: `Data stale, waiting next checkpoint.`
- If values are unavailable, show `--` and avoid speculative wording.

Status:
- [x] Diagnostics wording and mapping contract locked.
- [x] Runtime binding to implemented AUTO reason set is active for this phase; remaining UX polish is treated as incremental, not blocking.

## 16) UI Acceptance Checklist (Auto Tab)

Use this checklist before merging UI work:

- [x] Advanced settings section is collapsed by default on page load.
- [x] Chevron row toggles expand/collapse both ways with no layout jumps.
- [x] Four advanced inputs are shown as 2x2 grid on desktop and remain readable on mobile.
- [x] Field defaults are present on first render: wet tolerance `5`, pulse interval `120`, soak delay `120`, max pulses `3`.
- [x] Main status hint remains visible above Advanced settings.
- [x] `Dry avg` and `Wet avg` row is always visible and shows `--` placeholder when zone data is absent.
- [x] Changing any advanced field marks AUTO config as dirty and enables Activate flow.
- [x] Clicking Activate persists start/stop plus all advanced fields in one save operation.
- [x] After successful save and snapshot refresh, UI reflects persisted values and dirty indicator clears.
- [x] If save fails, user sees clear error status and previous persisted values are not silently overwritten.

## 17) Open Items (to discuss and lock)

- [x] Default values for `startThreshold` and `stopThreshold` (`35%` / `45%`, i.e. `350`/`450` permille).
- [x] Persistence model for advanced parameters (saved with irrigation config blob).
- [x] Exact mechanism/API contract for "keep sensors awake" while decision is pending (HEAD issues short `MSG_SLEEP_PLAN.sleepMs` override with lease refresh while AUTO run is active).
- [x] Optional anti-noise guards decision locked for this cycle (defer only if future long-soak field runs expose new instability).

Still open in implementation:
- [x] Intra-pulse checkpoint is strict fresh-only; degraded fallback removed.

## 18) Phase 3 Iteration Plan (Keep-Awake)

Goal:
- Remove reliance on online-snapshot fallback during AUTO run checkpoints by ensuring fresh telemetry is available at decision points.

Scope:
- Head schedules/requests short awake window for eligible sensors around AUTO decision checkpoints.
- AUTO decision path in active run prefers fresh-only evaluation (fallback remains only as guarded bring-up safety path).
- Runtime logs include keep-awake intent/ack visibility for field debugging.

Implementation checklist:
- [x] Define head->sensor keep-awake contract (existing `MSG_SLEEP_PLAN` fields, short base sleep override with lease refresh, existing sleep-ack retry semantics).
- [x] Implement head-side keep-awake scheduler for active AUTO run windows.
- [x] Fresh-only pre-pulse decision window.
- [x] Intra-pulse checkpoint window (`pulseEnd - 10s`).
- [x] Soak-end checkpoint window.
- [x] Implement sensor-side handling of keep-awake request and bounded awake lease (covered by existing sleep-plan apply + sleep-ack handshake path).
- [x] Ensure initial AUTO start consumes fresh-only window.
- [x] Intra-pulse checkpoint uses strict fresh-only behavior.
- [x] Add/extend logs: keep-awake override on/off/expired, plus existing sleep ack accepted/rejected logs.
- [x] Keep fallback path behind explicit reason logging for degraded-mode runs.

Acceptance checklist (hardware monitor):
- [x] During active AUTO run, checkpoint decisions prioritize fresh telemetry for the target window.
- [x] No stale-snapshot fallback decisions at AUTO checkpoints (fresh-only).
- [x] Pulse/soak transitions remain deterministic: `Idle -> PulseActive -> SoakWait` with no false abort.
- [x] CompletedByLimit guard behavior remains intact (no immediate stale restart).
- [x] If keep-awake fails, system degrades safely (no overwatering; clear reason codes in logs).
- [x] AUTO start path uses fresh-only telemetry and direct blocking-condition gating, matching the original plan.

Suggested validation script (manual):
- [x] Configure `pulseIntervalSec=21`, `soakDelaySec=20`, `maxPulses=2`, AUTO mode enabled.
- [x] Capture one full run from first `START_OK` through `CompletedByLimit`.
- [x] Confirm absence of repeated fallback/defer line spam in nominal conditions.
- [x] Repeat with intermittent node availability and confirm safe degraded behavior.

## 19) Development Trace (Apr 1, 2026)

This section records the implementation/validation trail used to stabilize AUTO watering behavior in real hardware monitor runs.

Phase progression:
- Phase 1 completed: AUTO UI + config persistence.
- Phase 2 completed: pulse/soak state machine + fresh-first decisions + restart guard.
- Phase 3 completed: keep-awake orchestration + pulse checkpoint hardening + post-limit gating fix.

Key engineering outcomes captured during validation:
- Fresh-first evaluation retained, with explicit degraded fallback when fresh window is unavailable.
- Fallback/no-start/defer log streams were throttled to preserve signal over noise.
- Pulse checkpoint stop is quorum-aware with defer/retry behavior instead of unsafe single-sample stop.
- CompletedByLimit flow no longer re-enters immediate stale-path restart after returning to Idle.
- AUTO start now requires fresh telemetry and an awake, non-lockout CONTROL.

Reference commit trail (branch `feature/auto-watering-ui-first`):
- `39057d2` implement head-side keep-awake sleep-plan override.
- `f0c97a2` window keep-awake near checkpoints and preserve slot staggering.
- `2d0fee0` add pulse-start keep-awake prime window.
- `260f061` tune keep-awake window timing.
- `3451f55` harden fallback behavior and throttle fallback logs.
- `72db90c` stabilize pulse checkpoint retries and stop quorum.
- `413d569` fix post-limit wait gating in Idle path.
- `4db9227` throttle repeated pulse defer logs.
- `9421b0c` finalize Phase 3 validation status in this spec.

Closure note:
- AUTO watering process tracing is maintained directly in this file as the canonical development log for this feature line.
- Field stabilization is complete, but full plan closure still requires the remaining implementation gaps listed below.

## 20) Implemented vs Planned Gaps

The following items are the main remaining differences between the current firmware and the original plan/spec:

- None for the core AUTO decision semantics described in this spec revision.

Recommended next logic work before declaring full feature closure:

- Long-soak field validation and optional anti-noise refinement only.

## 21) Phase 4 Stabilization (Apr 4, 2026)

Additional fixes applied after extended field testing:

- [x] Keep-awake lease renewal: `AUTO_KEEP_AWAKE_LEASE_MS` (4.5s) was too short for pulse+soak cycles. Fixed by renewing lease every tick in PULSE_ACTIVE and SOAK_WAIT states.
- [x] Soak checkpoint no-data retry: instead of immediate `ABORT_N_LT_MIN` when sensors haven't reported yet during soak checkpoint, system retries every 1.2s with generous timeout.
- [x] Resend sleep plans at pulse→soak transition to ensure sensors get short-cadence plans for soak monitoring.
- [x] New `target_reached` API phase: distinguishes stop-condition-met (moisture target reached) from pulse-limit-reached in UI status messages.
- [x] Dry/wet avg hold: frontend preserves last known dry/wet zone values when server sends null (sensors momentarily non-ONLINE during soak).
- [x] Control SUSPECT fix: `telemetryHeadResendSleepPlansToOnlineNodes()` now skips control node with irrigationActive (prevents rejected sleep plan → SUSPECT). BUSY sleep-ACK reject reason no longer sets SUSPECT state.

Reference commits:
- `4829ff9` keep-awake lease renewal during pulse+soak
- `e62f92b` target_reached phase + dry/wet avg hold
- `2da88e3` stats chart performance optimization
