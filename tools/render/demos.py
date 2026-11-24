#!/usr/bin/env python3
"""Renders the before/after audio previews for the website.

    python tools/render/demos.py <folder with the four source WAVs> <folder with the built *_render tools>

Source files: Before_Drumloop.wav, Before_Bassloop.wav, Before_Vocal.wav, Before_Mix.wav
Writes docs/audio/before/<clip>.mp3, docs/audio/<plugin>/<clip>.mp3 and scripts/site/demos.json
(which build.py reads for the players). Change the settings below and run it again.

Level: every clip is first set to TARGET_LUFS (about where a stem or a mix bus sits while mixing),
and that is what the plugins get, so presets and drive behave like they would in a session.
Loudness: all "before" clips are set to the same loudness (TARGET_LUFS). For plugins whose job is
not loudness, each "after" is matched to its "before", so you hear the character, not a volume jump.
For PEAKSLAYER and MASTERFUSE the real level change is kept; the player has a switch to match it.
"""
import json
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
import pyloudnorm
import soundfile as sf

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
DOCS = REPO / "docs"
TARGET_LUFS = -18.5
PEAK_LIMIT_DB = -1.0     # headroom for the MP3 encoding

CLIPS = {   # id: (file, label)
    "drums": ("Before_Drumloop.wav", "Drums"),
    "bass": ("Before_Bassloop.wav", "Bass"),
    "vocal": ("Before_Vocal.wav", "Vocal"),
    "mix": ("Before_Mix.wav", "Mix"),
}

# plugin: (tool, keep the level change?, {clip: (label shown in the player, renderer arguments)})
SETTINGS = {
    "peakslayer": ("peakslayer", True, {
        "drums": ("Preset: Hard Slam", ["--preset", "Hard Slam"]),
        "bass": ("Preset: Balanced", ["--preset", "Balanced"]),
        "vocal": ("Preset: Soft Glue", ["--preset", "Soft Glue"]),
        "mix": ("Preset: Balanced", ["--preset", "Balanced"]),
    }),
    "krush": ("krush", False, {
        "drums": ("Preset: Hip Hop Punch", ["--preset", "Hip Hop Punch"]),
        "bass": ("Preset: Diode Fuzz", ["--preset", "Diode Fuzz"]),
        "vocal": ("Preset: Tube Amp", ["--preset", "Tube Amp"]),
        "mix": ("Preset: Clean Tape", ["--preset", "Clean Tape"]),
    }),
    "masterfuse": ("masterfuse", True, {
        "drums": ("AMOUNT 70%", ["--set", "amount=70"]),
        "bass": ("AMOUNT 60%", ["--set", "amount=60"]),
        "vocal": ("AMOUNT 50%", ["--set", "amount=50"]),
        "mix": ("AMOUNT 60%", ["--set", "amount=60"]),
    }),
    "spectrakill": ("spectrakill", False, {
        "drums": ("Preset: Drum Detail", ["--preset", "Drum Detail"]),
        "bass": ("Preset: Bass Control · THRESHOLD −12 dB", ["--preset", "Bass Control", "--set", "threshold=-12"]),
        "vocal": ("Preset: Upward Detail", ["--preset", "Upward Detail"]),
        "mix": ("Preset: Full Mix Glue", ["--preset", "Full Mix Glue"]),
    }),
    "dark": ("dark", False, {
        "drums": ("DARKNESS 65% · PUNCH 70%", ["--set", "amount=65", "--set", "punch=70"]),
        "bass": ("DARKNESS 70% · PUNCH 30%", ["--set", "amount=70", "--set", "punch=30"]),
        "vocal": ("DARKNESS 50% · PUNCH 60%", ["--set", "amount=50"]),
        "mix": ("DARKNESS 55% · PUNCH 60%", ["--set", "amount=55"]),
    }),
    "shrapnel": ("shrapnel", False, {
        "drums": ("Preset: Metallic Drums · DRIVE 10%", ["--preset", "Metallic Drums", "--set", "drive=10"]),
        "bass": ("Bell Shift · WARP · DRIVE 5% · STRETCH 100%",
                 ["--preset", "Bell Shift", "--set", "mode=5", "--set", "drive=5", "--set", "character=100",
                  "--set", "tone=45", "--set", "tonal=90", "--set", "noise=10", "--set", "transient=0",
                  "--set", "dynamic=50", "--set", "mix=100", "--set", "gain=0", "--set", "quality=1",
                  "--set", "stereo=0", "--set", "clip=1"]),
        "vocal": ("Preset: Vocal Grit", ["--preset", "Vocal Grit"]),
        "mix": ("HARMONICS · DRIVE 100% · MIX 70% · sub spared below 80 Hz",
                ["--preset", "Default", "--set", "drive=100", "--set", "character=50", "--set", "tone=6",
                 "--set", "tonal=90", "--set", "noise=50", "--set", "transient=20", "--set", "dynamic=50",
                 "--set", "mix=70", "--set", "gain=2", "--set", "quality=2", "--set", "stereo=0", "--set", "clip=1",
                 "--point", "80=-1"]),
    }),
}


