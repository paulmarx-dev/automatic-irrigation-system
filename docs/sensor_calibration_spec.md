## TODO

Update 2026-03-15:
- No calibration logic changes in this cycle.
- Recent work was focused on head web transport/recovery UX and TIME scheduling behavior.

- [x] Sensor calibration flow (button-triggered): dry/wet points, NVS storage, moisture normalization


## ACCEPTANCE CRITERIA

### Sensor Calibration

**Done when:**


#### Trigger & UX

- [x] Calibration mode can be started by triple short button press within 2 seconds
- [x] Sensor indicates entering calibration mode via LED pattern
- [x] Calibration can be aborted by long button press
- [x] Abort leaves previous calibration intact


#### Dry Measurement Phase

- [x] Sensor enters dry measurement phase (air)
- [x] LED indicates dry measurement in progress
- [ ] Dry measurement duration ≥ 2 seconds
- [ ] Multiple samples collected (≥ 20 samples)
- [x] Dry value computed as stable average or median
- [ ] Dry value stored as:

    cal_dry_raw

- [ ] LED indicates dry value saved


#### Wet Measurement Phase

- [x] Sensor prompts user via LED to place probe into water
- [x] Sensor enters wet measurement phase automatically after delay or button press
- [ ] Wet measurement duration ≥ 2 seconds
- [ ] Multiple samples collected (≥ 20 samples)
- [x] Wet value computed as stable average or median
- [ ] Wet value stored as:

    cal_wet_raw

- [ ] LED indicates wet value saved


#### Validation

- [x] Calibration rejected if:

    abs(cal_wet_raw - cal_dry_raw) < MIN_CALIBRATION_SPAN

- [x] LED indicates calibration error if rejected
- [x] Previous calibration remains valid if rejected

Recommended initial constant:

    MIN_CALIBRATION_SPAN ≈ 200 ADC counts

(adjustable later)


#### Persistence (NVS)

- [x] Sensor stores in NVS:

    cal_valid
    cal_dry_raw
    cal_wet_raw

- [x] Calibration values restored after reboot
- [ ] Calibration values restored after deep sleep


#### Moisture Calculation

- [x] Sensor computes normalized moisture value:

    moisture = (raw - cal_dry_raw) / (cal_wet_raw - cal_dry_raw)

- [x] Moisture value clamped to:

    0.0 ≤ moisture ≤ 1.0

- [x] Moisture percentage available:

    0–100%


#### Reliability

- [ ] Calibration survives ≥100 deep sleep cycles
- [ ] Calibration works with battery supply
- [x] Calibration works without head unit present


### LED Behaviour (Reference)

Recommended patterns:

    Enter calibration:
      3 short flashes

    Dry measurement:
      slow blinking

    Dry saved:
      1 long flash

    Prompt "put in water":
      double blink repeating

    Wet measurement:
      fast blinking

    Calibration success:
      3 long flashes

    Calibration error:
      rapid blinking

    Abort:
      1 long flash
