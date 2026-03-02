## TODO

- [x] Project skeleton: common config structs, logging, build flags (roles: HEAD/SENSOR/CONTROL)
- [x] ESP-NOW basics: init, channel, send, receive callbacks, peer management
- [ ] Pairing protocol (service messages): beacon/join/offer/confirm/ack
  - [x] Pairing 2.0 MVP: always-open handshake (no buttons, no NVS)
  - [ ] Button + LED UX
  - [ ] NVS persistence for paired state
  - [ ] Safe rules: no accidental rebind; factory reset flow
- [ ] Base message definitions (telemetry, battery, cmd, cmd_ack), protocol versioning
- [ ] Sensor -> head telemetry (happy path) + ack policy + retries
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


## ACCEPTANCE CRITERIA

[ ] “HEAD waits for serial in dev builds only”

### 0. Project Skeleton

**Done when:**

- [x] Firmware builds for HEAD, SENSOR and CONTROL (PlatformIO envs or build flags)
- [ ] Startup log prints:
  - [ ] device role
  - [ ] protocol version
  - [ ] deviceUID (factory MAC)
  - [ ] WiFi channel
  - [ ] paired status
- [ ] Logging levels exist (INFO/WARN/ERROR)

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

- [ ] UX
  - [ ] Head short press -> pairing open 120s
  - [ ] Head short press again -> pairing closes
  - [ ] Node short press -> join mode 60s
  - [ ] Node long press -> factory reset
  - [ ] LED patterns implemented:
    - [ ] pairing open
    - [ ] joining
    - [ ] success
    - [ ] error
    - [ ] factory reset
- [ ] Functional
  - [ ] Unpaired node pairs in <10 seconds
  - [ ] Node stores in NVS:
    - [ ] paired flag
    - [ ] headMAC
    - [ ] nodeId
  - [ ] Node reconnects after reboot
  - [ ] Head accepts node after reboot
- [ ] Safety
  - [ ] Paired node does NOT rebind by short press
  - [ ] Rebind only after factory reset
- [ ] Multi-head safety
  - [ ] Node refuses pairing if multiple heads in pairing mode

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
  - [ ] moisture raw
  - [ ] battery voltage
  - [ ] status flags
- [ ] Commands include:
  - [ ] cmdId
- [ ] Command ACK includes:
  - [ ] cmdId
  - [ ] status

### 4. Sensor Telemetry

**Done when:**

- [ ] Sensor sends telemetry successfully
- [ ] ACK received from head
- [ ] Transmission cycle <500 ms
- [ ] Retries:
  - [ ] Retries implemented
  - [ ] Sensor sleeps after failure
- [ ] Duplicates:
  - [ ] Head ignores duplicate packets
- [ ] Failure test:
  - [ ] Sensor survives head being offline

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
  - [ ] Pump disabled on critical battery
  - [ ] Alert sent to head

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
  - [ ] Sensors reconnect after head reboot
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
