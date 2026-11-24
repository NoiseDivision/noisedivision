#!/usr/bin/env python3
"""Before/after players from bounces made in a DAW (RECOIL and PYRE).

    python tools/render/bounces.py recoil <folder with the bounces>
    python tools/render/bounces.py pyre <folder with the bounces>

RECOIL works on a whole song (a drum bus on its sidechain, a map of the song), and PYRE's demos are a mix and a lead
played in a session, so their players are bounced, not rendered from the four sample clips. The file names each
one expects are in BOUNCES below (any of WAV, FLAC, AIFF; a prefix before the name is fine).

Every "before" is set to TARGET_LUFS and its "afters" get the same gain, so the real level change is kept (the
player can match it). Writes docs/audio/before/<id>.mp3, docs/audio/<plugin>/<id>.mp3 and the plugin's entry in
scripts/site/demos.json. demos.py keeps these entries when it renders the other plugins.
"""
import json
import pathlib
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from demos import DOCS, REPO, TARGET_LUFS, PEAK_LIMIT_DB, lufs, peaks, mp3   # noqa: E402

# plugin: { id: (tab label, setting shown in the player, before file, after file) }. Ids start with the plugin's prefix.
BOUNCES = {
    "recoil": {
        "rc808punch": ("808 · Punch", "Preset: Kick -> 808 · shape Punch-Through Long",
                       "Kick_808_before", "Kick_808__Kickto808_punch_through_long"),
        "rc808tail": ("808 · Tail", "Preset: 808 Tail · BAND FULL",
                      "Kick_808_before", "Kick_808__808_tail_full_band"),
        "rcmix": ("Mix · AUDIO", "AUDIO trigger · Preset: Default (load it, send the kick)",
                  "Mix_before_sidechain", "Mix_audio_default"),
        "rcmap": ("Mix · MAP", "MAP trigger · Preset: Drum Bus Map with the bass bus (PER BASS)",
                  "Mix_before_sidechain", "Mix_MAP_Per_Bass_Default"),
    },
    "pyre": {
        "pylead": ("Lead", "IGNITE 15% · HEAT 15% · TILT -6 dB/oct · BURN +3 dB",
                   "Pyre_lead_before", "Pyre_lead_after"),
        "pymix": ("Mix", "IGNITE 5% · HEAT 10% · TILT -2 dB/oct · BURN 0 dB",
                  "Pyre_before", "Pyre_after"),
    },
}
PREFIX = {"recoil": "rc", "pyre": "py"}


def find(folder, stem):
    hits = [f for f in folder.iterdir() if f.suffix.lower() in (".wav", ".flac", ".aif", ".aiff") and f.stem.endswith(stem)]
    if not hits:
        sys.exit(f"missing: {stem}.wav/.flac in {folder}")
    return hits[0]


def main(plugin, folder):
    folder = pathlib.Path(folder)
    path = REPO / "scripts" / "site" / "demos.json"
    out = json.loads(path.read_text())
    out["clips"] = [c for c in out["clips"] if not c["id"].startswith(PREFIX[plugin])]
    entry = {"keepLevel": True, "clips": {}}
    for cid, (tab, label, bname, aname) in BOUNCES[plugin].items():
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
        mp3(after, sr, DOCS / "audio" / plugin / f"{cid}.mp3")
        out["clips"].append({"id": cid, "label": tab})
        entry["clips"][cid] = {"setting": label, "louder": round(lufs(after, sr) - lufs(before, sr), 1),
                               "wb": peaks(before), "wa": peaks(after)}
        print(f"{plugin}/{cid}: {label}; heard {entry['clips'][cid]['louder']:+.1f} LU")
    out["plugins"][plugin] = entry
    path.write_text(json.dumps(out, separators=(",", ":")))
    print("wrote scripts/site/demos.json")


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in BOUNCES:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
