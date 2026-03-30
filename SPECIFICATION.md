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

### 2. QR Scan

- On entry, the device is idle and ready to scan for a QR code.
- The LED shows the scan animation.
- After camera startup, audio may be initialized and a short `scan_start`
  chime may be played after a short delay.
- After the startup chime, speaker output may be disabled while active scanning
  continues, to reduce interference noise.
- On current hardware, leaving `EXIO6` HIGH is a required stability
  workaround. Vendor board examples describe `EXIO6` as switching the camera
  between the `TX/RX` path (`HIGH`) and the USB `D+/D-` path (`LOW`). Exiting
  `QR Scan` should power the camera down, but should not currently drive
  `EXIO6` LOW until that interaction is understood and fixed.
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
  budget, the device should play an error sound, wipe saved Wi-Fi credentials,
  and remain in or return to `Wi-Fi Portal`.
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
- Long-pressing `KEY2` from this state returns to `QR Scan`.

### 7. Sleep

- The device enters a low-power mode after the configured `Idle` timeout when
  Wi-Fi is disabled.
- A sleep sound may be played on entry.
- Wake-up behavior must be explicit in the implementation.
- Waking from sleep returns to `QR Scan`.

## Buttons

Long-press duration for all UI button actions is 3 seconds unless specified
otherwise.

### In `Playing` or `Paused`

- `KEY1` short press: previous track. If the current track has already played
  past the configured threshold, restart the current track instead.
- `KEY3` short press: next track.
- `KEY2` short press: toggle play/pause.
- `KEY1` long press: decrease volume by one step.
- `KEY3` long press: increase volume by one step.
- `KEY2` long press: stop playback and return to `QR Scan`.

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
- Sleep sound: short sound played when entering `Sleep`.
- Button sound: optional short confirmation sound for button presses.
- Error sound: played for invalid QR codes, missing albums, or empty albums.
- Wi-Fi connected sound: played when Wi-Fi connects successfully.

If a feedback sound conflicts with currently playing content, the implementation
must define whether the sound is mixed, queued, or allowed to interrupt current
playback. The current preferred behavior is a single queued playback path.

When transitioning from active scanning to QR error or album playback, the
camera should be stopped before initializing audio for the next sound or media
playback path.

The current board workaround still powers the camera down on scan exit, but
keeps `EXIO6` HIGH because driving it LOW has been observed to break USB
logging/flashing stability on this board.

## LED Behavior

Each main state should have a unique LED pattern.

- `Wi-Fi Portal`: breathing animation (green).
- `QR Scan`: rotational scanning animation distinct from portal mode (orange).
- `Idle`: a lower-activity pattern distinct from active scanning.
- `Playing`: rainbow rotation effect.
- `Sleep`: LED off or a clearly reduced low-power pattern.
- `Factory Reset`: red blinking pattern before reboot.

Future battery-aware optimizations may change the playback LED pattern.

## Wi-Fi and Web Interface

- The web interface is used to manage files on the microSD card and configure
  the device.
- Wi-Fi is disabled by default.
- Provisioning UI and normal app UI are separate concerns.
- The user explicitly enables Wi-Fi by pressing the Wi-Fi trigger control.
- If no Wi-Fi network is configured when Wi-Fi is enabled, the device enters
  `Wi-Fi Portal` and serves the provisioning UI from the access point.
- Saving provisioning credentials must not require a device reboot in the
  normal case; the preferred behavior is immediate connection attempt in the
  current session.
- Once connected to Wi-Fi, the device should serve the normal application UI on
  `/`.
- The normal application UI should be password protected.
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

## Future Additions

- Built-in battery.
- When running on battery, the rainbow playback effect is turned off.
- When battery reaches 20%, the LED blinks red.
- When battery reaches 10%, a low-battery warning sound is played every
  20 seconds.
