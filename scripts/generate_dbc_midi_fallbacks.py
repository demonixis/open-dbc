#!/usr/bin/env python3

import argparse
import array
import csv
import hashlib
import io
import shutil
import subprocess
import tempfile
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOUNDFONT_SHA256 = "5b85b6c2c61d10b2b91cddd41efcce7b25cd31c8271d511c73afafbef20b6fa3"
FLUIDSYNTH_VERSION = "2.6.0"
LAME_VERSION = "3.100"
EVIDENCE = ROOT / "scripts/data/dbc_midi_fallbacks.tsv"
SOUNDFONT_NAME = "MuseScore General 0.2"
RENDER_CONTRACT = (
    "44100 Hz stereo s16; gain 0.2; reverb+chorus; DAT reverb; polyphony 256; "
    "CBR 160 kbps joint stereo; no limiter, normalization, trim or fade"
)
EVIDENCE_COLUMNS = (
    "source_midi", "fallback_audio", "midi_sha256", "output_sha256", "duration_seconds", "pcm_peak",
    "fluidsynth", "lame", "soundfont", "soundfont_sha256", "render_contract",
)
# Same-stem MP3 renders of the project's MIDI music. The DarkBASIC Classic
# compositions were removed; new project-authored MIDI files are listed here.
FALLBACKS: tuple[tuple[Path, Path], ...] = ()
DERIVED_AUDIO_COPIES: tuple[tuple[Path, Path], ...] = ()
PINNED_ROWS: tuple[tuple[str, ...], ...] = ()


def Sha256(PathValue: Path) -> str:
    Digest = hashlib.sha256()
    with PathValue.open("rb") as Stream:
        for Chunk in iter(lambda: Stream.read(1024 * 1024), b""):
            Digest.update(Chunk)
    return Digest.hexdigest()


def Output(Command: list[str]) -> str:
    Result = subprocess.run(Command, check=True, capture_output=True, text=True)
    return Result.stdout + Result.stderr


def RequireVersion(Executable: str, Expected: str) -> None:
    Text = Output([Executable, "--version"])
    if Expected not in Text:
        raise SystemExit(f"{Executable} must be version {Expected}")


def InspectWav(PathValue: Path) -> tuple[int, float, int]:
    with wave.open(str(PathValue), "rb") as Source:
        if Source.getframerate() != 44100 or Source.getnchannels() != 2 or Source.getsampwidth() != 2:
            raise SystemExit(f"unexpected PCM format: {PathValue}")
        Frames = Source.getnframes()
        Samples = array.array("h", Source.readframes(Frames))
        if Samples.itemsize != 2:
            raise SystemExit("unexpected host PCM sample size")
        if max((abs(Value) for Value in Samples), default=0) >= 32767:
            raise SystemExit(f"PCM clipping detected: {PathValue}")
        return Frames, Frames / 44100.0, max((abs(Value) for Value in Samples), default=0)


def Render(FluidSynth: str, Lame: str, SoundFont: Path, Midi: Path, Mp3: Path) -> tuple[float, int]:
    with tempfile.TemporaryDirectory(prefix="opendbc-midi-") as Directory:
        Wav = Path(Directory) / "render.wav"
        subprocess.run(
            [
                FluidSynth,
                "--no-midi-in",
                "--no-shell",
                "--quiet",
                "--fast-render",
                str(Wav),
                "--audio-file-type",
                "wav",
                "--audio-file-format",
                "s16",
                "--audio-file-endian",
                "little",
                "--sample-rate",
                "44100",
                "--gain",
                "0.2",
                "--reverb",
                "1",
                "--chorus",
                "1",
                "--midi-channels",
                "16",
                "--audio-channels",
                "1",
                "-o",
                "synth.cpu-cores=1",
                "-o",
                "synth.polyphony=256",
                "-o",
                "synth.reverb.engine=dat",
                "-o",
                "player.reset-synth=1",
                str(SoundFont),
                str(Midi),
            ],
            check=True,
        )
        _, Duration, Peak = InspectWav(Wav)
        Mp3.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                Lame,
                "--silent",
                "--cbr",
                "-b",
                "160",
                "-q",
                "0",
                "-m",
                "j",
                "--noreplaygain",
                str(Wav),
                str(Mp3),
            ],
            check=True,
        )
        return Duration, Peak


def EvidenceRows(Durations: dict[Path, tuple[float, int]]) -> str:
    Rows = ["\t".join(EVIDENCE_COLUMNS)]
    for Midi, Mp3 in FALLBACKS:
        Duration, Peak = Durations[Mp3]
        Rows.append(
            "\t".join(
                (
                    Midi.relative_to(ROOT).as_posix(),
                    Mp3.relative_to(ROOT).as_posix(),
                    Sha256(Midi),
                    Sha256(Mp3),
                    f"{Duration:.6f}",
                    str(Peak),
                    FLUIDSYNTH_VERSION,
                    LAME_VERSION,
                    SOUNDFONT_NAME,
                    SOUNDFONT_SHA256,
                    RENDER_CONTRACT,
                )
            )
        )
    return "\n".join(Rows) + "\n"


