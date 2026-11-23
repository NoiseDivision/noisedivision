#!/usr/bin/env python3
"""Before/after players for RECOIL's page, from bounces made in a DAW.

    python tools/render/recoil_bounces.py <folder with the bounces>

RECOIL works on a whole song (a drum bus on its sidechain, a map of the song), so its demos are bounced from a real
session instead of rendered from the four sample clips. Expected files (any of WAV, FLAC, AIFF):

    Mix_before_sidechain       the mix without RECOIL
    Mix_audio_default          the same mix, RECOIL in AUDIO mode, Default preset
    Mix_MAP_Per_Bass_Default   the same mix, RECOIL in MAP mode, Drum Bus Map preset (PER BASS)
    Kick_808_before            kick and 808 without RECOIL
    Kick_808__808_tail_full_band          808 Tail preset, BAND on FULL
    Kick_808__Kickto808_punch_through_long Kick -> 808 preset with the Punch-Through Long shape

Every "before" is set to TARGET_LUFS and its "afters" get the same gain, so the real level change of the ducking
is kept (the player can match it). Writes docs/audio/before/<id>.mp3, docs/audio/recoil/<id>.mp3 and the RECOIL
entry of scripts/site/demos.json. demos.py keeps that entry when it renders the other plugins.
"""
import json
import pathlib
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from demos import DOCS, REPO, TARGET_LUFS, PEAK_LIMIT_DB, lufs, peaks, mp3   # noqa: E402

# id: (tab label, setting shown in the player, before file, after file)
CLIPS = {
    "rc808punch": ("808 · Punch", "Preset: Kick -> 808 · shape Punch-Through Long",
                   "Kick_808_before", "Kick_808__Kickto808_punch_through_long"),
    "rc808tail": ("808 · Tail", "Preset: 808 Tail · BAND FULL",
                  "Kick_808_before", "Kick_808__808_tail_full_band"),
    "rcmix": ("Mix · AUDIO", "AUDIO trigger · Preset: Default (load it, send the kick)",
              "Mix_before_sidechain", "Mix_audio_default"),
    "rcmap": ("Mix · MAP", "MAP trigger · Preset: Drum Bus Map with the bass bus (PER BASS)",
              "Mix_before_sidechain", "Mix_MAP_Per_Bass_Default"),
}


def find(folder, stem):
    hits = [f for f in folder.iterdir() if f.suffix.lower() in (".wav", ".flac", ".aif", ".aiff") and f.stem.endswith(stem)]
    if not hits:
        sys.exit(f"missing: {stem}.wav/.flac in {folder}")
    return hits[0]


def main(folder):
    folder = pathlib.Path(folder)
    path = REPO / "scripts" / "site" / "demos.json"
    out = json.loads(path.read_text())
    out["clips"] = [c for c in out["clips"] if not c["id"].startswith("rc")]
    entry = {"keepLevel": True, "clips": {}}
    for cid, (tab, label, bname, aname) in CLIPS.items():
        before, sr = sf.read(find(folder, bname), always_2d=True)
        after, sr2 = sf.read(find(folder, aname), always_2d=True)
        assert sr == sr2, f"{cid}: sample rates differ"
        n = min(len(before), len(after))
        before, after = before[:n], after[:n]
        g = 10 ** ((TARGET_LUFS - lufs(before, sr)) / 20)
        before, after = before * g, after * g
        pk = 20 * np.log10(max(np.abs(before).max(), np.abs(after).max()) + 1e-12)
        if pk > PEAK_LIMIT_DB:
            sys.exit(f"{cid}: peak {pk:.1f} dB after loudness setting; lower TARGET_LUFS")
        mp3(before, sr, DOCS / "audio" / "before" / f"{cid}.mp3")
        mp3(after, sr, DOCS / "audio" / "recoil" / f"{cid}.mp3")
        out["clips"].append({"id": cid, "label": tab})
        entry["clips"][cid] = {"setting": label, "louder": round(lufs(after, sr) - lufs(before, sr), 1),
                               "wb": peaks(before), "wa": peaks(after)}
        print(f"recoil/{cid}: {label}; heard {entry['clips'][cid]['louder']:+.1f} LU")
    out["plugins"]["recoil"] = entry
    path.write_text(json.dumps(out, separators=(",", ":")))
    print("wrote scripts/site/demos.json")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1])
