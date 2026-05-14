Zauberbox is firmware for the ESP32-S3-AUDIO-Board that turns it into an
audio player for children. Audio content lives on the microSD card. Albums are
stored in directories named `001`, `002`, and so on. QR codes are recognized by
the camera and start playback of the corresponding album directory.

## Purpose

- The device should be usable without a phone or computer during normal use.
- A child should be able to start playback by presenting a QR code.
- A parent should be able to manage content and configuration through Wi-Fi.

## Storage Model

- The microSD card contains album directories at the filesystem root.
- Album directories are named with zero-padded numeric identifiers such as
  `001`, `002`, `003`.
- A scanned QR code maps to one album directory.
- Audio files inside an album directory are played in alphabetical order.
- Subdirectories inside an album directory are ignored.
- If an album directory exists but contains no supported audio files, playback
  does not start and an error feedback sound should be played.

## Supported QR Format

- The primary supported QR payload format is `file://XXX`.
- `XXX` is the album directory name, for example `file://001`.
- The firmware should ignore QR codes with unsupported payload formats.
- If the payload is syntactically valid but the referenced directory does not
  exist, playback does not start and an error feedback sound should be played.
- Scanning the same album again restarts playback from the beginning of that
  album.

## Supported NFC Tag Format

- The RC522 input build reads ISO14443A NTAG cards containing an NDEF Text or
  URI record.
- The decoded NDEF payload uses the same album URL format as QR codes:
  `file://XXX`.
- Re-presenting the same tag while it remains on the reader should not restart
  playback repeatedly; remove and present the tag again to trigger another read.
- The RC522 input build can write NTAG cards from the web app. Writing a tag
  stores an NDEF URI record with the same `file://XXX` album URL format and
  verifies the tag by reading it back.
- While a web-initiated tag write is waiting for a tag, the next presented tag
  is written instead of starting playback. Playback may continue in the
  background, and normal tag-to-play behavior resumes after success or cancel.

## Supported Audio Files

- The player should support local file playback from the microSD card.
- Supported file types should be documented in the implementation and kept
  intentionally small.
- Files that are not recognized as supported audio files are ignored.

## Main States

The firmware behavior should be modeled as explicit states.

### 1. Boot

- The device initializes hardware, storage, buttons, LED, camera, and
  configuration.
- If `KEY1` is held for more than 3 seconds during boot, the device resets to
  factory settings.
- Wi-Fi is disabled by default.
- After initialization completes, the device enters `QR Scan` mode.
- Audio must not be initialized before camera startup, because on current
  hardware audio-first startup can break later camera operation, while
  camera-first startup followed by delayed audio initialization is stable.
- Camera initialization for QR mode should be owned by `QrService` directly.
  The vendored QR-reader wrapper should not own the camera-init path.

### 2. QR Scan

- On entry, the device is idle and ready to scan for a QR code.
- The LED shows the scan animation.
- After camera startup, audio may be initialized and a short `scan_start`
  chime may be played after a short delay.
- The production QR path should initialize the camera directly in `QrService`
  and only then start the QR decoder task.
- After the startup chime, speaker output may be disabled while active scanning
  continues, to reduce interference noise.
- On this board, `EXIO6` selects the camera routing:
  - `HIGH`: camera on the `TX/RX` path
  - `LOW`: camera on the USB `D+/D-` path
- Normal QR operation should keep `EXIO6` HIGH.
- Exiting `QR Scan` should power the camera down, but does not need to switch
  the camera to the USB path.
- The camera continuously looks for a QR code.
- If no QR code is detected for the configured timeout, the device enters
  `Idle`.
- If a valid album QR code is detected, the device enters `Playing`.

### 3. Idle

- `Idle` is a low-activity awake state.
- The camera is not actively scanning in this state.
- This state exists to stop expensive QR scanning without immediately entering
  deep sleep.
- If Wi-Fi is disabled, `Idle` transitions to `Sleep` after the configured idle
  timeout.
- If Wi-Fi is enabled, the device remains in `Idle` until the user disables
  Wi-Fi or another action returns the device to `QR Scan` or `Playing`.

### 4. Wi-Fi Portal

- `Wi-Fi Portal` is a dedicated provisioning mode entered when the user presses
  the Wi-Fi trigger and no Wi-Fi network is configured.
- In this mode the device starts a Wi-Fi access point and serves the
  provisioning UI.
- The access point may be open for initial provisioning.
- After credentials are submitted, the device should attempt to connect to the
  configured STA Wi-Fi without rebooting .
- If connection succeeds, the device exits `Wi-Fi Portal` and returns to the
  `Idle` mode with Wi-Fi enabled.