def CheckEvidence() -> None:
    if not EVIDENCE.is_file():
        raise SystemExit(f"missing MIDI fallback evidence: {EVIDENCE}")
    Text = EVIDENCE.read_text(encoding="utf-8")
    if not Text.endswith("\n") or "\r" in Text:
        raise SystemExit("MIDI fallback evidence must use newline-terminated LF records")
    try:
        Parsed = list(csv.reader(io.StringIO(Text), delimiter="\t", strict=True))
    except csv.Error as Error:
        raise SystemExit(f"invalid MIDI fallback TSV: {Error}") from Error
    if not Parsed or tuple(Parsed[0]) != EVIDENCE_COLUMNS:
        raise SystemExit("MIDI fallback evidence header/schema is stale")
    if len(Parsed) != len(PINNED_ROWS) + 1 or any(len(Row) != len(EVIDENCE_COLUMNS) for Row in Parsed):
        raise SystemExit("MIDI fallback evidence has an unexpected row or column count")
    for (Midi, Mp3), Values, Expected in zip(FALLBACKS, Parsed[1:], PINNED_ROWS):
        if tuple(Values) != Expected:
            Field = next((EVIDENCE_COLUMNS[Index] for Index, (Actual, Pinned) in
                          enumerate(zip(Values, Expected)) if Actual != Pinned), "row")
            raise SystemExit(f"MIDI fallback evidence {Field} is not pinned for {Expected[0]}")
        if not Midi.is_file() or not Mp3.is_file():
            raise SystemExit(f"missing MIDI fallback pair: {Midi} / {Mp3}")
        if Values[2] != Sha256(Midi):
            raise SystemExit(f"MIDI source hash changed: {Midi}")
        if Values[3] != Sha256(Mp3):
            raise SystemExit(f"MIDI fallback hash changed: {Mp3}")
        try:
            Duration = float(Values[4])
            Peak = int(Values[5])
        except ValueError as Error:
            raise SystemExit(f"invalid MIDI duration or PCM peak for {Midi}") from Error
        if f"{Duration:.6f}" != Values[4] or Duration <= 0 or not 0 < Peak < 32767:
            raise SystemExit(f"invalid MIDI duration or PCM peak contract for {Midi}")
    for Source, Destination in DERIVED_AUDIO_COPIES:
        if not Destination.is_file() or Sha256(Destination) != Sha256(Source):
            raise SystemExit(f"derived Classic audio fallback is stale: {Destination}")
    print("DBC MIDI fallbacks are current")


def main() -> None:
    Parser = argparse.ArgumentParser(description="Generate deterministic MP3 fallbacks for the shipped DBC demos")
    Parser.add_argument("--soundfont", type=Path)
    Parser.add_argument("--fluidsynth", default=shutil.which("fluidsynth"))
    Parser.add_argument("--lame", default=shutil.which("lame"))
    Parser.add_argument("--check", action="store_true")
    Args = Parser.parse_args()
    if Args.check:
        CheckEvidence()
        return
    if not Args.soundfont or not Args.soundfont.is_file():
        raise SystemExit("--soundfont must name MuseScore_General.sf3")
    if Sha256(Args.soundfont) != SOUNDFONT_SHA256:
        raise SystemExit("MuseScore General 0.2 soundfont hash does not match the pinned file")
    if not Args.fluidsynth or not Args.lame:
        raise SystemExit("FluidSynth 2.6.0 and LAME 3.100 are required")
    RequireVersion(Args.fluidsynth, FLUIDSYNTH_VERSION)
    RequireVersion(Args.lame, LAME_VERSION)
    Durations = {}
    for Midi, Mp3 in FALLBACKS:
        Durations[Mp3] = Render(Args.fluidsynth, Args.lame, Args.soundfont, Midi, Mp3)
    for Source, Destination in DERIVED_AUDIO_COPIES:
        Destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(Source, Destination)
    GeneratedEvidence = EvidenceRows(Durations)
    GeneratedRows = tuple(tuple(Row) for Row in csv.reader(io.StringIO(GeneratedEvidence), delimiter="\t"))[1:]
    if GeneratedRows != PINNED_ROWS:
        raise SystemExit("deterministic MIDI render differs from the pinned hashes, durations or PCM peaks")
    EVIDENCE.write_text(GeneratedEvidence, encoding="utf-8")
    print("Generated deterministic DBC MIDI fallbacks")


if __name__ == "__main__":
    main()
