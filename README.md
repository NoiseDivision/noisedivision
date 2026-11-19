# Noise Division

Free audio plugins: **PEAKSLAYER** (soft clipper), **KRUSH** (multiband distortion),
**MASTERFUSE** (one-knob mastering), **SPECTRAKILL** (spectral compressor) and **DARK** (one-knob darkener for drums and bass). VST3 and Standalone on Windows; AU, VST3 and Standalone on macOS.

```
plugins/            the five JUCE projects (each builds on its own with CMake)
docs/               the website (GitHub Pages serves this folder)
scripts/            packaging: Windows .zip and macOS .pkg installers
.github/workflows/  builds everything on GitHub's Windows and Mac machines
```

## Publish a new version

1. Bump `project(... VERSION x.y.z)` in each `plugins/*/CMakeLists.txt` you changed.
2. Create a tag named `v` + the version, e.g. `v1.0.1`, and push it
   (GitHub Desktop: Repository > Create tag... then Push origin;
   or on github.com: Releases > Draft a new release > choose a new tag > Publish).
3. The **Build plugins** workflow builds Windows and macOS, then attaches these files to that release:

   | Plugin     | Windows                  | macOS                  |
   |------------|--------------------------|------------------------|
   | PEAKSLAYER | `PEAKSLAYER-Windows.zip` | `PEAKSLAYER-macOS.pkg` |
   | KRUSH      | `KRUSH-Windows.zip`      | `KRUSH-macOS.pkg`      |
   | MASTERFUSE | `MASTERFUSE-Windows.zip` | `MASTERFUSE-macOS.pkg` |
   | SPECTRAKILL | `SPECTRAKILL-Windows.zip` | `SPECTRAKILL-macOS.pkg` |
   | DARK       | `DARK-Windows.zip`       | `DARK-macOS.pkg`       |

The website's buttons link to `releases/latest/download/<file>`, so they always point at the
newest release. Set `GITHUB_REPO` near the bottom of `docs/index.html` once.

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
