# Unit Sleep Coordination Spec

Status: Accepted
Date: 2026-03-15
Branch baseline: feature/unit-sleep-mode

Purpose:
Define one unambiguous source of truth for sleep coordination across HEAD, SENSOR, and CONTROL.
This document is normative for implementation and conflict resolution.

## Thematic Index (quick navigation)

- Governance and authority: rules 1-2
- Sleep handshake reliability: rules 3-14
- Wake recovery and directives: rules 15-17
- Button/service behavior: rules 18-22
- Control constraints during irrigation: rules 23-24
- AUTO coordination and RF behavior: rules 25-31
- Sensor energy policy: rules 32-37

## 37 Normative Rules

1. HEAD is the single source of truth for sleep policy, wake timing, and runtime coordination decisions.
2. Units do not choose long-term sleep schedules autonomously; they execute plans issued by HEAD.
3. Sleep command flow uses a 3-step handshake: HEAD `SLEEP_PLAN` -> UNIT `SLEEP_ACK` -> HEAD `SLEEP_ACK_ACK`.
4. UNIT may enter sleep only after receiving valid `SLEEP_ACK_ACK` for the same `plan_id`.
5. If HEAD does not receive `SLEEP_ACK`, it retries `SLEEP_PLAN` up to 3 times, then marks UNIT as `SUSPECT`.
6. If UNIT is `SUSPECT` and still does not report in the next expected wake/report opportunity, HEAD escalates UNIT state to `LOST`.
7. UNIT that sent `SLEEP_ACK` but did not receive `SLEEP_ACK_ACK` must stay awake and must not sleep silently.
8. In the case from rule 7, UNIT retries `SLEEP_ACK` up to 3 times with short jittered backoff.
9. After `SLEEP_ACK` retry exhaustion, UNIT enters temporary safe-awake mode and requests a fresh directive from HEAD.
10. If HEAD remains unreachable in safe-awake mode, UNIT uses a short fallback sleep (battery-protective) and retries on next wake.
11. Every sleep plan carries monotonic `plan_id`; UNIT accepts only newer plans than last committed one.
12. Repeated receipt of the same `plan_id` is handled idempotently (no duplicated side effects).
13. Sleep handshake messages include `unit_id`, `plan_id`, and bounded validity (`valid_until_ms`) to prevent stale execution.
14. Message replay protection is mandatory: expired or already-committed plans must be rejected.
15. On wake, UNIT sends `WAKE_HELLO` with wake reason and last known `plan_id` whenever it has no confirmed executable sleep plan.
16. HEAD responds to `WAKE_HELLO` with `WAKE_DIRECTIVE` (`sleep_now`, `stay_awake`, `join_rendezvous`, or service mode).
17. Rule 15 is required fallback after battery replacement/reset when UNIT cannot confirm previous sleep handshake completion.
18. Button-origin wake has higher priority than sleep transitions and must always be serviced.
19. Pairing logic must remain fully functional regardless of prior sleep state.
20. Calibration logic must remain fully functional regardless of prior sleep state.
21. In button-triggered debug mode, UNIT sleep is fully disabled (`no_sleep=true`).
22. Debug mode must have explicit exit and a maximum session TTL to avoid accidental battery drain.
23. CONTROL is forbidden to sleep while irrigation is active.
24. If irrigation is active and CONTROL receives a sleep plan, CONTROL must reject it with explicit reason.
25. In AUTO irrigation mode, HEAD schedules wake rendezvous so CONTROL and relevant SENSORs are online together.
26. During AUTO irrigation, SENSOR wake cadence must be high enough to support timely stop decisions from fresh moisture data.
27. Target stop-latency objective in AUTO is 10-15 seconds; hard upper bound is 30 seconds.
28. During active irrigation, HEAD should target SENSOR update cadence around 8-12 seconds effective per sensor group.
29. To avoid RF congestion, SENSOR wake/send operations must be slotted with stagger plus small random jitter.
30. Slotting parameters must be deterministic from `unit_id` and bounded to avoid collisions and burst storms.
31. UNIT uplink retry counts must be bounded; protocol must prefer deterministic recovery over infinite retries.
32. SENSOR primary energy policy: measure -> send -> complete sleep handshake -> sleep for HEAD-issued interval.
33. If valid sleep handshake cannot complete, SENSOR must use short fallback behavior that preserves battery and recoverability.
34. HEAD-issued sleep intervals for SENSOR must respect global min/max bounds (recommended min 10s, max 30min).
35. Recommended default SENSOR intervals: normal mode 5-10min (start at 7min), irrigation mode 10-15s, post-irrigation settle 20-30s for 1-2 cycles.
36. Capacity target for scheduling and slot design is 8 SENSORs nominal, with graceful degradation up to 12.
37. Sensor cannot uplink while physically sleeping; no separate urgent uplink mode is introduced in this protocol.