def lufs(x, sr):
    return pyloudnorm.Meter(sr).integrated_loudness(x)


def peaks(x, n=240):
    """Waveform outline for the player: n bars of peak level, 0-99."""
    m = np.abs(x).max(axis=1)
    edges = np.linspace(0, len(m), n + 1).astype(int)
    p = np.array([m[a:b].max() if b > a else 0 for a, b in zip(edges[:-1], edges[1:])])
    return [int(v) for v in np.clip(np.round(p * 99), 0, 99)]


def mp3(x, sr, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = TMP / "encode.wav"          # a plain file: Windows can't reopen a NamedTemporaryFile
    sf.write(str(tmp), x, sr, subtype="FLOAT")
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(tmp), "-ar", "44100", "-c:a", "libmp3lame",
                    "-b:a", "192k", "-id3v2_version", "0", "-map_metadata", "-1", str(dest)], check=True)


TMP = pathlib.Path(tempfile.mkdtemp(prefix="nd-demos-"))

# RECOIL is a sidechain: its clips are the drum loop and the bass loop playing together. "Before" is the two
# summed; "after" is the drums plus the bass run through RECOIL with the drums on its sidechain.
# The real level change is kept (ducking makes the sum a little quieter); the player can match it.
RECOIL_BPM = "72.5"    # the demo drum loop's tempo, so GROOVE LOCK hears a song playing in time
RECOIL_CLIPS = {   # id: (tab label, label shown in the player, renderer arguments)
    "rcdefault": ("Default", "Preset: Default (load it, send the kick)", ["--preset", "Default", "--bpm", RECOIL_BPM]),
    "rcblack": ("Blackout", "Preset: BLACKOUT", ["--preset", "BLACKOUT", "--bpm", RECOIL_BPM]),
}


def recoil_demos(out, tools):
    drums, sr = sf.read(TMP / "drums.wav", always_2d=True)
    bass, _ = sf.read(TMP / "bass.wav", always_2d=True)
    n = min(len(drums), len(bass))
    drums, bass = drums[:n], bass[:n]
    before = drums + bass
    g = 10 ** ((TARGET_LUFS - lufs(before, sr)) / 20)
    drums, bass, before = drums * g, bass * g, before * g
    sf.write(str(TMP / "rc_bass.wav"), bass, sr, subtype="FLOAT")
    sf.write(str(TMP / "rc_drums.wav"), drums, sr, subtype="FLOAT")
    exe = tools / ("recoil_render.exe" if sys.platform == "win32" else "recoil_render")
    if not exe.exists() and (tools / "Release" / exe.name).exists():
        exe = tools / "Release" / exe.name
    entry = {"keepLevel": True, "clips": {}}
    for cid, (tab, label, args) in RECOIL_CLIPS.items():
        out["clips"].append({"id": cid, "label": tab})
        mp3(before, sr, DOCS / "audio" / "before" / f"{cid}.mp3")
        raw = TMP / f"recoil_{cid}.wav"
        subprocess.run([str(exe), str(TMP / "rc_bass.wav"), str(raw), "--sidechain", str(TMP / "rc_drums.wav")] + args,
                       check=True, capture_output=True)
        y, _ = sf.read(raw, always_2d=True)
        after = drums + y[:n]
        pk = 20 * np.log10(np.abs(after).max() + 1e-12)
        if pk > PEAK_LIMIT_DB:
            after = after * 10 ** ((PEAK_LIMIT_DB - pk) / 20)
        mp3(after, sr, DOCS / "audio" / "recoil" / f"{cid}.mp3")
        entry["clips"][cid] = {"setting": label, "louder": round(lufs(after, sr) - lufs(before, sr), 1),
                               "wb": peaks(before), "wa": peaks(after)}
        print(f"recoil/{cid}: {label}; heard {entry['clips'][cid]['louder']:+.1f} LU")
    out["plugins"]["recoil"] = entry


