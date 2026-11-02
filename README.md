# Meridian Rack

A mix-bus / mastering "rack" plugin (AU, VST3, Standalone) built with JUCE.
It combines three processors and a loudness station in one hardware-style interface:

| Module | What it does |
|---|---|
| **Unmask EQ** | Adaptive, perception-driven EQ that reveals masked detail. Recover, Tame, Bias, Brighten, Boost, two draggable frequency-range limiters, sidechain analyser input, Live/Normal/Best quality, Recover/Tame/Bias/brightness meters, loudness-compensated output. |
| **Character** | One-knob mix-bus chain: Input meter with target zone, Low/Mid/High pre-EQ, Amount (0-200 %) driving 3-band compression + tone shaping + oversampled saturation/exciter + stereo width, gain-reduction meters with target zones, INT/EXT sidechain, oversampling Off/2x/4x. |
| **Limiter** | Look-ahead true-peak limiter with program-dependent release, Drive and Ceiling, gain-reduction history. |
| **Loudness** | BS.1770-4 / EBU R128 metering (integrated, short-term, momentary, loudness range, true peak), loudness targets, 60-second history, level-matched bypass, and a **reference track** you load from disk: its loudness is compared with your mix line by line, its tonal balance is overlaid on yours, and you can listen to it level-matched with one click. |

Global: Input and Output gain, latency-matched **Bypass** (Shift+click bypasses every instance in the session), factory and user presets, resizable window (60-150 %).

> The adaptive EQ and the bus chain are original designs inspired by the *controls and workflow* of well-known commercial tools. They do not contain or reproduce anyone else's code or algorithms, so they will not sound identical to those products.

---

## Getting the Mac plugin without installing any developer tools

GitHub can compile the plugin for you on one of its Macs (free).

1. **Create a repository.** Sign in at github.com (a free account is fine) and click **New repository**. Give it a name such as `meridian-rack`. Public or Private both work (private repositories have a smaller monthly allowance of free Mac build minutes, still plenty for this).
2. **Upload the project.** Unzip `MeridianRack.zip` on your computer. On the new repository's page click **uploading an existing file**, then drag in *everything inside* the `MeridianRack` folder, including the hidden `.github` folder.
   - On a Mac, press **Cmd + Shift + .** in Finder to show hidden folders before selecting.
   - If `.github` still doesn't upload, add it by hand afterwards: **Add file > Create new file**, type the name `.github/workflows/build-macos.yml`, paste in the contents of that file from the zip, and commit.
3. Click **Commit changes**. Open the **Actions** tab: a run called **Build Mac plugin** starts automatically (or click **Run workflow**). It takes about 10-20 minutes.
4. When it shows a green tick, click the run and download **MeridianRack-macOS** under *Artifacts*. Unzip it (you may need to double-click the inner zip as well).
5. Follow `INSTALL-MAC.txt` inside: one Terminal command copies the AU/VST3 into place and clears macOS's download quarantine (needed because the plugin isn't signed with a paid Apple certificate).

## Building locally (developers)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # downloads JUCE 9.0.4
cmake --build build --target MeridianRack_AU MeridianRack_VST3 MeridianRack_Standalone
```
Outputs land in `build/MeridianRack_artefacts/Release/`.

### Offline tests
```bash
cmake -S . -B build-tests -DMERIDIAN_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --target MeridianTests
./build-tests/MeridianTests_artefacts/Release/MeridianTests ./screenshots
```
Checks loudness accuracy against EBU test signals (44.1/48/96 kHz), gating, loudness range, true peak, perfect reconstruction of the adaptive EQ at zero settings, loudness preservation, reported latency vs. the real signal path, latency-aligned bypass, the limiter ceiling, and reference-file analysis; then renders each page to PNG.

## Controls cheat sheet

- **Knobs**: drag; **Shift**+drag for fine; **Alt**+click to jump between default and your last value (quick A/B); double-click to reset; mouse wheel works.
- **Unmask EQ display**: drag the red limiters to restrict which frequencies are processed (drag them past each other to *exclude* the range between); double-click a limiter to reset it; click-drag on the graph for a frequency/gain readout; scroll or click the dB scale to zoom.
- **Character**: gain-stage so the INPUT RMS bar sits in the green bracket, mix until each GR meter lands in its zone, then set AMOUNT to taste. Click the PEAK readout to reset it.
- **Quality / Oversampling** changes reallocate the engine and change latency; some hosts want playback restarted afterwards.
- **Reference**: LOAD (or drop a WAV/AIFF/FLAC/MP3/M4A onto the Loudness page). LISTEN switches the plugin's output to the reference; MATCH LEVEL plays it at your mix's loudness. The reference path is saved with your session.
- **Sidechain**: in Logic, choose the source from the plugin window's *Side Chain* menu. Unmask's SIDECHAIN analyses that signal; Character's EXT lets it drive the compressor (handy for exporting stems that behave like the full mix; the limiter always reacts to the main input).

## Project layout
```
Source/PluginProcessor.*      signal flow, latency, bypass, state
Source/Parameters.*           all automatable parameters
Source/Presets.*              factory + user presets
Source/dsp/SpectralUnmasker.* adaptive unmasking EQ (STFT, ERB bands, masking model)
Source/dsp/CharacterChain.*   pre-EQ, 3-band compressor, saturation, width
Source/dsp/Limiter.*          true-peak look-ahead limiter
Source/dsp/LoudnessMeter.*    BS.1770-4 meter
Source/dsp/ReferenceTrack.*   reference loading/analysis/playback, tonal analyser
Source/ui/*                   look-and-feel, widgets, EQ display, pages
tests/TestMain.cpp            offline test runner
.github/workflows/            automatic Mac build
```
