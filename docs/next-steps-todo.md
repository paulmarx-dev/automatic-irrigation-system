# Next Steps TODO

## Completed in this cycle (2026-03-15)

- [x] Web status transport moved to SSE-first snapshots with polling fallback only.
- [x] Transport diagnostics indicator added to UI.
- [x] TIME countdown/status rendering unified to snapshot-authoritative values.
- [x] Home transient fetch errors auto-clear after snapshot recovery.
- [x] Basic UI-facing CONTROL availability model is active (`online` / `offline` / `battery_lockout` / `not_paired`).
- [x] TIME mode stale delayed-start behavior removed (missed cycle is skipped).

## Completed in this cycle (2026-04-04)

- [x] AUTO pulse/soak desync: keep-awake lease renewal every tick in PULSE_ACTIVE and SOAK_WAIT; soak checkpoint retry on no-data instead of immediate abort.
- [x] AUTO stop message: new `target_reached` API phase distinguishes moisture-target-met from pulse-limit-reached.
- [x] Dry/wet avg reset during soak: frontend holds last known value when server sends null.
- [x] Stats chart performance: single-pass flat record format `[nodeIdx, t, m, b, i]` + 2KB send buffer.
- [x] Control SUSPECT during irrigation: resend-sleep-plans skips control with irrigationActive; BUSY sleep-ACK reject no longer marks SUSPECT.
- [x] Manual irrigation UX: timer double-update patterns removed, startup window shrink fixed, dead code cleaned up.
- [x] Stats view MVP: chart endpoint + tab with node visibility toggles and period selector.

## Completed in this cycle (2026-04-01)

- [x] AUTO watering Phase 3 keep-awake stabilization on head runtime path.
- [x] Pulse checkpoint stop hardening: quorum-aware defer/retry and guarded fallback behavior.
- [x] CompletedByLimit post-limit wait gating fixed (no immediate stale-path restart).
- [x] AUTO observability de-spammed for fallback/defer lines while preserving reason visibility.
- [x] Auto-watering spec and acceptance checklists updated to reflect field validation.

## Priority backlog

- [x] CONTROL battery telemetry: send battery metrics from control node (same HW path as sensor) using shared node-side code.
- [x] HEAD battery observability: ingest/display CONTROL battery status and alerts.
- [x] Web UI: show CONTROL card with role-specific fields (different from SENSOR card).
  - Shows irrigation execution state: Running / Starting… / Stopping… / Idle.
  - Calibrate button absent; lockout shown in-card.
- [x] Web/UI naming: rename "Sensors" concept to "Units" where appropriate.
  - Tab button and section heading already say "Units"; internal IDs unchanged.
- [x] Web UI: add manual irrigation duration input field.
- [x] Manual irrigation lease/timer model:
  - [x] HEAD remains source of truth for desired irrigation state.
  - [x] CONTROL executes with lease/deadline.
  - [x] Add max run cap enforcement.
- [x] Irrigation UI: show that watering is unavailable when CONTROL is absent.
  - [x] Distinguish between not paired and missing several expected wake windows.
- [x] Irrigation UI: show time until next scheduled CONTROL wake.
  - Home control status line shows "sleeping, wakes in Xh Ym" when sleepActive + nextContactSec > 0.
- [x] Irrigation UI: adapt AUTO/TIME/MANUAL status text using CONTROL wake ETA.
  - AUTO and TIME schedule hints append "Control wakes in X" when control is sleeping.
- [x] Define UI-facing CONTROL availability model.
  - Source wake schedule from CONTROL policy.
  - Define missed-wake threshold before declaring irrigation unavailable.
- [x] Low-battery shutdown policy for SENSOR/CONTROL:
  - [x] thresholds and behavior for CRITICAL/REPLACE states are implemented,
  - [x] false battery-low decisions while powered by USB are handled (near-zero ADC fallback),
  - [x] USB-power detection fallback logic and diagnostics are implemented.

- [ ] Sleep profile tuning for field cadence:
  - align practical sensor cadence and head-issued sleep policy for field operation windows,
  - keep phase synchronization active while preserving battery-first behavior.

## Notes

- Keep shared node logic unified across SENSOR/CONTROL where possible (pairing, battery/telemetry transport, safety policy).
- Prefer fail-safe OFF defaults when state is ambiguous after reboot/power events.

## Proposed next step (2026-03-30)

- [x] Stats view MVP (completed 2026-04-04):
  - [x] Add `GET /api/stats/moisture` JSON endpoint backed by existing track ring buffer.
  - [x] Include per-node series + average series + irrigation on/off markers.
  - [x] Build minimal Stats tab with period selector (`10m`, `1h`, `6h`, `24h`) and node visibility toggles.
