# Zauberbox Firmware Plan

This plan tracks the remaining work against the current
[`SPECIFICATION.md`](./SPECIFICATION.md). It intentionally omits most historical
refactor detail.

## Current Status

- Runtime architecture is in place:
  - [`main.cpp`](./firmware/src/main.cpp) is bootstrap/composition only
  - app behavior is centralized in
    [`app_controller.cpp`](./firmware/src/app_controller.cpp)
  - dedicated services/controllers exist for app state, buttons, LEDs, config,
    Wi-Fi, media, QR, audio, and the I/O expander
- QR scanning works and starts SD-card album playback from `file://NNN` codes.
- Playback controls are implemented for `Playing` and `Paused`.
- Wi-Fi provisioning works without rebooting and returns to the portal on
  connection failure.
- STA hostname and mDNS are in place.
- Normal application web UI/server does not exist yet.

## Known Constraints

- Audio must be initialized after camera startup on current hardware.
- Short UI sounds on the current file-playback path have a noticeable startup
  latency floor of roughly `80-90 ms`.
- `EXIO6` is the board routing select for the camera:
  - `HIGH` routes the camera via the `TX/RX` path
  - `LOW` routes it via the USB `D+/D-` path
- Current runtime behavior is correct:
  - QR mode uses `EXIO6 = HIGH`
  - leaving QR mode powers the camera down, but does not switch the camera to
    the USB path

## Remaining Work

### 1. Build the Normal Wi-Fi App Server

- Add the normal authenticated web server served on `/` when connected to STA.
- Keep provisioning and the normal app server as separate code paths.
- Implement the first scoped file-management operations from the spec:
  - list albums
  - create albums
  - upload files
  - delete files
  - rename files or albums
  - remove albums

### 2. Finish Idle and Sleep Behavior

- Make `Idle -> Sleep` timeout a real implementation, not just a state model.
- Implement explicit wake behavior from `Sleep`.
- Keep Wi-Fi-enabled operation awake while still allowing `QrScan -> Idle`.
- Make the intended path from `Idle` back to `QrScan` explicit.

### 3. Close the Remaining Product-Policy Gaps

- Finalize playback conflict policy for UI sounds versus album playback.
- Decide whether any non-playback button behavior still belongs in `Idle`.
- Confirm the intended QR re-scan/restart policy and document it in code.
- Expand factory reset to all persisted settings once web/app auth exists.

### 4. Reduce Regression Risk

- Add lightweight host-side tests for:
  - QR payload parsing
  - album sorting/filtering
  - config serialization
  - state transitions
- Document the hardware/runtime constraints in the firmware docs.

## Out of Scope For Now

- battery features
- mixed audio playback
- aggressive performance optimization
- a large web UI before the normal app server and auth model exist
