## TODO

- [ ] Sensor calibration flow (button-triggered): dry/wet points, NVS storage, moisture normalization


## ACCEPTANCE CRITERIA

### Sensor Calibration

**Done when:**


#### Trigger & UX

- [ ] Calibration mode can be started by triple short button press within 2 seconds
- [ ] Sensor indicates entering calibration mode via LED pattern
- [ ] Calibration can be aborted by long button press
- [ ] Abort leaves previous calibration intact


#### Dry Measurement Phase

- [ ] Sensor enters dry measurement phase (air)
- [ ] LED indicates dry measurement in progress
- [ ] Dry measurement duration ≥ 2 seconds
- [ ] Multiple samples collected (≥ 20 samples)
- [ ] Dry value computed as stable average or median
- [ ] Dry value stored as:

    cal_dry_raw

- [ ] LED indicates dry value saved


#### Wet Measurement Phase

- [ ] Sensor prompts user via LED to place probe into water
- [ ] Sensor enters wet measurement phase automatically after delay or button press
- [ ] Wet measurement duration ≥ 2 seconds
- [ ] Multiple samples collected (≥ 20 samples)
- [ ] Wet value computed as stable average or median
- [ ] Wet value stored as:

    cal_wet_raw

- [ ] LED indicates wet value saved


#### Validation

- [ ] Calibration rejected if:

    abs(cal_wet_raw - cal_dry_raw) < MIN_CALIBRATION_SPAN

- [ ] LED indicates calibration error if rejected
- [ ] Previous calibration remains valid if rejected

Recommended initial constant:

    MIN_CALIBRATION_SPAN ≈ 200 ADC counts

(adjustable later)


#### Persistence (NVS)

- [ ] Sensor stores in NVS:

    cal_valid
    cal_dry_raw
    cal_wet_raw

- [ ] Calibration values restored after reboot
- [ ] Calibration values restored after deep sleep


#### Moisture Calculation

- [ ] Sensor computes normalized moisture value:

    moisture = (raw - cal_dry_raw) / (cal_wet_raw - cal_dry_raw)

- [ ] Moisture value clamped to:

    0.0 ≤ moisture ≤ 1.0

- [ ] Moisture percentage available:

    0–100%


#### Reliability

- [ ] Calibration survives ≥100 deep sleep cycles
- [ ] Calibration works with battery supply
- [ ] Calibration works without head unit present


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