- If connection does not succeed within the configured retry window or attempt
  budget, the device should play an error sound.
- If the failed attempt came from newly provisioned credentials that have not
  yet connected successfully once, the device should wipe those credentials and
  remain in or return to `Wi-Fi Portal`.
- If the failed attempt used credentials that have already connected
  successfully at least once, the device should keep those credentials and
  return to normal runtime with Wi-Fi disabled.
- Saved Wi-Fi credentials should be wiped only for a provisioning attempt that
  has not yet produced a successful STA connection.
- Once a set of Wi-Fi credentials has connected successfully at least once, the
  device must not wipe those credentials automatically just because a later
  connection attempt or reconnect fails.
- After 5 minutes of inactivity, defined as no connected client and no active
  web requests, the access point switches off.
- While in this state, the LED shows a breathing animation distinct from all
  other states.
- Leaving this state transitions to `QR Scan`.
- The access point does not need to run in the background during normal playback
  or QR scanning.

### 5. Playing

- The referenced album is played from the first supported file.
- Files are played in alphabetical order.
- When the final track ends, the device enters `Idle`.
- The LED shows the playback animation.
- Playback and the web server may run in parallel if Wi-Fi is active.

### 6. Paused

- Playback is paused but the current album and track position are retained.
- Pressing play/pause again returns to `Playing`.
- After the configured paused timeout, the device enters `Sleep`.
- Long-pressing `KEY2` from this state returns to `QR Scan`.

### 7. Sleep

- The device enters a low-power mode after the configured `Idle` timeout when
  Wi-Fi is disabled.
- The device may also enter `Sleep` after the configured paused timeout.
- If battery telemetry is stable and battery reaches the configured critical
  threshold, the device enters `Sleep` immediately from `Idle`, `Playing`,
  `Paused`, `QR Scan`, or `Debug Camera Preview`.
- Wake-up behavior must be explicit in the implementation.
- The current implementation uses deep sleep and wakes on the dedicated `BOOT`
  button.
- Waking from sleep returns to `QR Scan`.

## Buttons

For the media/UI keys (`KEY1`, `KEY2`, `KEY3`), long-press duration is about
`600 ms`.

The boot-time factory-reset hold remains more than `3 seconds`.

### In `Playing` or `Paused`

- `KEY1` short press: previous track. If the current track has already played
  past the configured threshold, restart the current track instead.
- `KEY3` short press: next track.
- `KEY2` short press: toggle play/pause.
- `KEY1` long press (`~600 ms`): decrease volume by one step.
- `KEY3` long press (`~600 ms`): increase volume by one step.
- `KEY2` long press (`~600 ms`): stop playback and return to `QR Scan`.

### Global

- `KEY1` held for more than 3 seconds during boot: reset to factory settings.
- The dedicated `BOOT` button control toggles Wi-Fi mode. This control
  must be named consistently in code and hardware documentation to avoid
  confusion with the ESP32 boot button.
- If Wi-Fi is currently disabled and a Wi-Fi network is configured, pressing
  this control enables Wi-Fi and launches the web server in the background.
- If Wi-Fi is currently disabled and no Wi-Fi network is configured, pressing
  this control enters `Wi-Fi Portal`.
- If Wi-Fi or `Wi-Fi Portal` is currently active, pressing this control again
  disables Wi-Fi and stops the web server or access point.

## Audio Feedback

Feedback sounds should be treated as distinct UI events and must not require a
separate audio path from normal file playback.

- Scan-start chime: short sound played shortly after camera startup when
  entering `QR Scan` from boot.
- Button sound: optional short confirmation sound for button presses.
- Error sound: played for invalid QR codes, missing albums, or empty albums.
- Wi-Fi connected sound: played when Wi-Fi connects successfully.
- Low-battery warning sound: played during album playback when battery is low
  but not yet critical.

If a feedback sound conflicts with currently playing content, the implementation
must define whether the sound is mixed, queued, or allowed to interrupt current
playback.

- The current implementation uses the normal queued playback path for most UI
  sounds.
- The low-battery warning sound is allowed to interrupt current album playback
  and then resume the same track near the previous playback position.

When transitioning from active scanning to QR error or album playback, the
camera should be stopped before initializing audio for the next sound or media
playback path.

On this board, scan exit powers the camera down while leaving `EXIO6` HIGH,
which keeps the camera on the normal `TX/RX` routing rather than switching it
to the USB `D+/D-` path.

The confirmed camera/audio regression was caused by using the vendored
`ESP32QRCodeReader::setup()` camera-init path. Direct camera initialization in
`QrService` is the required implementation strategy.

