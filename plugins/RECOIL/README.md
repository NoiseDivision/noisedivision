# RECOIL 2

The sidechain weapon by Noise Division: shape-based volume ducking with zero latency. VST3, AU and standalone, Windows and macOS.

- **Triggers:** SYNC (locked to the host's song position, rates 1/1 to 1/32, dotted and triplet, offset, swing, AUTO OFFSET),
  MIDI (every note-on restarts the shape; any note or one note; velocity into DYNAMIC) and AUDIO (the sidechain, listening to
  the kick's low end, with GROOVE LOCK starting each duck a few ms before a learned kick and FIT LENGTH sizing the shape to the kick).
- **Shapes:** 16 factory shapes, 4 RECOIL shapes (Blackout, 808 Tail, Recoil, Click Gap), your own in a USER bank, and an editor
  on the display (points, bends, S-curves, draw, step, snap, mirror).
- **Clean:** every jump is crossfaded over ATTACK (0.8 ms keeps splatter 70 dB down); untouched audio is bit-exact in FULL band.
- **BAND:** duck only part of the spectrum (INSIDE / ABOVE amounts), Linkwitz-Riley split.
- **PHASE:** one rotation of the bass through a 90-degree all-pass pair; AUTO picks the angle that lines the bass up with the
  kick's low end where they overlap (closed form, theta = -atan2(SB, SA)) and only moves while the bass is ducked.

See `Source/RecoilEngine.h` for the engine.

Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`
(JUCE 8.0.4 is downloaded automatically). The Audio Unit is a "music effect" (aumf) because it accepts MIDI.
