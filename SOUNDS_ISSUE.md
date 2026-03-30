# Sound Regression Investigation

## Current Symptom

Camera and audio do not currently coexist reliably on hardware.

Observed behavior:

- `audio -> camera` init order:
  - camera initializes
  - audio requests still log
  - sound output disappears
- `camera -> audio` init order:
  - sound output comes back
  - QR scanning stops decoding reliably

The current working product behavior is therefore the pragmatic serialized mode:

- boot enters `QrScan` first, then audio is initialized afterward
- a short `scan_start` chime is played after about `1s`
- speaker output is then disabled while the camera remains active
- audio is initialized only after scan shutdown for QR error/playback paths

## Bisect Result

The user bisected the original regression to:

- `63103c10ad8ae04f0ef2402478f5fc58eb7e8ee1`
- commit message: `qr code initial implementation`

This was the first commit that turned QR from passive scaffolding into active
camera/decoder runtime.

## Proven Findings

### 1. SD card changes are unrelated

Board-specific SD wiring now confirmed working:

- `CMD = GPIO42`
- `CLK = GPIO40`
- `D0 = GPIO41`
- `SD card EXIO line = EXIO3`

The sound/scan issue is independent from the SD fix.

### 2. Camera activation is the trigger for the original sound loss

Diagnostic cuts established:

- `qr_service` present, camera init disabled:
  - sound works
- camera init enabled, decode worker disabled:
  - sound breaks

Conclusion:

- the original sound regression is tied to camera activation / `esp_camera_init`
- it is not caused by the QR decode worker alone

### 3. Console pin conflict was not sufficient to explain it

We moved the IDF console to USB Serial/JTAG via `firmware/sdkconfig.defaults`.

Result:

- sound was still broken with camera init active

Conclusion:

- the earlier `GPIO43/44` console concern may still matter for tooling/logging,
  but it did not explain the audio failure by itself

### 4. Audio must be initialized after camera init on current hardware

Hardware testing showed:

- if camera init happens first and audio is then initialized/recovered, sound
  can be heard again

This is now a confirmed runtime requirement for the current hardware, not just
an incidental workaround. Initializing audio before camera startup is known to
break later camera/audio behavior.

This strongly suggests shared peripheral/clock/resource reconfiguration rather
than simple application logic bugs.

### 5. Changing QR camera settings affected decode reliability, but did not
solve coexistence

We tried both:

- vendor-style camera settings
- earlier QR-reader-style timing (`10 MHz` XCLK, older LEDC timer)

Current result:

- audio can be made to work with camera-first order
- QR decoding is still unreliable / non-working in the coexistence builds

So camera config tuning alone does not appear to be the whole fix.

### 6. Camera-first startup with muted scan mode is the current pragmatic workaround

Current product-safe approach:

- do not initialize audio before camera startup
- once scanning is active, initialize audio after camera init
- wait about `1s`, play a short `scan_start` chime, then disable the speaker amp
- stop/deinit camera before QR error/playback audio paths
- initialize audio only after scanning has stopped

This reduces both audible interference and the camera/audio coexistence risk.

### 7. Short UI sounds have a real playback startup latency

Button-click timing was instrumented directly on hardware.

Observed trace for `button.wav`:

- BOOT press-down logged at `26509 ms`
- audio file read started at `26580 ms`
- stream became ready at `26596 ms`

So the current stack has an inherent startup latency of roughly `80-90 ms`
before the first playback becomes ready. This is not caused by:

- a hidden `500 ms` delay in our firmware
- leading silence in `button.wav`
- expensive file parsing for the tiny PCM WAV asset

The measured software-side fixed cost includes at least:

- speaker re-enable path in `audioInit()`
- explicit `delay(50)` in `enableSpeaker()`
- normal task scheduling / `ESP32-audioI2S` startup work

Implication:

- very short UI clicks can feel late even when they are triggered correctly
- the current `ESP32-audioI2S` path is acceptable for longer cues and music,
  but it has a noticeable first-sound latency floor for tiny UI sounds

## Most Plausible Root Cause

Best current hypothesis:

- shared clock/peripheral collision between camera and audio

Most plausible concrete mechanism:

- audio uses `I2S_NUM_0` with MCLK/APLL-sensitive configuration
- camera init also reconfigures shared clocking/peripheral resources needed for
  XCLK / capture
- whichever subsystem initializes second breaks the first

Other plausible secondary factors:

- I2C0 contention between:
  - ES8311
  - TCA9555
  - camera SCCB
- GDMA resource conflicts between camera RX and audio TX

## Pragmatic Runtime Decision

The current implementation should follow this model:

1. boot directly into `QrScan`
2. initialize audio only after camera startup
3. wait about `1s`
4. play the `scan_start` chime
5. disable speaker output while camera scan remains active
6. on QR success or QR error path:
   - stop/deinit camera
   - initialize audio
   - wait about `1s`
   - then play the first sound / start playback

This is the low-risk product path.

Additional note:

- UI sounds that use the current file-playback path should assume roughly
  `80-90 ms` startup latency before playback is ready

## Next Diagnostic Steps If Coexistence Is Wanted Later

If true simultaneous camera+audio support becomes important, test in this order:

1. move audio from `I2S_NUM_0` to `I2S_NUM_1`
2. if still needed, investigate avoiding shared APLL assumptions for either:
   - camera XCLK
   - audio MCLK/sample clocking
3. add an I2C mutex around ES8311 / TCA9555 / SCCB init traffic
4. investigate GDMA channel assignment conflicts

The lowest-effort next R&D experiment is:

- move audio to `I2S_NUM_1` and retest coexistence

## Important Notes For Resuming

- `sdkconfig.waveshare_s3_audio` is generated
- persistent config changes belong in:
  - `firmware/sdkconfig.defaults`
- the user observed upload/log instability earlier during QR bring-up
- keep future diagnostics narrow and one-variable-at-a-time
