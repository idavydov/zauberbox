# Battery Implementation Plan

## Goal

Add battery-aware behavior to the device in a way that is reliable on this
board, matches the existing state machine, and does not guess too much from raw
battery voltage.

This plan is ordered by implementation priority, starting with the minimum work
needed to make battery-powered operation safe and useful.

## Known Inputs

- `SPECIFICATION.md` already defines these future battery behaviors:
  - built-in battery support
  - rainbow playback effect disabled on battery
  - red LED blink at 20%
  - low-battery warning sound every 20 seconds at 10%
- The current firmware has a `Sleep` app state, but no actual deep-sleep
  implementation yet.
- The Waveshare demo in `tmp/ESP32-S3-AUDIO-Board-Demo/Arduino/examples/LVGL_Arduino`
  measures battery voltage via ADC:
  - `BAT_ADC_PIN = 8`
  - `analogReadMilliVolts(BAT_ADC_PIN)`
  - scaled by `3.0 / Measurement_offset`
- That demo is a useful board hint, not a hardware truth source. The ADC pin and
  divider still need to be validated on the actual board and against measured
  voltage.

## MVP: Most Pressing Work

### 1. Battery telemetry foundation

This is the first step. Everything else depends on it.

Implement a small `BatteryService` in firmware that:

- samples battery voltage on a fixed cadence
- averages or filters readings
- exposes:
  - raw ADC millivolts
  - calculated battery voltage
  - estimated percent
  - low / critical flags
  - whether the device is externally powered or running on battery
- applies hysteresis so the state does not flap near thresholds

Why this is first:

- UI without trustworthy telemetry is misleading
- low-battery policy without filtering will be unstable under audio / Wi-Fi load
- deep sleep needs a reliable "running on battery" signal

Recommended implementation shape:

- add `firmware/src/battery_service.h` and `firmware/src/battery_service.cpp`
- initialize it from `AppController::begin()`
- update it from `AppController::update()`
- keep thresholds and calibration constants centralized in the service

Recommended first API:

- `begin()`
- `update()`
- `snapshot()`
- `isOnBattery()`
- `isLow()`
- `isCritical()`

Notes:

- If there is no charger-status GPIO, "on battery" may need to be inferred from
  voltage behavior or from whether USB / external power can be detected
  elsewhere. That hardware question should be resolved early.
- Percent should initially be approximate and derived from voltage. It should be
  labeled as such in the web UI until calibrated.

### 2. Graceful low-battery protection

Before adding user-facing polish, prevent bad runtime behavior near empty
battery.

Implement a firmware battery policy layer that:

- defines thresholds for:
  - low warning
  - critical
  - forced sleep / shutdown
- uses hysteresis and timing guards
- never waits for the battery protection PCB to be the first cutoff

Recommended behavior:

- low threshold: warn, reduce cosmetic power use
- critical threshold: stop non-essential activity
- forced-sleep threshold: enter low-power state cleanly before brownout

Why this matters:

- the battery PCB is a cell-safety backstop, not product behavior
- waiting for protection cutoff risks brownouts, corrupted writes, and unstable
  Wi-Fi / audio / camera behavior

### 3. Deep sleep when idle on battery

This is the main power-saving feature and should be part of MVP.

Implement real low-power entry when:

- app state is `Idle`
- Wi-Fi is disabled
- device is on battery
- the idle timeout expires

Recommended behavior:

- `Idle` on battery transitions to `Sleep`
- `Sleep` performs explicit shutdown of active peripherals
- `Sleep` then enters ESP32 deep sleep
- wake returns to `QrScan`, matching the specification

This needs explicit wake-source design:

- wake on a button press
- define exactly which button
- debounce and validate wake behavior on hardware

Recommended implementation shape:

- keep `AppState::Sleep`
- add a real sleep entry path in `AppController`
- add a dedicated sleep helper if needed, rather than scattering
  `esp_sleep_*` calls across services

### 4. Battery-aware LED policy

Once telemetry and protection exist, reduce needless power draw.

MVP battery LED policy:

- on battery, disable the `Playing` rainbow effect
- on battery, simplify or disable `Paused` LEDs
- keep `QrScan` visible, but consider lower brightness
- preserve an unambiguous low-battery warning pattern

This should be driven by battery state, not by ad hoc checks spread through the
LED task.

