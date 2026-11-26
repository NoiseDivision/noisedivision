# Noise Division

Free audio plugins: **PEAKSLAYER** (soft clipper), **KRUSH** (multiband distortion),
**MASTERFUSE** (one-knob mastering), **SPECTRAKILL** (spectral compressor), **DARK** (one-knob darkener for drums and bass), **SHRAPNEL** (spectral distortion), **RECOIL** (the sidechain weapon: zero-latency ducking with a song map), **PYRE** (harmonic loudness: leads and synths as dense as a saw, driven into a soft ceiling) and **FLATLINE** (clean loudness: a clipper whose MASK mode hides its distortion under the music, an upward compressor and a lookahead limiter for the master, with LEARN to hit a LUFS target). VST3 and Standalone on Windows; AU, VST3 and Standalone on macOS.

```
plugins/            the nine JUCE projects (each builds on its own with CMake)
docs/               the website (GitHub Pages serves this folder)
scripts/            packaging: Windows .zip and macOS .pkg installers
.github/workflows/  builds everything on GitHub's Windows and Mac machines
```

## Publish a new version

1. Bump `project(... VERSION x.y.z)` in each `plugins/*/CMakeLists.txt` you changed.
2. Create a tag named `v` + the version, e.g. `v1.0.1`, and push it
   (GitHub Desktop: Repository > Create tag... then Push origin;
   or on github.com: Releases > Draft a new release > choose a new tag > Publish).
   Pushing just the tag is best: the release is then created at the end of the build, with all its files,
   so the website's download links never point at an empty release. If you publish the release on
   github.com first, the workflow points "latest" back at the previous release until the files are in.
3. The **Build plugins** workflow builds Windows and macOS, then attaches these files to that release:

   | Plugin     | Windows                  | macOS                  |
   |------------|--------------------------|------------------------|
   | PEAKSLAYER | `PEAKSLAYER-Windows.zip` | `PEAKSLAYER-macOS.pkg` |
   | KRUSH      | `KRUSH-Windows.zip`      | `KRUSH-macOS.pkg`      |
   | MASTERFUSE | `MASTERFUSE-Windows.zip` | `MASTERFUSE-macOS.pkg` |
   | SPECTRAKILL | `SPECTRAKILL-Windows.zip` | `SPECTRAKILL-macOS.pkg` |
   | DARK       | `DARK-Windows.zip`       | `DARK-macOS.pkg`       |
   | SHRAPNEL   | `SHRAPNEL-Windows.zip`   | `SHRAPNEL-macOS.pkg`   |
   | RECOIL     | `RECOIL-Windows.zip`     | `RECOIL-macOS.pkg`     |
   | PYRE       | `PYRE-Windows.zip`       | `PYRE-macOS.pkg`       |
   | FLATLINE   | `FLATLINE-Windows.zip`   | `FLATLINE-macOS.pkg`   |
   | Full suite | `NoiseDivision-Suite-Windows.zip` | `NoiseDivision-Suite-macOS.pkg` |

   The suite holds every plugin: on Windows one folder per plugin, on macOS one installer where
   Customize lets people untick any plugin. To add a plugin, add it to `scripts/plugins.sh` and the
   workflow's build list; both suites pick it up automatically.

   Only the plugins that changed are compiled, on Windows and macOS alike. Each plugin's finished
   build is kept on a hidden pre-release called **build-cache**, named after the fingerprint of its
   `plugins/<NAME>/` folder, and reused until a file in that folder changes. Editing the workflow or
   another plugin doesn't trigger a rebuild, and the saved builds don't expire. Every release still
   has every plugin and both suites. Leave build-cache in place (it's never the "latest" release the
   site links to); if it's deleted, everything compiles once more. To force a full rebuild:
   Actions > Build plugins > Run workflow, tick **Rebuild every plugin**.

The website's buttons link to `releases/latest/download/<file>`, so they always point at the
newest release. `GITHUB_REPO`, the donation links and the download file names are at the top of `docs/assets/site.js`.

To try a build without publishing: Actions tab > Build plugins > Run workflow. The files appear
under **Artifacts** on the run's page.

## macOS signing

The Mac installers are not signed with an Apple Developer ID, so the first time, macOS asks the user
to allow the installer (System Settings > Privacy & Security > Open Anyway). The website explains this.
With an Apple Developer account ($99/year) the installers can be signed and notarized so that
step disappears. Signing would be added to `scripts/package-mac.sh` (codesign with the Developer ID, then notarytool).

## Build on your own computer

```
cmake -S plugins/KRUSH -B build/KRUSH
cmake --build build/KRUSH --config Release
```

## The website

`docs/` is generated, apart from `docs/assets/site.css` (the look), `docs/assets/site.js` (settings and
the menu), `docs/img/` and `CNAME`. To change the text, edit `scripts/site/content.py`, then run:

```
pip install playwright pypdf pillow
python -m playwright install chromium

python scripts/site/build.py      # all pages and sitemap.xml
python scripts/site/manuals.py    # the PDF manuals in docs/manuals/
python scripts/site/og.py         # the share images in docs/img/og/ (only when a screenshot changes)
```

```
docs/index.html                  home: all plugins as cards
docs/plugins/<name>/index.html   one page per plugin
docs/contact/  docs/donate/      contact and donate pages
docs/404.html                    shown for any address that doesn't exist
docs/manuals/<NAME>-User-Manual.pdf
```

**Adding a plugin:** add a screenshot to `docs/img/`, an entry to `PLUGINS` in `content.py`, its file names to
`FILES` in `site.js`, and run the three scripts. The menu, home page, footer, sitemap and "more plugins"
strips pick it up automatically. (The signal-chain graphic on the home page lists every plugin; add a node to
`trace()` in `build.py` for a new one.)

To preview locally: `cd docs` then `python -m http.server`, and open http://localhost:8000.

## Before/after audio on the plugin pages

`tools/render/` builds a small command-line renderer for each plugin from its own source code, and
`demos.py` runs the four sample clips through them:

```
cmake -S tools/render -B tools/render/build -DCMAKE_BUILD_TYPE=Release
cmake --build tools/render/build
python tools/render/demos.py <folder with Before_Drumloop.wav, Before_Bassloop.wav, Before_Vocal.wav, Before_Mix.wav> tools/render/build/bin
python scripts/site/build.py
```

The settings used for each plugin and clip are at the top of `demos.py`. RECOIL's and PYRE's players are bounced from a real session instead (a sidechain, a song map or a whole mix):
`python tools/render/bounces.py recoil|pyre <folder with the bounces>` encodes them (the expected file names are at the top of that script), and `demos.py` keeps them. It writes the MP3s to
`docs/audio/` and the player data to `scripts/site/demos.json`. (Needs `pip install numpy soundfile pyloudnorm`
and ffmpeg.) Render again whenever a plugin's sound changes.