## LED Behavior

Each main state should have a unique LED pattern.

- `Wi-Fi Portal`: breathing animation (green).
- `QR Scan`: rotational scanning animation distinct from portal mode (orange).
- `Idle`: a lower-activity pattern distinct from active scanning.
- `Playing`: rainbow rotation effect.
- `Playing` with battery saver active: initial rainbow window, then a low-power
  pattern with a long off interval and a short rainbow breathing interval.
- `Paused` with battery saver active: sparse cyan pulse.
- `Sleep`: LED off or a clearly reduced low-power pattern.
- `Factory Reset`: red blinking pattern before reboot.
- Low battery warning: brief red blink overlay distinct from normal state
  animation.

## Wi-Fi and Web Interface

- The web interface is used to manage files on the microSD card and configure
  the device.
- Wi-Fi is disabled by default.
- Provisioning UI and normal app UI are separate concerns.
- The user explicitly enables Wi-Fi by pressing the Wi-Fi trigger control.
- If no Wi-Fi network is configured when Wi-Fi is enabled, the device enters
  `Wi-Fi Portal` and serves the provisioning UI from the access point.
- If Wi-Fi is enabled and saved credentials exist, the device should attempt a
  bounded STA connection in the current session rather than retrying forever.
- If that bounded attempt fails with previously validated credentials, the
  device should play an error sound and return to normal runtime with Wi-Fi
  disabled.
- Saving provisioning credentials must not require a device reboot in the
  normal case; the preferred behavior is immediate connection attempt in the
  current session.
- Once connected to Wi-Fi, the device should serve the normal application UI on
  `/`.
- The normal application UI should be password protected.
- Camera-input builds can generate printable album cards containing the cover
  image and QR code.
- RC522-input builds can generate printable cover-only album tiles without QR
  codes; cover tile sheets contain six images.
- The provisioning UI and the normal application UI should not be conflated in
  one long-lived mode.
- If Wi-Fi is enabled and a network is configured, playback and QR scanning may
  continue while the normal web application is served in the background.
- Playback must continue to work if Wi-Fi reconnects while audio is playing.
- While Wi-Fi is enabled, QR-scan timeout should stop active camera scanning by
  transitioning to `Idle`, but should not force the device into `Sleep`.
- Pressing the Wi-Fi trigger control again disables Wi-Fi and the normal sleep
  policy resumes.

The following web operations should be explicitly scoped by the implementation:

- list albums
- upload files
- delete files
- rename files or albums
- create albums
- remove albums

If any of these operations are intentionally out of scope, that should be
stated explicitly in the implementation notes.

## Factory Reset

Factory reset should remove stored configuration and return the device to the
same state as first boot.

At minimum this includes:

- stored Wi-Fi credentials
- stored web/app authentication credentials
- other locally stored runtime configuration

Factory reset should not erase media files from the microSD card.

## Battery And Power

- The device includes battery telemetry measured by ADC.
- There is no reliable hardware signal that distinguishes external power from
  battery power.
- Battery policy is therefore based on stable battery voltage readings rather
  than a true power-source signal.
- Battery percentage is approximate and derived from a voltage curve; protection
  behavior should be based on voltage thresholds, not percentage alone.
- Battery telemetry should treat invalid near-zero readings as unavailable
  rather than as a critically low battery state.
- Battery telemetry should use a short measurement history and detect large
  jumps so that plug/unplug transitions settle quickly.

### Current Battery Policy

- Low battery threshold: `3600 mV` with clear threshold `3675 mV`.
- Critical battery threshold: `3450 mV` with clear threshold `3525 mV`.
- Playback battery-saver threshold: below `90%` estimated charge.
- Idle sleep timeout: `10 seconds` when Wi-Fi is disabled.
- Paused sleep timeout: `5 minutes`.
- Timeout-based sleep does not require stable battery telemetry; critical
  battery sleep does.
- Critical-battery sleep uses a short startup settle window before forcing
  sleep again after boot or wake.

### Current Battery-Aware Behavior

- When battery is low and telemetry is stable, the LED ring briefly blinks red
  every `4 seconds`.
- During `Playing`, if battery is low but not critical, a low-battery warning
  sound is played every `60 seconds`.
- The low-battery warning sound is only used during `Playing`.
- In battery-saver playback mode, normal rainbow playback remains visible for
  the first `10 seconds` of playback.
- After that initial window, playback LED behavior becomes low-power: `6
  seconds` off followed by a `2 second` rainbow breathing interval.
- When battery is critical, the device enters `Sleep` immediately instead of
  trying to continue operation.
