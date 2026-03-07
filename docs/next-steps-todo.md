# Next Steps TODO

## Priority backlog

- [ ] CONTROL battery telemetry: send battery metrics from control node (same HW path as sensor) using shared node-side code.
- [ ] HEAD battery observability: ingest/display CONTROL battery status and alerts.
- [ ] Web UI: show CONTROL card with role-specific fields (different from SENSOR card).
- [ ] Web/UI naming: rename "Sensors" concept to "Units" where appropriate.
- [ ] Web UI: add manual irrigation duration input field.
- [ ] Manual irrigation lease/timer model:
  - HEAD remains source of truth for desired irrigation state.
  - CONTROL executes with lease/deadline.
  - Add max run cap enforcement (now introduced as default constant, continue full lease implementation).
- [ ] Low-battery shutdown policy for SENSOR/CONTROL:
  - define thresholds and behavior for CRITICAL/REPLACE states,
  - avoid false battery-low decisions while powered by USB (near-zero/invalid battery ADC profile),
  - implement USB-power detection fallback logic and diagnostics.

## Notes

- Keep shared node logic unified across SENSOR/CONTROL where possible (pairing, battery/telemetry transport, safety policy).
- Prefer fail-safe OFF defaults when state is ambiguous after reboot/power events.
