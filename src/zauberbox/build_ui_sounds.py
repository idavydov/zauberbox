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


def _write_wav(path: Path, pcm: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(SAMPLE_RATE)
        wav_file.writeframes(b"".join(struct.pack("<h", sample) for sample in pcm))


def build_ui_sounds(output_dir: Path) -> None:
    boot = _tone(880.0, 45, amplitude=0.055)
    wifi_connected = _tone(2000.0, 35, amplitude=0.045) + _silence(35) + _tone(2500.0, 50, amplitude=0.045)

    files = {
        "boot.wav": boot,
        "wifi_connected.wav": wifi_connected,
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
