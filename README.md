# Curve EQ (VST3)

A 24-band parametric EQ built with JUCE, in the spirit of FabFilter Pro-Q 3.

## Features
- 24 bands: bell, low/high shelf, low/high cut (6-96 dB/oct), notch, band-pass
- Per-band channel routing: Stereo, Left, Right, Mid, Side
- Per-band dynamic EQ: range, threshold, attack, release, trigger above/below
- Zero Latency (minimum phase) and Linear Phase modes (Low/Medium/High/Max quality; latency is reported to the host)
- Smoothed parameters (no zipper noise or clicks)
- Real-time spectrum analyzer, piano strip, note-aware tooltips, output gain

## Interaction
Double-click graph: add band. Drag node: frequency/gain (hold Shift for fine). Scroll over node: Q.
Right-click node: change shape / delete. Double-click node or press Delete: remove band.

## Build (Windows)
Needs CMake 3.22+ and Visual Studio 2022 (Desktop development with C++):

    cmake -B build
    cmake --build build --config Release

Output: `build\CurveEQ_artefacts\Release\VST3\Curve EQ.vst3`
Install: copy that folder to `C:\Program Files\Common Files\VST3`.
No compiler? Push to GitHub: the included Actions workflow builds the Windows VST3 for you (download it from the Actions tab, artifact `CurveEQ-windows`).

## Tests
`cmake -B build -DCURVEEQ_BUILD_TESTS=ON && cmake --build build --target EqTest && ./build/EqTest`
Headless checks of filters, mid/side, smoothing, dynamics and linear-phase convolution.
