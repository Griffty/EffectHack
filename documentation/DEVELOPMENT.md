# Developer guide

[Project overview](../README.md) · [Architecture and contracts](ARCHITECTURE.md)

## Build and checks

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

CMake pins JUCE's source revision and archive checksum. The first configure requires network access; the application links JUCE audio utilities/DSP and libcurl. MP3 decoding is explicitly enabled. JUCE WebBrowser and its curl integration are disabled; the project's `HttpClient` owns HTTPS requests.

In CLion, reload CMake and run the **EffectHack** application target. Linux is the verified target. Do not describe a Windows build as supported until its dependencies, devices, and UI have been exercised.

CTest uses synthetic signals and injected service responses. It does not require guitar hardware, an API key, or live HTTP. UI integration tests initialize JUCE and simulate callbacks; their Linux build disables ALSA device probing.

| Change | Focused check |
| --- | --- |
| Distortion | `ctest --test-dir build -R '^distortion_dsp$' --output-on-failure` |
| Input playback or dry phrase | `ctest --test-dir build -R '^virtual_input$' --output-on-failure` |
| Amp, cabinet, EQ, gate, compressor | `ctest --test-dir build -R '^cabinet_eq$' --output-on-failure` |
| Chorus, delay, reverb | `ctest --test-dir build -R '^chorus_delay_reverb$' --output-on-failure` |
| Presets, A/B state, order, startup, generated preparation | `ctest --test-dir build -R '^presets$' --output-on-failure` |
| Master controls and transport | `ctest --test-dir build -R '^master_playback$' --output-on-failure` |
| Tuner, metering, load metrics | `ctest --test-dir build -R '^tuner_input_performance$' --output-on-failure` |
| Gemini configuration and legacy recommendation boundary | `ctest --test-dir build -R '^song_recommendation$' --output-on-failure` |
| Media matching, plan compilation, review | `ctest --test-dir build -R '^audio_tone_matching$' --output-on-failure` |
| UI shortcuts, slots, phrase controls | `ctest --test-dir build -R '^rig_keyboard_slots$' --output-on-failure` |

Rebuild affected targets before running tests. Use the full suite after changes to shared rack definitions, state formats, or routing.

## Exercise matching without the GUI

The probe is excluded from the default build:

```sh
cmake --build build --target EffectHackToneProbe --parallel 2

# Actual YouTube matching; requires Gemini credentials and network access.
./build/EffectHackToneProbe youtube \
  'https://www.youtube.com/watch?v=1-3efFGEnr8' \
  /tmp/nana-tone.json intro 0:00 0:25

# Compare a selected local reference section.
./build/EffectHackToneProbe file \
  /absolute/path/reference.mp3 /tmp/lead-tone.json lead 0:30 1:00

# Name-only estimate, without reference audio.
./build/EffectHackToneProbe name 'Artist - Song' /tmp/estimated-tone.json rhythm

# Render the processor's examples locally; no Gemini call.
./build/EffectHackToneProbe --examples /tmp/effecthack-examples
```

The JSON output contains the analysis plan, full rack, prepared preset, model/source labels, rationale, warnings, and headroom adjustment. The probe renders offline and opens no audio device. Use absolute file/output paths. `--examples` writes WAV files that can be auditioned to inspect the model's reference vocabulary.

## Add or change an effect

Keep each effect and its parameters in a dedicated `effects/Name.h` / `effects/Name.cpp` pair; put effect-specific tests in their own file.

1. Choose a mono processor wrapped by `ChannelWiseRackStage`, or a true stereo `RackStage`/`StereoRackStage`. Never collapse an existing stereo rack signal to mono inside a stage.
2. Put allocation and sample-rate preparation in `prepare()`. Keep processing bounded and allocation-free. Smooth audible parameter and bypass transitions; report latency consistently, including bypass.
3. Register the stage in `AudioEngine`. Update IDs/order and parameter definitions in `RackDefinition.h`, plus UI controls and their bindings.
4. Update `RackState`, capture/apply, serialization and migrations, generated-rack schema/validation, factories, and A/B/session handling. Old presets must acquire intentional defaults for new controls.
5. Revisit matching anchors and plan compilation. Reordering or adding nonlinear stages may invalidate internal level-balancing assumptions.
6. Add CMake sources and tests for bypass, reset, channel isolation, automation, multiple sample rates, invalid inputs, and complete-rack behavior as appropriate.

Existing tests show the conventions: `RackOrderTests.cpp` checks ordering/stereo preservation; `AmpTests.cpp`, `NoiseGateTests.cpp`, and `CompressorTests.cpp` exercise isolated processors as well as routing. `GeneratedToneTests.cpp` checks preparation and silent commits.

## Work on matching

Keep three boundaries distinct:

- **Model contract:** `ToneAnalysis` builds requests and validates compact plans against known anchors.
- **Deterministic compilation:** `compileTonePlan()` creates a full rack from a deep-copied anchor. Model output never writes directly to live atomics.
- **Preparation/application:** `GeneratedTonePreparation` checks rendered output; `MainComponentSong` commits only after a fade to silence.

Inject `HttpTransport` for tests. Cover malformed or unknown fields, invalid ranges, missing models, failed second-pass review, cancellation, and preservation of Master gains. Do not put credentials or live service dependencies in automated tests.

## Documentation conventions

Public headers explain ownership, units, threading, and preconditions. Implementation comments explain non-obvious invariants (for example, shared `juce::var` objects or why a rack commit waits for silence). Avoid comments that merely repeat a function name.

Keep the root README focused on running and using the app. Put player details in [USAGE.md](USAGE.md), cross-module design in [ARCHITECTURE.md](ARCHITECTURE.md), and contributor steps here. Update these when behavior or persistence contracts change.
