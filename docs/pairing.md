# Pairing Process (Current Behavior)

Update 2026-03-15:
- No functional changes to pairing protocol in this cycle.
- Web/UI changes were transport/status-layer only (SSE/fallback/recovery behavior).

This document describes the implemented pairing/rebind flow for `head_c6_wroom1` and `sensor_c3_mini`.

## Scope

Implemented:
- Pairing handshake (`BEACON -> JOIN_REQ -> OFFER -> CONFIRM -> ACK`)
- Sensor pairing persistence in NVS
- Head reset recovery via `NOT_PAIRED` telemetry ACK + short candidate rebind window

Not implemented here:
- Multi-node persistent head registry
- Deep-sleep-specific state machine
- Cryptographic identity/authentication

## Timing and Windows

- Head manual pairing window: `120s` (`PAIRING_HEAD_OPEN_MS`)
- Node join window: `60s` (`PAIRING_NODE_JOIN_MS`)
- Head candidate rebind window: `10s` (`REBIND_OPEN_MS`)
- Candidate rebind cooldown: `30s` (`REBIND_COOLDOWN_MS`)

## NVS (Sensor)

Namespace: `pair_node`

Keys:
- `ver` (`uint8`) = `1`
- `paired` (`uint8`) = `0/1`
- `headMac` (`blob[6]`)

Load rules:
- Missing/invalid `ver` => unpaired
- `paired=1` but invalid/missing `headMac` => unpaired

## Normal First Pairing

1. Head opens pairing window (short press).
2. Unpaired sensor enters join mode (auto on boot, or manual).
3. Head sends `BEACON` (while window is open).
4. Sensor sends `JOIN_REQ`.
5. Head responds `OFFER`.
6. Sensor sends `CONFIRM`.
7. Head sends `ACK`.
8. Sensor stores `headMac` to NVS.

## Sensor Boot with Persisted Pairing

1. Sensor boots and loads `pair_node`.
2. If valid record exists, sensor restores paired state and does **not** auto-join.
3. Telemetry continues to saved head MAC.

## Head Factory Reset Recovery (Rebind)

After head factory reset, sensor may still think it is paired.

1. Sensor sends telemetry to old pairing.
2. Head sees telemetry is not from current active pair and replies with telemetry ACK `status=NOT_PAIRED`.
3. Head opens a short candidate rebind window (`10s`) for that sender MAC.
4. Sensor accepts `NOT_PAIRED` only if ACK is valid for current pending telemetry (`src_mac`, `ackSeq`, `nodeId` checks already passed).
5. Sensor clears local pairing (RAM + NVS), enters join mode immediately.
6. Rebind handshake runs and pairing is restored.

## Candidate-MAC Rebind Policy

- Auto-opened rebind window is filtered to one `candidateMac`.
- `JOIN_REQ` is accepted only from that candidate during the auto rebind window.
- Cooldown prevents repeated auto-open spam.
- Manual pairing window remains available for operator-driven pairing.

## Factory Reset (Sensor)

On long press:
1. Sensor resets runtime pairing state.
2. Clears NVS (`pair_node`).
3. Immediately opens join mode.
4. If head pairing window is open, sensor can pair right away.

## Reproducible Test Checklist

1. Pair sensor and head successfully.
2. Reboot sensor: verify restore from NVS and telemetry continues.
3. Factory reset head: verify sensor receives `NOT_PAIRED`, clears pairing, rejoins.
4. Factory reset sensor: verify immediate join mode and successful pairing when head window is open.
5. Verify no continuous reopen spam on head (cooldown behavior).

## Sensor Calibration Process (Current Behavior)

Button logic on Sensor uses a shared multipress window (`1400ms`) with priority:
- `>=5` presses: debug mode
- `==3` presses: enter calibration mode

Decision is made only after the window closes.

### Enter calibration

1. Press Sensor button `3x` quickly.
2. Device enters calibration and shows `cal_enter` once.
3. Then `cal_measure_dry` starts automatically.

### Dry measurement

1. LED base: `cal_measure_dry` (slow pulse 500/500).
2. Wait settle (`1000ms`).
3. Sensor collects 5 moisture mV samples (non-blocking, median used).
4. After dry is captured, success indication is shown once, then prompt stage starts.

### Prompt for wet stage

1. LED base: `cal_prompt_wet` (double tap 400/400/400/800).
2. Timeout: `20s`.
3. Actions in this stage:
	- `1x` press -> start wet measurement.
	- `3x` press -> clear saved calibration from `cal_node`, restore defaults immediately (no reboot), show success once, exit calibration.
	- No press until timeout -> cancel calibration (error once), exit without save.

### Wet measurement and save

1. LED base: `cal_measure_wet` (slow pulse 500/500).
2. Wait settle (`1000ms`).
3. Sensor collects 5 moisture mV samples (median used).
4. Validate `abs(wet - dry) >= 300mV`.
	- If invalid -> error once, exit without save.
5. On success:
	- Save calibration to NVS namespace `cal_node`.
	- Apply calibration immediately to runtime mapping.
	- Show success once.
	- Exit calibration.

### Cancel rule

In calibration mode, any button press outside the wet prompt stage cancels calibration (error once, no save).

### Debug logs (temporary)

During calibration, monitor prints each sample and final medians (`dry`, `wet`, `delta`) for debugging.

## Known Practical Assumption

This flow trusts MAC-level identity for MVP operation (sufficient for local/home deployment).