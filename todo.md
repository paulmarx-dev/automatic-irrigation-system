## TODO

- [x] Project skeleton: common config structs, logging, build flags (roles: HEAD/SENSOR/CONTROL)
- [x] ESP-NOW basics: init, channel, send, receive callbacks, peer management
- [ ] Pairing protocol (service messages): beacon/join/offer/confirm/ack
  - [x] Pairing 2.0 MVP: always-open handshake (no buttons, no NVS)
  - [x] Pairing receive router returns handled flag (bool pairingOnRecv)
  - [x] Session and step validation on handshake packets (offer/confirm/ack)
  - [x] Button + LED UX
  - [x] NVS persistence for paired state (Milestone 6: pair_node namespace, ver/paired/headMac)
  - [x] Safe rules: no accidental rebind; factory reset flow
  - [ ] Multi-device pairing: head supports pairing/handling multiple nodes (SENSOR + CONTROL)
  - [x] NVS on HEAD: persist paired nodes registry (MAC/role/nodeId/lastSeen)
  - [ ] NVS on CONTROL: persist paired head state and restore after reboot
- [ ] Base message definitions (telemetry, battery, cmd, cmd_ack), protocol versioning
- [x] Sensor -> head telemetry (happy path) + ack policy + retries
- [ ] Power management for sensor: deep sleep cycle, wake -> measure -> transmit -> sleep
- [ ] Battery-driven behavior: low battery thresholds, "critical" mode, messaging to head
- [ ] Control unit: duty-cycle listen vs active mode, heartbeat, command execution state machine
- [ ] Head logic: command decisions (manual/web + automation), scheduling, irrigation state
- [ ] Reliability: rejoin after head reboot, stale peer cleanup, registry maintenance
- [ ] Optional: time sync, irrigation-phase sampling
- [ ] Web server for head: status page, manual control, pairing management, OTA updates
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

- [ ] UX
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
- [ ] Functional
  - [x] Unpaired node pairs in <10 seconds
  - [ ] Node stores in NVS:
    - [x] paired flag
    - [x] headMAC
    - [x] nodeId
  - [x] Node reconnects after reboot
  - [x] Head accepts node after reboot
- [ ] Safety
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
- [ ] Milestone 6.3: Multi-device pairing + persistence beyond SENSOR
  - [ ] Head supports multiple paired devices concurrently (at least SENSOR + CONTROL)
  - [x] Head stores paired devices registry in NVS and restores on reboot
  - [x] Control stores paired HEAD info in NVS and restores on reboot (code ready)
  - [ ] CONTROL hardware validation pending (run when module is available):
    - [ ] Pair CONTROL with HEAD, verify join success + NVS save
    - [ ] Reboot CONTROL and confirm auto-restore from NVS without manual join
    - [ ] Reboot HEAD and confirm CONTROL is accepted from restored head registry
    - [ ] Factory reset HEAD and verify CONTROL gets NOT_PAIRED and requires manual pair

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

- [ ] All messages contain:
  - [ ] protocolVersion
  - [ ] messageType
  - [ ] seq
  - [ ] nodeId (except service messages)
- [ ] Head rejects:
  - [ ] invalid version
  - [ ] invalid size
  - [ ] unknown type
- [ ] Telemetry includes:
  - [x] moisture raw
  - [x] battery voltage
  - [x] status flags
- [ ] Commands include:
  - [ ] cmdId
- [ ] Command ACK includes:
  - [ ] cmdId
  - [ ] status

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

- [ ] Sensor cycle:
  - [ ] Wake -> Measure -> Send -> Sleep
- [ ] Wake interval configurable
- [ ] Sensor survives 100 sleep cycles
- [ ] No crashes after repeated deep sleep

### 6. Battery Behaviour

**Done when:**

- [ ] Thresholds:
  - [ ] LOW threshold implemented
  - [ ] CRITICAL threshold implemented
- [ ] Sensor:
  - [ ] Sends low battery warning
  - [ ] Reduces activity
- [ ] Control:
  - [ ] Pump/Sensor disabled on critical battery
  - [ ] Alert sent to head
- [ ] Charging state (also send in Flags)


### 7. Control Unit

**Done when:**

- [ ] Modes:
  - [ ] Idle duty-cycle listen works
  - [ ] Active mode works
- [ ] Heartbeat:
  - [ ] Status sent periodically
  - [ ] Head detects online/offline
- [ ] Commands:
  - [ ] START command works
  - [ ] STOP command works
  - [ ] ACK messages correct
- [ ] Safety:
  - [ ] Pump OFF after reboot

### 8. Head Logic

**Done when:**

- [ ] State machine exists:
  - [ ] IDLE
  - [ ] STARTING
  - [ ] RUNNING
  - [ ] STOPPING
- [ ] Decision inputs:
  - [ ] Sensor data
  - [ ] Manual commands
  - [ ] Schedule
- [ ] Manual commands override automatic mode

### 9. Reliability

**Done when:**

- [ ] Recovery:
  - [x] Sensors reconnect after head reboot
  - [ ] Control reconnects after head reboot
- [ ] Registry:
  - [ ] lastSeen stored
  - [ ] Offline devices detected
- [ ] Maintenance:
  - [ ] Nodes can be removed

### 10. Optional Features

**Done when:**

- [ ] Time Sync:
  - [ ] Head provides time
  - [ ] Nodes store timestamps
- [ ] Irrigation Sampling:
  - [ ] Faster sampling during irrigation

### 11. Head Web Interface

**Done when:**

- [ ] Status page shows:
  - [ ] Devices list
  - [ ] lastSeen
  - [ ] battery
  - [ ] moisture
  - [ ] irrigation state
- [ ] Controls:
  - [ ] Start irrigation
  - [ ] Stop irrigation
  - [ ] Open pairing
  - [ ] Close pairing
- [ ] OTA:
  - [ ] Firmware update works
  - [ ] Registry preserved

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
