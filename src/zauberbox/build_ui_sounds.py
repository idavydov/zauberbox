import argparse
import math
import struct
import wave
from pathlib import Path


SAMPLE_RATE = 16000


def _project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _tone(freq_hz: float, duration_ms: int, amplitude: float, fade_ms: int = 6) -> list[int]:
    samples = int(SAMPLE_RATE * duration_ms / 1000)
    fade_samples = max(1, int(SAMPLE_RATE * fade_ms / 1000))
    pcm: list[int] = []

    for i in range(samples):
        envelope = 1.0
        if i < fade_samples:
            envelope = i / fade_samples
        elif i >= samples - fade_samples:
            envelope = max(0.0, (samples - 1 - i) / fade_samples)

        sample = math.sin(2.0 * math.pi * freq_hz * i / SAMPLE_RATE) * amplitude * envelope
        pcm.append(int(max(-1.0, min(1.0, sample)) * 32767))

    return pcm


def _silence(duration_ms: int) -> list[int]:
    return [0] * int(SAMPLE_RATE * duration_ms / 1000)


def _sequence(segments: list[tuple[float, int, float, int]]) -> list[int]:
    pcm: list[int] = []
    for freq_hz, duration_ms, amplitude, pause_ms in segments:
        pcm.extend(_tone(freq_hz, duration_ms, amplitude=amplitude))
        if pause_ms > 0:
            pcm.extend(_silence(pause_ms))
    return pcm


def _write_wav(path: Path, pcm: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(SAMPLE_RATE)
        wav_file.writeframes(b"".join(struct.pack("<h", sample) for sample in pcm))


def build_ui_sounds(output_dir: Path) -> None:
    scan_start = _sequence(
        [
            (1046.5, 45, 0.04, 30),
            (1318.5, 65, 0.04, 0),
        ]
    )
    sleep = _sequence(
        [
            (740.0, 70, 0.045, 20),
            (587.0, 90, 0.04, 0),
        ]
    )
    error = _sequence(
        [
            (330.0, 90, 0.055, 45),
            (330.0, 120, 0.055, 0),
        ]
    )
    wifi_connected = _sequence(
        [
            (2000.0, 35, 0.045, 35),
            (2500.0, 50, 0.045, 0),
        ]
    )
    button = _sequence(
        [
            (1300.0, 22, 0.03, 0),
        ]
    )

    files = {
        "scan_start.wav": scan_start,
        "sleep.wav": sleep,
        "error.wav": error,
        "wifi_connected.wav": wifi_connected,
        "button.wav": button,
    }

    for name, pcm in files.items():
        target = output_dir / name
        _write_wav(target, pcm)
        print(f"Wrote {target}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate UI sound WAV assets for the firmware filesystem.")
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=_project_root() / "firmware" / "data",
        help="Directory where the generated WAV files will be written.",
    )
    args = parser.parse_args()
    build_ui_sounds(args.out_dir.resolve())


if __name__ == "__main__":
    main()
