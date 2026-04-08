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
  connection failure for newly-entered credentials.
- STA hostname and mDNS are in place.
- The normal authenticated web server exists and is served on `/` in STA mode.
- The normal web UI exists and already covers the first scoped file-management
  operations from the spec:
  - list albums
  - create albums
  - upload files
  - delete files
  - rename files or albums
  - remove albums
- Web auth and first-login password setup exist.
- Factory reset already removes persisted Wi-Fi credentials, web auth, and
  runtime config.
- `Idle -> QrScan` already has an explicit path through button handling.

## Known Constraints

- Audio must be initialized after camera startup on current hardware.
- Initializing audio before camera startup is known to break later
  camera/audio behavior.
- The confirmed bug was not generic `esp_camera_init()`. The bad path was the
  vendored `qr_reader/ESP32QRCodeReader::setup()` camera-init path.
- The reliable runtime design is:
  - `QrService` owns camera init/deinit directly via `esp_camera_init()`
  - `ESP32QRCodeReader` is used only for decode task / queue handling
- Current tested delayed short-sound timings are:
  - `scan_start` chime: `500 ms`
  - other delayed short sounds: `50 ms`
- Short UI sounds on the current file-playback path still have a measurable
  startup latency floor of roughly `80-90 ms`; likely hardware-related and
  impossible to reduce.
- Speaker output should be muted while active scanning continues, then
  re-enabled for UI sounds or playback after scan shutdown.
- `EXIO6` is the board routing select for the camera:
  - `HIGH` routes the camera via the `TX/RX` path
  - `LOW` routes it via the USB `D+/D-` path
- Current runtime behavior is correct:
  - QR mode uses `EXIO6 = HIGH`
  - leaving QR mode powers the camera down, but does not switch the camera to
    the USB path

## Remaining Work

### 1. Close the Remaining Product-Policy Gaps

- Finalize playback conflict policy for UI sounds versus album playback.
- Decide whether any non-playback button behavior still belongs in `Idle`.
- Check if QR code scanning mode works after Idle mode.
- More web-app settings (change password).

### 2. Reduce Regression Risk

- Add lightweight host-side tests for:
  - QR payload parsing
  - album sorting/filtering
  - config serialization
  - state transitions
- Document the hardware/runtime constraints in the firmware docs.

## Out of Scope For Now

- battery features, including sleep state
- mixed audio playback
- aggressive performance optimization