## Additional 20 Accepted Rules (Operational Constants Lock)

These constants are mandatory implementation values for v1 and map directly to the normative rules.

1. `AUTO_STOP_TARGET_LATENCY_SEC = 10..15` (rules 27, 28)
2. `AUTO_STOP_MAX_LATENCY_SEC = 30` (rule 27)
3. `AUTO_SENSOR_FRESHNESS_SEC = 8..12` (rule 28)
4. `WAIT_ACK_ACK_TIMEOUT_MS = 1200` (rules 7, 8)
5. `SLEEP_ACK_RETRY_MAX = 3` (rule 8)
6. `SLEEP_ACK_RETRY_JITTER_MS = 150..350` (rule 8)
7. `SAFE_RECOVERY_HOLD_SEC = 20..30` (rule 9)
8. `SAFE_RECOVERY_REQUIRES_DIRECTIVE = true` (rules 9, 16)
9. `HEAD_UNREACHABLE_FALLBACK_SLEEP_SEC = 30..60` (rule 10)
10. `SENSOR_NORMAL_SLEEP_MIN = 5min` (rule 35)
11. `SENSOR_NORMAL_SLEEP_MAX = 10min` (rule 35)
12. `SENSOR_NORMAL_SLEEP_DEFAULT = 7min` (rule 35)
13. `SENSOR_IRRIGATION_SLEEP_SEC = 10..15` (rule 35)
14. `SENSOR_POST_IRRIGATION_SETTLE_CYCLES = 1..2` (rule 35)
15. `SENSOR_POST_IRRIGATION_SETTLE_SEC = 20..30` (rule 35)
16. `PLAN_SLEEP_MIN_SEC = 10` (rule 34)
17. `PLAN_SLEEP_MAX_SEC = 1800` (rule 34)
18. `SCHEDULING_CAPACITY_NOMINAL = 8` (rule 36)
19. `SCHEDULING_CAPACITY_GRACEFUL_MAX = 12` (rule 36)
20. `RENDEZVOUS_SLOT_MS = 120..180 (+micro-jitter)` (rules 29, 30)

## This Iteration Decisions (locked)

1. SENSOR and CONTROL use one shared sleep protocol message set; role-specific behavior is policy-level only.
2. CONTROL follows normal sleep handshake rules only when irrigation is not active.
3. State transition policy is retry-based, not abstract window-based:
  - after 3 missed `SLEEP_PLAN`/`SLEEP_ACK` attempts: `SUSPECT`
  - if still absent at next expected wake/report: `LOST`
4. `WAKE_HELLO` means a unit-initiated resynchronization message sent after wake when no confirmed executable plan exists.
5. Debug no-sleep TTL default is 30 minutes.
6. Advanced AUTO irrigation rendezvous strategy is deferred to a dedicated next iteration.
7. If CONTROL is considered lost/unavailable, HEAD cancels active irrigation and all scheduled irrigation intents.
8. HEAD must publish explicit service reason/status that irrigation is unavailable due to CONTROL loss.
9. Manual irrigation requested while CONTROL sleeps is represented as `scheduled` and starts at nearest CONTROL wake, unless canceled by rule 7.

## Current Deployed Profile (2026-04-04)

This block documents currently deployed behavior in firmware and has precedence for field validation.

- HEAD sleep plan handshake is active: `SLEEP_PLAN` -> `SLEEP_ACK` -> `SLEEP_ACK_ACK`.
- Base head-issued sleep duration is currently `SLEEP_BASE_DURATION_MS = 60000`.
- During AUTO irrigation, keep-awake lease is renewed every tick in PULSE_ACTIVE and SOAK_WAIT states (lease = 4500ms, base sleep override = 1200ms).
- Sleep plans are resent at pulse-to-soak transition to ensure sensors get short-cadence plans.
- `telemetryHeadResendSleepPlansToOnlineNodes()` skips control node when `irrigationActive` is set.
- BUSY sleep-ACK reject from control during irrigation no longer marks node as SUSPECT.
- CONTROL battery tiers are active:
  - Tier 0: 60s base sleep
  - Tier 1: 15min base sleep
  - Tier 2: 45min base sleep
