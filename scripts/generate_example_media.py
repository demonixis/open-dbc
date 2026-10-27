#!/usr/bin/env python3
"""Generate the project-authored sounds and music used by the OpenDBC examples.

Every sound is synthesised deterministically as a PCM WAV. Music is written as
a MIDI file together with a WAV rendered from the same notes, which LOAD MUSIC
plays when no MIDI synthesiser is available. Textures are baked from
procedural materials by assets-src/blender/bake_textures.py.
"""

import argparse
import math
from pathlib import Path

from generate_classic_fixtures import wav


ROOT = Path(__file__).resolve().parents[1]


def hash01(x: int, y: int, seed: int) -> float:
    value = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    value = ((value ^ (value >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((value ^ (value >> 16)) & 0xFFFF) / 65535.0


def engine_hum(rate: int = 11025):
    # A one-second loop: every partial completes whole cycles so the loop is seamless.
    return [0.35 * math.sin(2 * math.pi * 55 * i / rate) + 0.2 * math.sin(2 * math.pi * 110 * i / rate) +
            0.1 * math.sin(2 * math.pi * 165 * i / rate) for i in range(rate)]


def crash(rate: int = 22050):
    count = rate * 3 // 2
    output = []
    for i in range(count):
        envelope = math.exp(-5.0 * i / count)
        noise = hash01(i, 0, 59) * 2.0 - 1.0
        output.append(envelope * (0.7 * noise + 0.3 * math.sin(2 * math.pi * 40 * i / rate)))
    return output


def drip(rate: int = 22050):
    # A falling-pitch plink with a fast decay.
    count = rate * 2 // 5
    phase, output = 0.0, []
    for i in range(count):
        frequency = 1400.0 * math.exp(-6.0 * i / count) + 500.0
        phase += 2 * math.pi * frequency / rate
        output.append(0.6 * math.exp(-9.0 * i / count) * math.sin(phase))
    return output


def chime(rate: int = 22050):
    count = rate * 3 // 4
    return [0.4 * math.exp(-4.0 * i / count) * (math.sin(2 * math.pi * 880 * i / rate) +
                                                0.5 * math.sin(2 * math.pi * 1320 * i / rate))
            for i in range(count)]


def cistern(rate: int = 11025):
    # Four seconds of low drone; each partial completes whole cycles so the loop is seamless.
    count = rate * 4
    return [0.25 * math.sin(2 * math.pi * 41.25 * i / rate) + 0.12 * math.sin(2 * math.pi * 82.5 * i / rate) +
            0.06 * math.sin(2 * math.pi * 61.75 * i / rate) * math.sin(2 * math.pi * 0.25 * i / rate)
            for i in range(count)]


def sweep(start: float, end: float, seconds: float, decay: float, rate: int = 22050):
    count = int(rate * seconds)
    phase, output = 0.0, []
    for i in range(count):
        frequency = start + (end - start) * i / count
        phase += 2 * math.pi * frequency / rate
        square = 1.0 if math.sin(phase) >= 0 else -1.0
        output.append(0.35 * math.exp(-decay * i / count) * (0.6 * math.sin(phase) + 0.4 * square))
    return output


def howl(rate: int = 22050):
    count = int(rate * 1.6)
    phase, output = 0.0, []
    for i in range(count):
        t = i / count
        frequency = 220 + 260 * math.sin(math.pi * t) + 12 * math.sin(2 * math.pi * 6 * t)
        phase += 2 * math.pi * frequency / rate
        breath = hash01(i, 3, 101) * 2.0 - 1.0
        output.append(math.sin(math.pi * t) * (0.45 * math.sin(phase) + 0.12 * breath))
    return output


def noise_burst(seconds: float, decay: float, seed: int, tone: float, rate: int = 22050):
    count = int(rate * seconds)
    return [math.exp(-decay * i / count) * (0.6 * (hash01(i, seed, 103) * 2.0 - 1.0) +
                                            0.4 * math.sin(2 * math.pi * tone * i / rate)) for i in range(count)]


def wind(rate: int = 11025):
    # Five seconds of gusting noise; the gust envelope completes whole cycles so the loop is seamless.
    count = rate * 5
    smooth, output = 0.0, []
    for i in range(count):
        smooth += 0.08 * ((hash01(i, 7, 107) * 2.0 - 1.0) - smooth)
        gust = 0.55 + 0.45 * math.sin(2 * math.pi * 0.4 * i / rate)
        output.append(0.6 * gust * smooth)
    return output


def jet(rate: int = 11025):
    # One-second loop of a high engine whine over water hiss; partials complete whole cycles.
    return [0.25 * math.sin(2 * math.pi * 130 * i / rate) + 0.18 * math.sin(2 * math.pi * 260 * i / rate) +
            0.08 * math.sin(2 * math.pi * 390 * i / rate) + 0.12 * (hash01(i, 13, 109) * 2.0 - 1.0)
            for i in range(rate)]


def fanfare(rate: int = 22050):
    notes = (523.25, 659.25, 783.99, 1046.5)
    output = []
    for index, frequency in enumerate(notes):
        count = rate // 5 if index < 3 else rate * 3 // 5
        for i in range(count):
            envelope = math.exp(-3.0 * i / count)
            output.append(0.3 * envelope * (math.sin(2 * math.pi * frequency * i / rate) +
                                            0.4 * math.sin(4 * math.pi * frequency * i / rate)))
    return output


def tone(frequency: float, seconds: float, decay: float, rate: int = 22050, harmonics=(1.0, 0.0, 0.0)):
    count = int(seconds * rate)
    return [math.exp(-decay * i / rate) * sum(weight * math.sin(2 * math.pi * frequency * (k + 1) * i / rate)
                                               for k, weight in enumerate(harmonics))
            for i in range(count)]


def kick(rate: int = 22050):
    output, phase = [], 0.0
    for i in range(int(0.35 * rate)):
        t = i / rate
        phase += 2 * math.pi * (45 + 110 * math.exp(-t * 30)) / rate
        output.append(0.9 * math.exp(-t * 9) * math.sin(phase))
    return output


def synth_loop(rate: int = 22050):
    # Two seconds of arpeggio; every note fades to silence so the loop joins cleanly.
    notes = (220.0, 261.63, 329.63, 392.0, 329.63, 261.63, 220.0, 196.0)
    step = rate // 4
    output = []
    for frequency in notes:
        output += [0.3 * math.sin(math.pi * i / step) * (math.sin(2 * math.pi * frequency * i / rate) +
                                                         0.3 * math.sin(6 * math.pi * frequency * i / rate))
                   for i in range(step)]
    return output


def buzz(rate: int = 11025):
    # A one-second insect buzz: a 150 Hz sawtooth with a wobble; whole cycles keep the loop seamless.
    return [0.3 * (2.0 * ((150 * i / rate) % 1.0) - 1.0) * (0.75 + 0.25 * math.sin(2 * math.pi * 6 * i / rate))
            for i in range(rate)]


# Music: (MIDI pitch, start beat, length in beats, velocity, channel). Channel 0 carries the
# melody on a vibraphone, channel 1 the accompaniment on an acoustic bass.
def harbour_waltz():
    melody = ((76, 2), (79, 1), (84, 2), (83, 1), (81, 2), (79, 1), (76, 3),
              (77, 2), (81, 1), (86, 2), (84, 1), (83, 1.5), (81, 0.5), (79, 1), (79, 3),
              (76, 2), (79, 1), (84, 2), (88, 1), (86, 2), (84, 1), (81, 3),
              (77, 1), (81, 1), (84, 1), (83, 2), (79, 1), (74, 1), (76, 1), (71, 1), (72, 3))
    chords = ((48, 55, 64), (48, 55, 64), (41, 57, 65), (48, 55, 64), (41, 57, 65), (50, 57, 65),
              (43, 59, 65), (43, 59, 62), (48, 55, 64), (45, 57, 64), (50, 57, 65), (41, 57, 65),
              (41, 57, 65), (43, 59, 65), (43, 55, 62), (48, 55, 64))
    notes, beat = [], 0.0
    for pitch, length in melody:
        notes.append((pitch, beat, length * 0.95, 90, 0))
        beat += length
    for bar, (root, third, fifth) in enumerate(chords):
        notes.append((root, bar * 3, 0.9, 80, 1))
        for offset in (1, 2):
            notes.append((third, bar * 3 + offset, 0.5, 55, 1))
            notes.append((fifth, bar * 3 + offset, 0.5, 55, 1))
    return notes, 132


def night_march():
    chords = ((57, 60, 64), (53, 57, 60), (48, 52, 55), (55, 59, 62),
              (57, 60, 64), (53, 57, 60), (52, 56, 59), (57, 60, 64))
    rhythm = (0, 2, 1, 2, 0, 1, 2, 1)
    notes = []
    for repeat in range(2):
        for bar, chord in enumerate(chords):
            start = (repeat * len(chords) + bar) * 4
            notes.append((chord[0] - 12, start, 1.8, 85, 1))
            notes.append((chord[0] - 12, start + 2, 1.8, 75, 1))
            for step, index in enumerate(rhythm):
                pitch = chord[index] + 12 + (12 if repeat == 1 and step % 4 == 3 else 0)
                notes.append((pitch, start + step * 0.5, 0.45, 70 + 10 * (step % 2 == 0), 0))
    return notes, 104


def midi(notes, tempo: int) -> bytes:
    """A format 0 MIDI file with the two instruments used by render()."""
    division = 480

    def variable_length(value: int) -> bytes:
        data = [value & 0x7F]
        value >>= 7
        while value:
            data.insert(0, (value & 0x7F) | 0x80)
            value >>= 7
        return bytes(data)

    events = [(0, 0, bytes((0xFF, 0x51, 0x03)) + (60000000 // tempo).to_bytes(3, "big")),
              (0, 0, bytes((0xC0, 11))), (0, 0, bytes((0xC1, 32)))]
    for pitch, start, length, velocity, channel in notes:
        events.append((round(start * division), 1, bytes((0x90 | channel, pitch, velocity))))
        events.append((round((start + length) * division), 0, bytes((0x80 | channel, pitch, 0))))
    events.sort(key=lambda event: (event[0], event[1]))
    track, now = bytearray(), 0
    for tick, _, data in events:
        track += variable_length(tick - now) + data
        now = tick
    track += variable_length(0) + bytes((0xFF, 0x2F, 0x00))
    return (b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") + (1).to_bytes(2, "big") +
            division.to_bytes(2, "big") + b"MTrk" + len(track).to_bytes(4, "big") + bytes(track))


def render(notes, tempo: int, rate: int = 22050):
    """Play the notes with two simple timbres: a ringing bar and a plucked bass."""
    seconds_per_beat = 60.0 / tempo
    total = int((max(start + length for _, start, length, _, _ in notes) * seconds_per_beat + 1.0) * rate)
    output = [0.0] * total
    for pitch, start, length, velocity, channel in notes:
        frequency = 440.0 * 2 ** ((pitch - 69) / 12)
        begin = int(start * seconds_per_beat * rate)
        count = int((length * seconds_per_beat + 0.4) * rate)
        gain = velocity / 127.0 * (0.22 if channel == 0 else 0.3)
        decay = 2.5 if channel == 0 else 5.0
        for i in range(min(count, total - begin)):
            t = i / rate
            attack = min(1.0, i / (0.005 * rate))
            wave_value = math.sin(2 * math.pi * frequency * t)
            wave_value += (0.25 if channel == 0 else 0.5) * math.sin(4 * math.pi * frequency * t)
            output[begin + i] += gain * attack * math.exp(-decay * t) * wave_value
    peak = max(abs(value) for value in output) or 1.0
    return [0.8 * value / peak for value in output]


def rumble(rate: int = 11025):
    # A one-second diesel loop: low partials with whole cycles and a firing-rate pulse.
    return [(0.3 * math.sin(2 * math.pi * 40 * i / rate) + 0.18 * math.sin(2 * math.pi * 80 * i / rate) +
             0.12 * (hash01(i // 40, 3, 151) * 2.0 - 1.0)) * (0.7 + 0.3 * math.sin(2 * math.pi * 10 * i / rate))
            for i in range(rate)]


def ping(rate: int = 22050):
    return [0.5 * math.exp(-5.0 * i / rate) * math.sin(2 * math.pi * 1320 * i / rate) for i in range(int(rate * 0.8))]


def hum(rate: int = 11025):
    # A one-second mothership drone: two close tones beating, whole cycles for a seamless loop.
    return [0.25 * math.sin(2 * math.pi * 110 * i / rate) + 0.25 * math.sin(2 * math.pi * 113 * i / rate) +
            0.1 * math.sin(2 * math.pi * 330 * i / rate) for i in range(rate)]


def motor(rate: int = 11025):
    # One-second loop of a four-cylinder burble: firing pulses at 40 Hz with their harmonics,
    # every partial completing whole cycles so the loop is seamless; SET SOUND SPEED raises the pitch.
    output = []
    for i in range(rate):
        t = i / rate
        pulse = sum(math.sin(2 * math.pi * 40 * k * t) / k for k in range(1, 9))
        rasp = 0.15 * (hash01(i, 61, 113) * 2.0 - 1.0)
        output.append(0.22 * pulse * (0.8 + 0.2 * math.sin(2 * math.pi * 20 * t)) + rasp)
    return output


def screech(rate: int = 22050):
    # Half-second loop of tyre squeal: a wavering 1.1 kHz tone with whole-cycle vibrato, over hiss.
    count = rate // 2
    output = []
    for i in range(count):
        t = i / rate
        phase = 2 * math.pi * (1100 * t - 6.0 / (2 * math.pi * 8) * math.cos(2 * math.pi * 8 * t))
        hiss = hash01(i, 67, 127) * 2.0 - 1.0
        output.append(0.28 * math.sin(phase) + 0.18 * math.sin(2 * phase) + 0.12 * hiss)
    return output


def surf(rate: int = 11025):
    # Six seconds of waves: smoothed noise swelling twice, the swell envelope completing whole cycles.
    count = rate * 6
    smooth, output = 0.0, []
    for i in range(count):
        smooth += 0.25 * ((hash01(i, 71, 131) * 2.0 - 1.0) - smooth)
        swell = 0.25 + 0.75 * math.sin(math.pi * 2 * i / count) ** 2
        output.append(0.7 * swell * smooth)
    return output


def media() -> dict[str, bytes]:
    files = {}
    cavern = "examples/dbc/demos/cavern_dash/"
    files[cavern + "engine.wav"] = wav(engine_hum(), 8, 11025)
    files[cavern + "crash.wav"] = wav(crash(), 16)
    vault = "examples/dbc/demos/flooded_vault/"
    files[vault + "drip.wav"] = wav(drip(), 16)
    files[vault + "chime.wav"] = wav(chime(), 16)
    files[vault + "cistern.wav"] = wav(cistern(), 8, 11025)
    frost = "examples/dbc/demos/frost_outpost/"
    files[frost + "pulse.wav"] = wav(sweep(1800.0, 300.0, 0.35, 6.0), 16)
    files[frost + "howl.wav"] = wav(howl(), 16)
    files[frost + "impact.wav"] = wav(noise_burst(0.4, 8.0, 5, 90.0), 16)
    files[frost + "shatter.wav"] = wav(noise_burst(1.2, 4.0, 9, 1400.0), 16)
    files[frost + "punch.wav"] = wav(noise_burst(0.25, 10.0, 11, 60.0), 16)
    files[frost + "wind.wav"] = wav(wind(), 8, 11025)
    racer = "examples/dbc/demos/wave_racer/"
    files[racer + "jet.wav"] = wav(jet(), 8, 11025)
    files[racer + "splash.wav"] = wav(noise_burst(0.9, 5.0, 17, 70.0), 16)
    files[racer + "gate.wav"] = wav(chime(), 16)
    files[racer + "fanfare.wav"] = wav(fanfare(), 16)
    sound = "examples/dbc/tutorials/sound/"
    files[sound + "bell.wav"] = wav(tone(880.0, 1.6, 2.5, harmonics=(1.0, 0.5, 0.25)), 16)
    files[sound + "kick.wav"] = wav(kick(), 16)
    files[sound + "snare.wav"] = wav(noise_burst(0.3, 12.0, 23, 900.0), 16)
    files[sound + "pluck.wav"] = wav(tone(330.0, 0.8, 6.0, harmonics=(1.0, 0.6, 0.3)), 16)
    files[sound + "synth_loop.wav"] = wav(synth_loop(), 16)
    files[sound + "buzz.wav"] = wav(buzz(), 8, 11025)
    armour = "examples/dbc/demos/dune_armour/"
    files[armour + "rumble.wav"] = wav(rumble(), 8, 11025)
    files[armour + "cannon.wav"] = wav(noise_burst(0.7, 6.0, 31, 55.0), 16)
    files[armour + "blast.wav"] = wav(noise_burst(1.4, 3.5, 37, 40.0), 16)
    files[armour + "ping.wav"] = wav(ping(), 16)
    files[armour + "raider_gun.wav"] = wav(noise_burst(0.2, 14.0, 41, 300.0), 16)
    hive = "examples/dbc/demos/hive_defender/"
    files[hive + "laser.wav"] = wav(sweep(2400.0, 900.0, 0.25, 8.0), 16)
    files[hive + "boom.wav"] = wav(noise_burst(0.9, 5.0, 43, 70.0), 16)
    files[hive + "swoop.wav"] = wav(sweep(500.0, 1500.0, 0.6, 2.0), 16)
    files[hive + "hum.wav"] = wav(hum(), 8, 11025)
    files[hive + "hit.wav"] = wav(noise_burst(1.2, 3.0, 47, 50.0), 16)
    rally = "examples/dbpro/demos/dusk_rally/"
    files[rally + "motor.wav"] = wav(motor(), 8, 11025)
    files[rally + "screech.wav"] = wav(screech(), 16)
    files[rally + "surf.wav"] = wav(surf(), 8, 11025)
    files[rally + "beep.wav"] = wav(tone(660.0, 0.3, 6.0, harmonics=(1.0, 0.3, 0.1)), 16)
    files[rally + "go.wav"] = wav(tone(1320.0, 0.7, 3.0, harmonics=(1.0, 0.3, 0.1)), 16)
    files[rally + "lap.wav"] = wav(chime(), 16)
    files[rally + "finish.wav"] = wav(fanfare(), 16)
    files[rally + "bump.wav"] = wav(noise_burst(0.5, 7.0, 73, 60.0), 16)
    music = "examples/dbc/tutorials/music/"
    for name, (notes, tempo) in (("harbour_waltz", harbour_waltz()), ("night_march", night_march())):
        files[music + name + ".mid"] = midi(notes, tempo)
        files[music + name + ".wav"] = wav(render(notes, tempo), 16)
    return files


def main() -> None:
    parser = argparse.ArgumentParser(description="Regenerate the procedural example media")
    parser.add_argument("--check", action="store_true", help="fail when a tracked file is stale")
    args = parser.parse_args()
    generated = media()
    for relative, content in generated.items():
        path = ROOT / relative
        if args.check:
            if not path.is_file() or path.read_bytes() != content:
                raise SystemExit(f"example media is stale: {path}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
    print("Example media is current" if args.check else f"Wrote {len(generated)} example media files")


if __name__ == "__main__":
    main()
