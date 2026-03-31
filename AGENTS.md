# Sound issues
- Camera initialization breaks audio. So camera should be initialized first.
- First sound after boot requires ~1s after sound card initialization.
- Subsequent sounds require ~50-100ms after unmute.
- Audio output should be muted when not used due to interference sounds.

# Git commits
- Code changes belonging to different functionalities should not be
  commited together. Instead split them into logical units; when required
  only parts of files should be staged/commited.

# Running python code
- Use `uv run`

# Platformio
- Libraries should be added via platformio.ini libraries
- `firmware/sdkconfig.waveshare_s3_audio` is a generated file, it should
  not be edited; regenerate instead if needed

# Vendoring
- When vendoring new code make sure to include copyrights/license
- Try to limit changes to vendored libraries
- We vendored `AyresWifiManager` and `qr_reader`

# Web app
- Use `uv run build-ui-sounds` to prepare the minimized web app

# Specification
- `SPECIFICATION.md` has the description of the firmware behavior.
- Try to keep the code in sync with the specification
- Most of the times code development should be guided by the specification
- In rare cases the specification will need to updated to match the code