- Sleep phase alignment is active and anchored to the first observed node report after head start.
- Alignment computes nearest future wake slot and preserves full sleep cadence (no forced rapid reconnect loop).
- Slotting still uses deterministic per-node slot with bounded micro-jitter.

Future cadence changes (for field hourly strategy, etc.) should update this section and corresponding constants in one commit.

## Timebase and Identity Constraints (resolved ambiguity)

- `valid_until_ms` is measured in HEAD monotonic uptime milliseconds.
- Time comparisons for monotonic milliseconds must use wrap-safe arithmetic.
- Every plan-bearing message must include `head_boot_id` so UNIT can reject pre-reboot stale windows.
- UNIT persistence must store `(head_boot_id, last_committed_plan_id)` as atomic pair.
- If `head_boot_id` changes, UNIT treats all prior uncommitted plans as invalid and requests a fresh directive.

## Required Message Set (v1)

- `SLEEP_PLAN`
  - Fields: `unit_id`, `plan_id`, `head_boot_id`, `sleep_ms`, `valid_until_ms`, `mode_hint`, `issued_at_ms`
- `SLEEP_ACK`
  - Fields: `unit_id`, `plan_id`, `head_boot_id`, `accepted`, `effective_sleep_ms`, `reject_reason`
- `SLEEP_ACK_ACK`
  - Fields: `unit_id`, `plan_id`, `head_boot_id`, `commit=true`
- `WAKE_HELLO`
  - Purpose: unit-initiated resync request after wake when plan state is uncertain
  - Fields: `unit_id`, `wake_reason`, `last_plan_id`, `boot_counter`, `uptime_ms`
- `WAKE_DIRECTIVE`
  - Fields: `unit_id`, `head_boot_id`, `directive_type`, `sleep_ms`, `window_open_ms`, `reason_code`

## Timing Defaults (initial recommendation)

This block mirrors the locked constants above and is a human-readable summary only.
If values diverge, the "Operational Constants Lock" section is authoritative.

- `SLEEP_PLAN` retry count on HEAD: 3
- `SLEEP_ACK` retry count on UNIT: 3
- `SLEEP_ACK_ACK` wait on UNIT: 1200 ms
- Retry jitter on UNIT ACK resend: 150-350 ms
- Safe-awake hold before fallback sleep: 20-30 s
- Fallback sleep when HEAD unreachable: 30-60 s
- Debug no-sleep TTL default: 30 min

## State Model (minimum)

HEAD per-unit states:
- `ONLINE`
- `SUSPECT`
- `LOST`

UNIT local states:
- `AWAKE_IDLE`
- `AWAKE_WAIT_ACK_ACK`
- `AWAKE_SAFE_RECOVERY`
- `SLEEPING`
- `SERVICE_MODE` (pairing/calibration/debug)

## Implementation Quick Reference (execution order)

1. Add protocol fields and enums (`plan_id`, `head_boot_id`, reject reasons, directive types).
2. Implement HEAD per-unit state machine (`ONLINE` -> `SUSPECT` -> `LOST`) with bounded retries.
3. Implement UNIT handshake path (`SLEEP_PLAN` -> `SLEEP_ACK` -> wait `SLEEP_ACK_ACK` -> sleep).
4. Implement UNIT recovery path (`WAKE_HELLO`/`WAKE_DIRECTIVE`) for reboot/battery replacement cases.
5. Gate button-driven pairing/calibration/debug above sleep transitions.
6. Enforce CONTROL no-sleep during active irrigation with explicit rejection.
7. Implement base slotting primitives; defer advanced AUTO rendezvous strategy to next iteration.
8. Validate timing constants from this spec and only then tune with measured telemetry.

## Conflict Resolution Rule

If code behavior and this spec differ, this spec wins until explicitly revised.

## Revision Rule

Any change to sleep behavior must update this document in the same branch/PR.