def main(src, tools):
    src, tools = pathlib.Path(src), pathlib.Path(tools)
    out = {"clips": [{"id": k, "label": v[1]} for k, v in CLIPS.items()], "plugins": {}}
    before = {}
    for cid, (fname, _) in CLIPS.items():
        x, sr = sf.read(src / fname, always_2d=True)
        g = 10 ** ((TARGET_LUFS - lufs(x, sr)) / 20)
        x = x * g
        pk = 20 * np.log10(np.abs(x).max())
        if pk > PEAK_LIMIT_DB:
            sys.exit(f"{cid}: peak {pk:.1f} dB after loudness setting; lower TARGET_LUFS")
        before[cid] = (x, sr, g)
        sf.write(str(TMP / f"{cid}.wav"), x, sr, subtype="FLOAT")
        mp3(x, sr, DOCS / "audio" / "before" / f"{cid}.mp3")
        print(f"before/{cid}: {len(x) / sr:.1f} s, gain {20 * np.log10(g):+.1f} dB, peak {pk:.1f} dB")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        for slug, (tool, keep_level, clips) in SETTINGS.items():
            entry = {"keepLevel": keep_level, "clips": {}}
            for cid, (label, args) in clips.items():
                x, sr, g = before[cid]
                raw = tmp / f"{slug}_{cid}.wav"
                exe = tools / (f"{tool}_render.exe" if sys.platform == "win32" else f"{tool}_render")
                if not exe.exists() and (tools / "Release" / exe.name).exists():   # Visual Studio puts it in bin/Release
                    exe = tools / "Release" / exe.name
                subprocess.run([str(exe), str(TMP / f"{cid}.wav"), str(raw)] + args,
                               check=True, capture_output=True)
                y, _ = sf.read(raw, always_2d=True)
                diff = lufs(y, sr) - lufs(x, sr)
                if not keep_level:
                    y = y / 10 ** (diff / 20)
                pk = 20 * np.log10(np.abs(y).max() + 1e-12)
                if pk > PEAK_LIMIT_DB:
                    y = y * 10 ** ((PEAK_LIMIT_DB - pk) / 20)
                after_lufs = lufs(y, sr)
                mp3(y, sr, DOCS / "audio" / slug / f"{cid}.mp3")
                entry["clips"][cid] = {
                    "setting": label,
                    "louder": round(after_lufs - lufs(x, sr), 1),      # dB louder than "before" as heard
                    "wb": peaks(x), "wa": peaks(y),
                }
                print(f"{slug}/{cid}: {label}; change {diff:+.1f} LU, heard {after_lufs - lufs(x, sr):+.1f} LU, peak {20 * np.log10(np.abs(y).max()):.1f} dB")
            out["plugins"][slug] = entry
    # RECOIL's and PYRE's players come from DAW bounces (bounces.py): keep them
    old = json.loads((REPO / "scripts" / "site" / "demos.json").read_text()) if (REPO / "scripts" / "site" / "demos.json").exists() else {}
    for plugin, prefix in (("recoil", "rc"), ("pyre", "py")):
        if plugin in old.get("plugins", {}):
            out["clips"] += [c for c in old["clips"] if c["id"].startswith(prefix)]
            out["plugins"][plugin] = old["plugins"][plugin]
        elif plugin == "recoil":
            recoil_demos(out, tools)
    (REPO / "scripts" / "site" / "demos.json").write_text(json.dumps(out, separators=(",", ":")))
    print("wrote scripts/site/demos.json")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
