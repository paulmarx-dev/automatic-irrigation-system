# Next Steps TODO

## Priority backlog

- [x] CONTROL battery telemetry: send battery metrics from control node (same HW path as sensor) using shared node-side code.
- [ ] HEAD battery observability: ingest/display CONTROL battery status and alerts.
- [ ] Web UI: show CONTROL card with role-specific fields (different from SENSOR card).
- [ ] Web/UI naming: rename "Sensors" concept to "Units" where appropriate.
- [x] Web UI: add manual irrigation duration input field.
- [x] Manual irrigation lease/timer model:
  - [x] HEAD remains source of truth for desired irrigation state.
  - [x] CONTROL executes with lease/deadline.
  - [x] Add max run cap enforcement.
- [ ] Irrigation UI: show that watering is unavailable when CONTROL is absent.
  - Distinguish between not paired and missing several expected wake windows.
- [ ] Irrigation UI: show time until next scheduled CONTROL wake.
- [ ] Irrigation UI: adapt AUTO/TIME/MANUAL status text using CONTROL wake ETA.
- [ ] Define UI-facing CONTROL availability model.
  - Source wake schedule from CONTROL policy.
  - Define missed-wake threshold before declaring irrigation unavailable.
- [ ] Low-battery shutdown policy for SENSOR/CONTROL:
  - define thresholds and behavior for CRITICAL/REPLACE states,
  - avoid false battery-low decisions while powered by USB (near-zero/invalid battery ADC profile),
  - implement USB-power detection fallback logic and diagnostics.

## Notes

- Keep shared node logic unified across SENSOR/CONTROL where possible (pairing, battery/telemetry transport, safety policy).
- Prefer fail-safe OFF defaults when state is ambiguous after reboot/power events.
