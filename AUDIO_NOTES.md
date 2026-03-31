# Audio Notes

## Current Hardware Facts

- Audio must be initialized after camera startup on current hardware.
- Initializing audio before camera startup is known to break later
  camera/audio behavior.
- The current firmware therefore uses a camera-first runtime model.

## Confirmed Root Cause

The long-running camera/audio bug is now narrowed and fixed:

- direct `esp_camera_init(...)` in the real firmware works
- `ESP32-audioI2S` playback after that works
- QR decoding can still work afterward
- the bad path was specifically the vendored
  `qr_reader/ESP32QRCodeReader::setup()` camera-init path

So the rule is more precise than "camera init breaks audio":

- camera init itself is fine
- the vendored QR-reader camera-init wrapper path was the thing that broke the
  first post-camera sound

The fix is:

- `QrService` owns camera init/deinit directly
- `ESP32QRCodeReader` is used only for decode task / queue handling

## Current Runtime Model

1. Boot enters `QrScan`.
2. Camera starts first.
3. Audio is initialized afterward.
4. The first `scan_start` chime waits `500 ms` before playback.
5. Speaker output is then disabled while active scanning continues.
6. When scanning stops, audio can be re-enabled for UI sounds or playback.

## Delayed First-Sound Rules

The board does not behave well if every short sound is played immediately after
speaker re-enable. The current tested timing is:

- `scan_start` chime: `500 ms`
- other delayed short sounds: `50 ms`

The `50 ms` value is currently used for:

- delayed UI sounds in `QrScan`
- delayed UI sounds in quiet states (`Idle`, `Paused`, `WifiPortal`)
- QR album-start audio after scan shutdown

## Short UI Sound Latency

Instrumented trace for `button.wav` showed:

- BOOT press-down logged at `26509 ms`
- file read started at `26580 ms`
- stream ready at `26596 ms`

So the current audio path has an inherent startup latency of about `80-90 ms`
before playback becomes ready.

This is not caused by:

- hidden multi-hundred-millisecond firmware waits
- leading silence in `button.wav`

It is mostly the normal startup cost of:

- speaker re-enable
- `audioInit()`
- `ESP32-audioI2S` startup / scheduling

Implication:

- very short UI sounds can feel late even when they are triggered correctly
- this is a property of the current board + playback path

## Camera and Audio Coexistence

The safest product behavior remains serialized operation:

- scan with camera active and speaker muted
- stop/deinit camera before QR error/playback audio paths
- initialize audio after scan shutdown

True simultaneous camera+audio support is still unproven.

The previously suspected shared peripheral/clock conflict was too broad.
The confirmed production bug was higher level: the vendored QR-reader
camera-init path, not `esp_camera_init()` in general.

## Other Confirmed Notes

- `EXIO6` is the camera routing select:
  - `HIGH` = `TX/RX`
  - `LOW` = USB `D+/D-`
- the current runtime keeps `EXIO6` `HIGH`
- `sdkconfig.waveshare_s3_audio` is generated
- persistent config changes belong in `firmware/sdkconfig.defaults`

## If Coexistence Is Investigated Later

Recommended next R&D steps:

1. move audio from `I2S_NUM_0` to `I2S_NUM_1`
2. investigate shared APLL / clock assumptions
3. add an I2C mutex around ES8311 / TCA9555 / camera SCCB traffic
4. investigate GDMA allocation conflicts
