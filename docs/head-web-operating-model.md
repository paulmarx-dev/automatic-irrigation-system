# Head Web Operating Model (AP-first)

## Status
Accepted for current and near-term project scope.

## Intent
Provide useful local web access for:
- end-user information and control,
- field maintenance,
- engineering/debug workflows,

while preserving ESP-NOW reliability as top priority.

Internet/cloud sync is intentionally deferred.

---

## Product principle
1. Primary function: irrigation runtime + ESP-NOW communication.
2. Secondary function: web interface for local operations.
3. Any feature that risks primary function must be mode-gated or deferred.

---

## Network model (current)
- Head runs local AP in service windows (`Irrigation-Setup-XXXX`).
- User connects directly to head AP and uses local web UI/API.
- No dependency on home router for regular operation.
- ESP-NOW channel remains fixed and stable in this mode.

Why this is chosen:
- Single-radio ESP32 cannot keep stable ESP-NOW and arbitrary STA channel simultaneously.
- AP-first avoids unpredictable router channel conflicts.

---

## User interaction model
- Short press opens service window:
  - pairing management,
  - local dashboard/status,
  - local configuration.
- Short press closes service window if setup was not started.
- Started Wi-Fi setup session is allowed to complete/timeout per provisioning rules.

Operationally:
- User should treat web UI as a local control console, not a permanently hosted cloud UI.

---

## Scope now vs later

### In scope now (high value)
- Read-only status dashboard (nodes online/offline, last seen, battery/flags where available).
- Pairing management actions (open/close window, unpair/reset paths).
- Runtime settings (safe bounded config in NVS):
  - moisture threshold,
  - check interval,
  - basic policy toggles.
- Service/debug views for engineering support.

### Explicitly deferred (late project / post-project)
- Internet upload/sync pipeline.
- Always-on LAN web while ESP-NOW is active.
- Cloud account/auth/multi-tenant features.

---

## Deferred uplink mode (future design target)
When internet sync is implemented later:
- It is a manual/explicit short operation mode (e.g., "Sync now").
- ESP-NOW pause during sync is acceptable and expected.
- On completion or timeout, system must return to normal ESP-NOW mode deterministically.

Mandatory safety rules for that future mode:
- hard timeout,
- clear mode indicator in UI/logs,
- guaranteed rollback path to primary runtime.

---

## Risks and mitigations
- Risk: product value not visible if web only shows provisioning.
  - Mitigation: prioritize operational pages (status/control/settings) immediately.
- Risk: users expect always-available local IP from router.
  - Mitigation: clear UX copy: "Connect to head AP for local console".
- Risk: future sync mode may degrade irrigation stability.
  - Mitigation: strict mode gate + timeout + explicit operator action.

---

## Acceptance criteria for this model
- Head can run stable ESP-NOW for long periods without router dependency.
- User can open AP web console on demand and read/adjust local operational settings.
- Debug workflows can access useful diagnostics locally.
- No internet sync logic is required for core user value.

---

## Notes
This document is intentionally conservative: optimize reliability and field usability first, defer network ambition to a controlled future phase.