Recommended shape:

- `LedController` reads battery status from a lightweight shared snapshot
- battery policy decides "normal", "reduced", or "warning" LED mode

### 5. Minimal battery telemetry in the web app

Once firmware telemetry exists, expose it in the web UI.

MVP web behavior:

- add a small battery status block to the dashboard or debug area
- show:
  - percentage
  - voltage
  - charging / external power state if available
- expose the raw telemetry through a simple API endpoint

Recommended API:

- `GET /api/status` or `GET /api/power`

I would not start by spreading battery info across multiple pages. One small,
stable status surface is enough for MVP.

## Next Priority

### 6. Low-battery warning behavior

After the protection path works, implement the spec-level warnings:

- at about 20%: red LED warning
- at about 10%: warning sound every 20 seconds

Recommended constraints:

- do not spam warning sounds during active playback unless battery is critical
- rate-limit warnings strictly
- make the warning scheduler battery-policy-driven, not embedded in random UI
  code paths

### 7. Better battery percentage calibration

Voltage-to-percent mapping for Li-ion is only approximate, especially under
load.

After MVP:

- collect measured voltage points on this board
- calibrate for:
  - idle
  - Wi-Fi active
  - camera active
  - audio playback
- use a piecewise mapping instead of a naive linear conversion

If a proper fuel gauge exists on the hardware, that should replace voltage-only
percent estimation.

### 8. Battery state in non-debug web UI

After the battery values are trustworthy, consider promoting them from debug to
main UI.

Possible placements:

- compact status row on the main dashboard
- debug view with more detailed raw telemetry

The main UI should stay simple. Detailed diagnostics belong in debug.

## Lower Priority / Later Improvements

### 9. More aggressive power savings on battery

Potential later optimizations:

- reduce QR scan duty cycle on battery
- lower LED brightness globally on battery
- reduce Wi-Fi polling or background activity where possible
- shorten or suppress non-essential UI sounds

These should be measured, not guessed.

### 10. Battery-history / diagnostics

Later debug additions could include:

- current battery snapshot
- last low-battery event
- last forced-sleep reason
- wake reason
- recent min / max battery voltage

This is useful for field debugging but not needed for MVP.

### 11. Charging UX

If charging state can be detected reliably, later improvements could include:

- different LED behavior while charging
- preventing sleep while charging
- more accurate web status wording:
  - `On battery`
  - `Charging`
  - `External power`

## Proposed Implementation Order

1. Validate the hardware measurement path from the Waveshare example.
   - Confirm ADC pin, divider ratio, and whether charger / USB power detection
     exists.
2. Add `BatteryService` with filtered voltage readings and thresholds.
3. Expose telemetry through a firmware API and show it in debug first.
4. Add graceful low-battery protection and forced-sleep behavior.
5. Implement real deep sleep from `Idle` on battery.
6. Reduce LED usage on battery.
7. Add spec-level 20% / 10% warnings.
8. Promote battery level to the main web UI if the values are stable.

## Recommended File Touch Points

Firmware:

- `firmware/src/app_controller.cpp`
- `firmware/src/app_state.h`
- `firmware/src/app_state.cpp`
- `firmware/src/led_controller.cpp`
- `firmware/src/web_server_service.cpp`
- new:
  - `firmware/src/battery_service.h`
  - `firmware/src/battery_service.cpp`

Web:

- `web/app.js`
- `web/style.css`
- `src/zauberbox/web/mock_server.py`

Spec:

- `SPECIFICATION.md`

Only update the spec if the implementation chooses behavior that materially
differs from the current text.

## Open Questions To Resolve Early

1. How do we reliably detect "on battery" versus "externally powered" on this
   board?
2. Is the Waveshare demo ADC mapping (`GPIO8`, x3 divider) correct for this
   exact hardware revision?
3. Which wake source should exit deep sleep?
4. Should low-battery warning sounds be suppressed during `Playing`, or only at
   critical battery levels?
5. Should forced low-battery handling enter deep sleep, or should there also be
   an explicit shutdown state distinct from normal sleep?

## Non-Goals For MVP

- perfect battery percentage accuracy
- full charging UX
- historical battery charts
- battery estimation based on current consumption modeling

The MVP should focus on safe runtime behavior, real power savings, and basic
visibility.
