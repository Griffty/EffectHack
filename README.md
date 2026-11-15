# EffectHack

A standalone C++ guitar-effects processor for Linux, using JUCE. The rack contains
a noise gate, compressor, distortion, generic amp, cabinet simulation, three-band EQ, chorus, delay, and reverb. Windows can use the same sources and CMake project;
the current implementation has been built on Linux.

## Build

Requires CMake 3.22+, a C++20 compiler, and the Linux development packages for ALSA,
FreeType/Fontconfig, X11/Xrandr/Xinerama/Xcursor, and OpenGL, plus libcurl 7.85+ development files for HTTPS. JUCE is downloaded on the
first configure; its source revision and checksum are pinned in CMakeLists.txt.
JUCE's browser and curl features are disabled, so WebKit is not required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/EffectHack_artefacts/Release/EffectHack
```

In CLion, reload the CMake project and select the **EffectHack** application target.
The console-only **EffectHackDspTests**, **EffectHackSourceTests**, and
**EffectHackToneTests**, **EffectHackSpatialTests**, **EffectHackPresetTests**, **EffectHackMasterTests**, and **EffectHackRigToolsTests** targets check distortion, source/playback, cabinet/EQ, spatial effects, preset persistence, and master/transport behavior
without requiring audio hardware.

## Try it without a guitar

The default input is **Test riff**, an original eight-second generated plucked-string
pattern with single notes and power chords. Choose a working output device in
**Audio setup**, then press **Play**. No microphone, guitar, or interface is required.
The synthetic riff is for development; it is not a recording of a real guitar.

Choose **Audio file**, or click **Load audio file...**, to feed a recording through
the same rack. WAV, FLAC, MP3, AIFF, and Ogg are supported when readable by the bundled
JUCE decoders. The file loads in the background and stays paused until you press Play.
MP3 decoding is explicitly enabled in both the application and test builds. Stereo
recordings are decoded in small blocks and averaged to mono, matching the guitar-input path. Playback is
converted to the output device's sample rate, preserving the recording's speed/pitch.
Use a dry guitar recording for the most representative distortion test; a full song
will process all its instruments together.

**Play/Pause** controls monitoring and playback, **Restart** returns to the beginning,
and **Loop** repeats the source. A playback counter shows the current position and
duration. Audio always starts paused, and changing the source/device pauses it again.
Only the selected source feeds the rack. Files are preloaded into memory, with a limit
of ten minutes and 128 MB of decoded working data. The file player is internal to
EffectHack; it does not install an operating-system virtual audio device.

## Play with a guitar

1. Connect your guitar to the interface's instrument input and headphones to its output.
2. Select **Guitar input** in **Input source**. Choose your interface for input and
   output in **Audio setup**. Enable the relevant
   input/output channels. The app currently supports up to two input and two output channels.
3. Choose the connected guitar channel in **Guitar input**.
4. Press **Start audio**. The app always starts with its output muted, and changing the
   audio device mutes monitoring again. Start with a low headphone volume.
5. Adjust **Input** and **Output** in Master, and **Drive** and **Mix** in Distortion. Double-click a knob to restore
   its default. **Bypass distortion** keeps input/output gain and the same latency;
   cabinet and EQ have independent bypass switches.

The chosen mono source receives Master Input gain and is duplicated into two channels
before the effects rack. Every stage preserves stereo; a single enabled output channel
receives a mono fold-down only at the final output.
Disable the interface's
direct monitoring if you hear both dry and processed guitar. Input/output meters show
digital peak levels. Reported latency comes from the audio driver plus the distortion's
oversampling delay (output latency only for virtual sources); it is an estimate, not
a measured round-trip value.

## Master and playback controls

**Master** stays visible above the scrollable rack. Its **Input** gain acts before the
rack; **Output** controls the entire processed signal and tails. These are the same
Input/Output values stored in existing presets. **Mute** fades the output to silence
while file playback and effect processing continue. **Bypass whole rack** crossfades
to a mono dry signal, duplicated to stereo and delayed to match the rack's processing
latency. Input/Output gain still applies. The individual effect settings are preserved,
and effects continue running while bypassed so switching back does not restart them.
Mute and whole-rack bypass are session controls: preset loads do not change them and
they reset when the app is relaunched.

Input/output meters show digital peak level. Their **CLIP** indicators latch on input
full-scale/overload or output overload before the final hard clamp. **Clear clips**
resets the indicators; an ongoing overload lights them again. This is peak protection,
not an automatic limiter or a measurement of clipping inside the audio interface.

The file/test-riff timeline displays **mm:ss / mm:ss**. Drag to preview a position and
release to seek, or use its arrow keys. Seeking during playback fades down, changes
position, clears old effect history, and fades back up. Seeking while paused keeps it
paused. **Restart** returns to the beginning while preserving play/pause state;
**Stop** fades out, rewinds, and stays paused. **Space** toggles play/pause (or live
monitoring), and **Home** restarts a virtual source. Shortcuts apply while the app is
focused, outside text entry and modal dialogs. Hardware input has no seekable timeline;
Stop/Restart are disabled for that source. The existing loop control remains available
for files and the test riff.

## Tuner, input guidance, and performance

The tools panel above the first rack stage contains the tuner, source/trim levels,
and performance details. **Tuner...** in the header enables tuning and scrolls to it.
Master uses colored numeric peak meters; the header shows compact LOAD/DROPOUTS values.

Enable **Tuner** and pluck one string at a time. The display shows note/octave,
frequency, signed cents, and a center needle; within +/-3 cents is colored green.
A4 can be set from 430 to 450 Hz (default 440). **Mute to tune** defaults on and fades
the output silent while tuning, independently of Master Mute. Turning the tuner off
preserves Master Mute. Guitar tuning and level measurement work even with monitoring
paused, provided the hardware input is enabled. Files/test riff must be playing;
the tuner never starts playback itself. The synthetic riff also contains chords,
which are outside this monophonic tuner's intended use.

The tuner reads the dry signal before Input trim, gate, and all rack effects.
Filtered samples are resampled to 12 kHz into a fixed-size, non-blocking FIFO. A
background worker uses a [YIN-style pitch estimator](https://www.ee.columbia.edu/~dpwe/papers/deChevK02-yin.pdf)
on 2048-sample windows, updating roughly every 64 ms once enough data has arrived.
Supported detection range is approximately 40-1400 Hz; the analysis window itself
adds about 171 ms of observation time without adding audio-path latency. Silence
and low-confidence noise show no note. Chords, very weak fundamentals, heavy noise,
and a full mixed song can produce ambiguous readings. Source/channel/device changes,
pausing virtual playback, and seeking invalidate old readings.

**SOURCE** and **TRIM** are horizontal meters with numeric dBFS peaks. The white
marker on SOURCE shows RMS; green range marks indicate -18 to -6 dBFS. TRIM shows
the level entering the rack. Peak indications decay over about 350 ms; RMS uses a 100 ms envelope.
Meter colors distinguish weak, normal, and hot levels; clipping is latched in Master.
Input advice and detailed RMS values are available in tooltips. Strong peaks around -18 to -6 dBFS are a starting guide, not automatic
normalization or a required guitar calibration. Raise/lower physical interface gain
for the recorded guitar signal; use Input trim for how hard the effects are driven.
Software trim cannot repair converter clipping, and the app cannot detect analog
overload that the interface does not expose as digital clipping. **Clear clips**
clears the latched warnings. Virtual sources get source/trim guidance instead of
instructions to adjust an interface.

**Audio load** is time spent in the audio callback relative to its available time,
not total computer CPU use. The display briefly holds peaks and warns at 80%.
**Dropouts** uses JUCE's driver report plus callback overrun estimates; it is an
indicator rather than a complete measurement of every audible glitch. **Reset**
clears the displayed count. New device sessions clear old performance warnings.
The panel also shows sample rate, buffer size, and estimated driver/rack latency.
The load history is graphed over approximately five seconds with an 80% reference
line. LOAD and DROPOUTS values change color for overload/dropouts. Explanations and
troubleshooting are available on hover rather than as permanent status sentences.

Tuner reference and the mute-to-tune preference are remembered on normal exit;
the tuner itself always starts disabled. These tools are outside the movable rack
and tone presets, and do not change preset dirty state.

## Noise gate and compressor

Both stages start **bypassed**, including when older presets are loaded. Uncheck their
Bypass switches to use them. Default order is Master Input, Noise Gate, Compressor,
then distortion/amp and the rest of the rack. Both stages are movable, add zero samples
of latency, and use the louder channel to control a shared gain, preserving stereo balance.
Their GR bars show gate attenuation and compressor gain reduction as colored numbers
and horizontal meters. Bypass dims the meters. Stereo linking, knee, and hysteresis
details are available in tooltips.

The **Noise Gate** has Threshold (-90 to 0 dBFS, default -50), Attack (0.1-20 ms,
default 1), Hold (0-250 ms, default 30), and Release (10-1000 ms, default 120).
Its peak detector uses a fast attack and slower decay. A 6 dB gap between opening and
closing thresholds reduces chatter near the threshold. Hold keeps the gate open briefly
after a note falls below the closing threshold; Release fades it down afterward.
Set the threshold just above idle guitar/interface noise, then check that quiet notes
still open it. Placing the gate before the amp reduces noise between phrases while
letting downstream delay and reverb tails continue.

The **Compressor** offers Threshold (-60 to 0 dBFS, default -18), Ratio (1:1-12:1,
default 4:1), Attack (0.1-100 ms, default 10), Release (10-1000 ms, default 120),
Makeup (0-18 dB, default 0), and Mix (0-100%, default 100). It uses feed-forward peak
detection, a fixed 6 dB soft knee, and attack/release smoothing of gain reduction.
Makeup is manual and applies after compression; Mix blends the dry and compressed
signals for parallel compression. A 1:1 ratio with zero makeup is neutral. Mix 0% or
full bypass preserves the dry signal. Gain reduction is displayed before makeup.
Makeup can raise the output level, so use the output meter when balancing a preset.

Parameters and bypass transitions are smoothed. Bypass does not pause transport or
change stereo routing. All ten dynamics knob values, both bypass switches, and both
positions are stored in tone presets; gain-reduction meter readings are not stored.

## Distortion

The effect uses `tanh` soft clipping at four times the device sample rate, with a
latency-aligned dry path and 20 ms parameter ramps. Input gain ranges from -24 to +24 dB,
drive from 0 to 48 dB, mix from 0 to 100%, and output from -60 to 0 dB. Output is bounded
to digital full scale. Master Output defaults to 0 dB.

## Generic amp

The movable **Amp** stage supplies original **Clean**, **Crunch**, and **High Gain**
voices. It starts **bypassed**, including when older presets are loaded. To audition
it, bypass Distortion, uncheck **Bypass amp**, keep the cabinet enabled, and choose a
voice. Distortion can then be used as an optional pedal ahead of the amp.

- **Gain:** 0-36 dB of preamp drive; default 12 dB.
- **Bass / Mid / Treble:** +/-12 dB at 120 Hz, 750 Hz, and 2.8 kHz.
- **Presence:** +/-12 dB high shelf at 4.5 kHz.
- **Level:** -24 to +6 dB after the amp; default -6 dB. Master Output still controls the whole rack.

The voices use different input/interstage filters, saturation headroom, asymmetry,
and stage counts. Clean has two gentle stages, Crunch two stronger stages, and High
Gain three stages with tighter low-frequency filtering. The voice paths run in parallel
so changing voices crossfades over 30 ms without clearing their history. A shared tone
section and mild output-stage saturation follow the preamp; a DC filter removes offsets.
Nonlinear processing runs at 4x the device rate. These are generic algorithmic voices,
with no circuit-accurate or named-amplifier claims, trained models, or power-supply sag.

Left/right channels have independent histories and shared settings. Bypass crossfades
to a dry path aligned to the amp's oversampling latency; amp Gain/Level/tone controls do
not affect that bypassed signal. Voice, control, and bypass changes are smoothed, and
the reported rack latency and master dry path include the amp even when bypassed.
Controls, voice, bypass, and the amp's rack position are saved in presets. The Cabinet
stage remains separate; the amp does not include a speaker/cabinet response.

## Cabinet and EQ

The default signal chain is **input -> gate -> compressor -> distortion -> amp -> cabinet -> EQ -> chorus -> delay -> reverb -> output level**.
The effects are represented internally as stable-ID `RackStage` objects in an ordered
`RackStageChain`. All stages use a two-channel buffer. Distortion, amp, cabinet, and EQ use
two independent mono processors with shared controls, preserving left/right histories.
Chorus, delay, and reverb process stereo directly. Bypassed stages retain both channels.
Master Input gain always runs before the rack, including when distortion moves later.
Live Up/Down moves fade down over 20 ms, switch order and reset effect histories at
silence, then fade back up over 20 ms. Playback continues; old tails are cleared.
No-op and invalid moves leave audio untouched. The signal-flow strip and card positions
follow the same desired order, with boundary move buttons disabled.
Output gain is applied after the whole rack, with a digital full-scale bound.

Cabinet simulation is enabled by default. **Built-in speaker approximation** is an
original synthetic impulse response with bass and treble roll-off and broad speaker-like
resonances. It is not a measured recording of a particular cabinet. **Load IR...** accepts
mono/stereo WAV, AIFF, and FLAC cabinet impulse responses up to 500 ms. Custom stereo
IRs are averaged to mono and normalized by JUCE's convolution engine. The sample rate
is converted automatically. Invalid/silent IRs are rejected while the current cabinet
remains active. **Use built-in** restores the default. Live IR changes use JUCE's
background convolution preparation and transition; file decoding stays off the audio thread.

EQ offers **Bass** (120 Hz low shelf), **Mid** (750 Hz bell, Q 0.8), and **Treble**
(3.2 kHz high shelf), each with +/-12 dB adjustment and a neutral 0 dB default. Controls
and bypass transitions are smoothed. The cabinet uses zero-block-latency convolution;
any delay inherent in a loaded IR is part of that response.

Performance scoring and optional multiple-rack switching are future work.

## Chorus, delay, and reverb

The three new effects start **bypassed**. Uncheck a stage's **Bypass** to hear it.
Scroll the effects rack to access every stage; scrolling over knobs scrolls the rack
rather than changing a parameter. All mixes are linear dry/wet blends.

- **Chorus:** Rate 0.1-5 Hz, Depth 0-100%, Mix 0-100%. Two modulated delay taps use
  quarter-cycle-separated LFOs to create stereo movement from a mono guitar signal.
- **Delay:** Time 20-1500 ms, Feedback 0-80%, Mix 0-100%. Time changes glide over
  50 ms, so changing time while playing can bend the pitch of repeats. This version
  uses manual milliseconds, without tempo sync or ping-pong routing.
- **Reverb:** Size 0-95%, Damping 0-100%, Mix 0-100%. JUCE's stereo room algorithm
  supplies the ambience; freeze mode is disabled.

Controls and bypass mixes are smoothed. The final Output knob controls all stages,
including their tails, and output remains bounded to digital full scale. At the end
of a non-looping file, enabled effects can ring out until quiet (at most 30 seconds),
with **TAIL** shown in the status line. Pause still fades to silence promptly. Restart,
source changes, and replay clear the old spatial-effect buffers.

## Tone presets

Use the preset selector above the source controls to load a saved tone. **Save as...**
creates a named preset; **Save** updates the selected user preset. **Rename...** changes
its name without saving unsaved knob edits. **Delete...** asks for confirmation and
keeps the current tone in the rack. **Reload** discards knob edits and restores the
selected saved tone. Selecting another preset also replaces unsaved tone changes.
A compact **Modified** status appears whenever the rack differs from the selected
preset; normal statuses are Default, Factory, or Saved. Preset descriptions and action
details are available in the status tooltip. Errors and missing-file warnings remain visible. **Default rack** and the factory tones are read-only; Save on them opens Save as.

The **Factory presets** group contains six ready-to-use starting points:

| Preset | Sound |
| --- | --- |
| Clean | Clean amp, gentle compression, small room |
| Crunch | Mid-forward rhythm crunch, gentle gate, small room |
| Metal Rhythm | Tight gated high gain with dry articulation |
| Lead | Mid-focused high gain, moderate compression, delay and reverb |
| Chorus Clean | Clean amp with slow stereo chorus and light reverb |
| Ambient Clean | Wide clean modulation with longer delay and reverb |

All use the built-in cabinet and work without downloaded IRs. They use the amp as
their drive source, leaving the separate distortion pedal bypassed. Fixed amp levels
balance their RMS on the built-in reference riff (within 0.7 dB); different guitar
pickups and playing dynamics can still require Input/Output adjustment. These are
generic musical starting points. **Your presets** appears separately; **Save as...**
creates an editable copy without changing a factory tone. Existing user presets may
share a factory name because the two groups have independent identities.

Presets include all 32 knob values, the amp voice, nine bypass switches, the cabinet selection, and
all nine stage IDs in execution order. Reordering counts as an unsaved change. Reload
restores the saved order; Default rack restores the original order as well as its knobs.
They do not change the source, audio file, playback position, loop setting, monitoring,
or audio devices. Spatial tails are cleared on load. On the first launch the app uses Default rack. Later launches restore your last setup;
your saved presets remain available in the selector.

New saves use JSON schema version 4. Versions 1-3 remain readable. Versions 1 and 2
receive a bypassed amp after distortion as before; all older versions receive bypassed
gate and compressor stages at the front. Their existing relative order and controls
are preserved. Missing, repeated, or unknown stage IDs and unsafe control values are
rejected before applying a preset. Reading older files does not rewrite them; saving
or renaming writes version 4.

Versioned JSON files are saved locally under JUCE's user application data directory,
in `EffectHack/Presets` (normally `~/.config/EffectHack/Presets` on Linux). The full
folder path is also available in the preset status tooltip. The folder is created on
first save. Writes replace the previous file only after the new JSON is written.
Names are unique ignoring case, with a 64-character limit. Unreadable or unsupported
preset files are skipped with a warning; values outside the supported control ranges
are rejected.

Custom cabinet IRs are referenced by absolute path, not copied into the preset.
Keep the original IR file in place. A missing or invalid IR prevents loading the
entire preset and leaves the current rack intact. File reading and IR decoding for
preset loads run in the background. There is no import/export or automatic preset
switching in this version.

## Record and audition a dry phrase

Use **Record** in the PHRASE strip, or **R**, and play a phrase. Press **Finish / R**
to loop that performance through the current rack. Recording also works with an
audio file or the built-in riff, so it can be tested without a guitar. It starts
monitoring on the selected source when a working output/input is available.

The selected mono input is captured before Master Input trim, gate, compressor,
all effects, Mute, and Output. Its original level and dynamics are retained. A
short five-millisecond fade at either end reduces loop-boundary clicks; there is
no normalization or continuous auto-leveling. The callback writes into preallocated
memory; clip finalization and waveform analysis happen on a worker.

While auditioning, the phrase replaces the live feed and keeps looping. Adjust
any effect, Input trim, or A/B slot to hear the same playing through different tones.
**Audition / L** toggles phrase playback versus the original input. **Live** returns
to that input while keeping the take available; **Clear** discards it. Playback,
seek, Stop, Space, Home, Mute, and whole-rack Bypass work with the phrase. Source
changes use a short fade and clear previous effect history. The original source
file, position and loop preference remain available when you return.

A take can be 0.25–30 seconds long; reaching 30 seconds finishes automatically.
The strip shows recording time, a waveform, and a playback cursor. Too-short or
silent takes leave the previous take available and expose an error in the duration
indicator's tooltip. Recording while an older phrase is auditioning first returns
to the original input, preventing the phrase from recording itself.

Pausing stops capture without restarting playback after preparation. Device changes
pause audio and end a partial capture at its original sample rate; retained phrases
are resampled for the new output device. Changing the input source or channel cancels
a pending capture. Phrases live in memory for this app session, are never uploaded,
and are excluded from tone presets/A/B snapshots and startup files. Startup restores
the original source with audio paused.

Fresh setups, the default rack, factory presets, and new generated default levels
use **Master Output 0 dB**. Existing user presets and startup settings keep their
explicit saved levels. Double-click Output to reset it to 0 dB. Generated-tone
headroom checks and the output clip indicators
continue to operate.

## A/B tone slots and keyboard controls

The **A / B** buttons beside the preset status hold two editable versions of the rack.
A starts with the current setup. The first visit to B copies the current tone; later
visits restore B's own edits. Switching automatically retains the tone you leave,
including unsaved controls, bypasses, amp voice, cabinet and effect order. Loading a
preset or generating a song tone replaces the currently selected slot's working tone.
Named preset files are changed only by the normal Save actions.

Use **A → B** or **B → A** to replace the other slot with a copy of the current tone
and its unsaved edits. Copy does not switch slots. For an easy comparison, copy A to
B, select B, tweak it, then switch between A and B. The active button is highlighted;
hover either button for its tone name. **Keys** opens the shortcut reference.

| Key | Action |
|---|---|
| **1** | Select A |
| **2** | Select B |
| **X** | Switch A/B |
| **Shift+X** | Copy the current tone to the other slot |
| **B** | Toggle whole-rack bypass |
| **M** | Toggle Master Mute |
| **T** | Toggle tuner on/off |
| **R** | Record / finish a dry phrase |
| **L** | Audition phrase / return to original input |
| **Space** | Play/pause monitoring or file playback |
| **Home** | Restart file/test-riff playback |

Shortcuts apply inside the rack window, including when a rack button has focus.
Text editors, slider value entry, selection menus and modal dialogs retain normal
keyboard behavior. Holding a key triggers its action once, until it is released.
The header Tuner button also toggles on/off; Mute to tune keeps its existing behavior
and never changes Master Mute.

**Master Input and Output are shared between A and B.** Switching retains the current
trim and listening volume, plus Mute, whole-rack Bypass, tuner, audio devices, input
source and transport position. Amp Level and effect gains remain part of each tone,
so the slots can still have different loudness. There is no continuous auto-leveling.

Tone changes use the existing fade-down, complete-rack commit and fade-up path.
Identical tones switch without resetting the DSP. Different tones clear old effect
tails during the change. This uses one processor, so delay/reverb tails do not spill
between independent parallel racks. Cabinet files are prepared in the background
and cached after loading; failed preparation leaves the current tone and slot active.
Slot switches do not replace the existing one-step Song Revert history.

Both slots, their preset/generated baselines, their unsaved edits, and the selected
slot are remembered on normal close. Playback starts paused after reopening. Old
startup files without slots remain compatible. Slots remember custom cabinet paths;
those files must still exist when first loaded after a restart.

## Matching a song's guitar tone

Click **Song...**, choose a reference and guitar part, then **Match tone**. **Custom**
is enabled by default. The best input is a 15–30 second recording where the intended
guitar sound is clear, ideally an isolated guitar part. A public YouTube link or a
full-song audio file also works. Use **From / To** to target a verse, intro, or solo
instead of averaging different guitar sounds. **Main rhythm / Lead / Intro** chooses
one static tone for that part. A From time alone selects 30 seconds; both times on
Auto analyze the available recording (local files must be no longer than six minutes).

Three input sources are available:

- **YouTube** sends the public video URL directly to Gemini as media input. Standard
  watch, youtu.be, mobile/music, Shorts, embed, and live-video URLs are accepted.
  Use the time controls for an excerpt; timestamps embedded in share URLs are ignored.
  The app fetches public title/channel metadata but does not download the video itself.
- **Audio reference** decodes WAV, FLAC, MP3, AIFF, or Ogg locally, selects the requested
  section, and uploads a 16-bit, 16 kHz mono WAV to Gemini. Only the selected section
  is sent, not the original file or its full local path. Choose a section of 1–360
  seconds from a mono/stereo file up to 512 MB. Reference audio is never played
  automatically or fed into the live guitar input.
- **Artist + song** uses model knowledge without a target recording. The result is
  labelled **Song estimate**, distinct from **Audio reference**. Audio references
  generally provide stronger evidence of the intended sound. No MIDI or Guitar Pro
  file is required: notes and instrument labels do not supply the original guitar timbre.

The model hears nine actual processor examples: the six factory tones plus edge of
breakup, classic crunch, and a distortion pedal into a clean amp. They render the same original synthetic plucked-string
phrase, with matched RMS levels and peak headroom, so the comparison includes how
this processor actually sounds. It selects an example and proposes bounded changes
to gain, EQ/presence, gentle compression, chorus, delay, and reverb. Local code compiles
these into the full 32-control rack, retains the tested effect order and amp voice,
uses a conservative gate, and balances internal amp Level against a clean reference.
A second pass hears the rendered candidate alongside the source and examples and can
refine the plan. If that review fails, the first valid match remains available.

**Keep master** is enabled by default: Input trim and Output listening volume remain
yours, including changes made while matching runs. Turning it off uses Input 0 dB
and Output 0 dB. Tone balancing runs once in the background; it is not an automatic
volume controller during playing. Turn **Custom** off to select one unchanged factory
preset, including its stored master levels.

All model fields are validated locally: exact field names, numeric types/ranges,
known example IDs, bounded text, and the complete compiled rack schema. A background
headroom check plays a louder synthetic riff plus four seconds of effect tails and
rejects nonfinite audio. If needed it attenuates the amp's post-gain Level while
keeping Master controls, or Output when Keep master is off. This checks one reference
performance; it cannot guarantee headroom for every guitar or pickup.

This is a tone approximation, not recovery of original studio equipment or a measured
match percentage. The limited generic amp and synthetic cabinet, pickups, playing
technique, mixed instruments, and layered guitars affect the result. Unsupported
features such as wah, phaser, octave/pitch effects, and acoustic-body resonance are
not invented. Hover the result for its musical rationale, qualitative model confidence,
limitations, base tone, and model used.

The current rack stays unchanged until analysis, balancing and headroom checks finish.
The engine then fades down, commits the complete rack while silent, and fades up.
Source, devices, channel, transport, Mute, whole-rack Bypass and tuner state stay
independent. The new tone appears in the **Generated** preset group. **Save as...**
keeps it as a user preset; **Revert** restores the previous tone and unsaved edits.
Generated baselines and later edits survive normal close/reopen. Revert history is
session-only. Matching and preview run on workers; **Cancel** or closing the dialog
discards pending results without changing the rack.

Set `GEMINI_API_KEY` in local `.env` or the app's launch environment. `GEMINI_MODEL`
defaults to `gemini-3.5-flash-lite`, verified with both media input paths. If a configured
model returns HTTP 404, the app retries with that default and records the fallback in
the result tooltip. Authentication, quota, and other failures do not trigger a model
switch. Public YouTube media support depends on Gemini; private/unavailable videos
may require a local audio reference or a song estimate. Each model request has a
120-second timeout. Local audio references make two requests in Custom mode, as do
YouTube references; name-only estimates and factory selection use one, excluding a
possible unavailable-model retry.

The key is sent only in Gemini's HTTPS authentication header and is not stored in
presets, sessions, or the binary. `.env` discovery searches the working directory and
its parents, then the executable directory and its parents (up to eight levels each).
Environment settings take precedence. See `.env.example`. Uploaded reference audio
and synthetic examples are processed by the configured Gemini service/project;
no live guitar recording is uploaded.

To test the matcher without opening the GUI or an audio device:

```sh
cmake --build build --target EffectHackToneProbe
./build/EffectHackToneProbe youtube 'https://youtu.be/CD-E-LDc384' /tmp/tone.json rhythm 1:00 1:30
./build/EffectHackToneProbe file /absolute/path/reference.mp3 /tmp/tone.json lead 0:30 1:00
./build/EffectHackToneProbe name 'Artist - Song' /tmp/tone.json rhythm
./build/EffectHackToneProbe --examples /tmp/effecthack-examples
```

The probe makes live API requests except in `--examples` mode and writes the validated
plan, complete rack, final headroom-checked preset, and rationale to JSON. It never
prints credentials. Automated CTest uses fake service responses and synthetic audio,
without network access or audio hardware.

## Remembering your setup

Closing the app saves `EffectHack/session.json` alongside the Presets directory.
The next launch restores the complete rack, including unsaved knob changes, stage
order, cabinet path, and the selected preset identity. The unsaved-changes indicator
still compares your restored tone with the selected preset. It also remembers the
input source, guitar input channel, loop setting, and audio device/driver, active
channels, sample rate, and buffer size. A selected audio file reloads in the background.

Audio always starts paused at the beginning. Mute and whole-rack Bypass reset to off.
Saving startup state does not modify any named preset. Files and cabinet IRs are
referenced by their original paths. A missing recording leaves the file player empty
with an error; a missing IR prevents restoration of the tone and leaves Default rack
active. If the selected user preset was deleted, the last tone is recovered separately
and can be saved with **Save as...**. Missing devices fall back to an available default.
Corrupt or unsupported startup files show a warning and use defaults. Settings are
saved on normal close; forced termination does not save the latest changes.

## Source layout

- `effects/NoiseGate.*`: stereo-linked gate, hysteresis, hold/release, and attenuation meter.
- `effects/Compressor.*`: stereo-linked soft-knee compression, makeup/mix, and gain-reduction meter.
- `effects/Amp.*`: generic amp voices, filtered oversampled preamps, tone section, output saturation, and bypass.
- `effects/Distortion.*`: DSP, parameter smoothing, oversampling, dry/wet blend.
- `effects/Cabinet.*`: built-in speaker response, IR file validation, convolution, bypass.
- `effects/Equalizer.*`: smoothed bass/mid/treble biquads and bypass.
- `effects/Chorus.*`, `effects/Delay.*`, `effects/Reverb.*`: independent stereo effect stages.
- `effects/ChannelWiseRackStage.*`, `effects/StereoRackStage.*`: independent-channel adapters and stereo stages.
- `effects/RackStage.*`: reorderable stage interface and non-owning ordered rack chain.
- `AudioEngine.*`: device callback, mono routing, master gain/mute/bypass, peak metering, and faded transport changes.
- `VirtualInput.*`: file decoding/downmix, generated riff, memory playback, resampling.
- `DryPhraseRecorder.*`: bounded preallocated dry capture, worker finalization, boundary fades, and waveform data.
- `DryPhrasePanel.*`, `MainComponentPhrase.cpp`: capture/audition controls and guarded asynchronous source switching.
- `PresetManager.*`: versioned tone snapshots, validation, local preset CRUD, and IR preparation.
- `FactoryPresets.*`: read-only factory catalog with stable IDs and balanced starting tones.
- `SessionState.*`: validated, atomic startup snapshots, separate from tone presets.
- `MainComponentSession.cpp`: background tone restoration and setup capture on close.
- `MainComponentSong.cpp`, `SongPresetPanel.*`: reference/part/section UI and atomic preset-loader integration.
- `ToneSlots.*`: A/B edit buffers and validated startup snapshots.
- `MainComponentToneSlots.cpp`, `MainComponentKeyboard.cpp`: slot controls, copying, prepared-cabinet reuse, and guarded keyboard actions.
- `ApiConfig.*`: local dotenv discovery, environment overrides, and model configuration.
- `YouTubeMetadata.*`: allowlisted video URL parsing and public metadata lookup.
- `GeminiRecommender.*`: shared recommendation types and the legacy metadata-only API.
- `ToneAnalysis.*`: media-comparison prompts, bounded plan schema/validation, and deterministic rack compilation.
- `ToneMatching.*`: processor example rendering, media matching, audition refinement, model fallback, and internal level balancing.
- `ReferenceAudio.*`: background audio-section decoding, downmixing, resampling, and in-memory WAV encoding.
- `GeneratedRack.*`, `RackDefinition.h`: complete generated schema and shared parameter/routing definitions.
- `GeneratedTonePreparation.*`: preset conversion and off-thread reference-headroom preview.
- `HttpClient.*`: bounded HTTPS requests and cancellation via libcurl.
- `InputMonitor.*`: fixed-storage input metering and dry-source analysis FIFO.
- `Tuner.*`: background monophonic pitch detection and note/cents conversion.
- `RigStatus.*`: input advice and callback load/dropout tracking.
- `RigTools.*`: tuner, source/trim meters, load history, and performance values.
- `MetricMeter.*`: shared read-only numeric/range meters with optional target bands and RMS markers.
- `MainComponent.*`: rack/preset controls and audio-device selector.
- `main.cpp`: application/window lifetime.
- `tests/DistortionTests.cpp`: synthetic-signal checks; no audio hardware is required.
- `tests/VirtualInputTests.cpp`: MP3/WAV decoding, looping, EOF/replay, pause/resume,
  sample-rate conversion, output-only playback, and physical-channel routing.
- `tests/fixtures/sine-440hz.mp3`: generated 440 Hz stereo tone used to catch MP3
  decoder configuration regressions. No external encoder is needed to run the tests.
- `tests/ToneTests.cpp`: frequency-response, convolution, IR validation, bypass, and
  complete-rack output-level checks.
- `tests/SpatialEffectsTests.cpp`: bypass, echo timing/decay, stereo modulation,
  reverb tails, EOF handling, pause, and parameter automation.

- `tests/PresetTests.cpp`: complete-rack round trips, persistence/CRUD, malformed settings,
  custom and missing IRs, and preservation of source/monitoring settings.

- `tests/MasterTests.cpp`: bypass latency/gain, smooth transitions, mute/playback, clip latches,
  preset independence, seek/stop/restart, and clearing old echoes.

- `tests/RackOrderTests.cpp` (part of EffectHackPresetTests): all 362880 nine-stage execution orders,
  moves, version 1/2/3 migration, version 4 tone/order persistence and validation, per-channel isolation
  at four sample rates, stereo width after reordering, Master Input placement, and live fades.

- `tests/AmpTests.cpp` (part of EffectHackToneTests): four sample rates, bypass latency,
  distinct voice saturation, gain/tone behavior, DC rejection, resets, invalid input,
  voice transitions, and an informational render-speed measurement.

- `tests/NoiseGateTests.cpp` (part of EffectHackToneTests): four sample rates, closure,
  hysteresis, hold/reopen, stereo linking, bypass, invalid inputs, reset, and engine routing.
- `tests/CompressorTests.cpp` (part of EffectHackToneTests): four sample rates, ratio/knee,
  attack/release, makeup/mix, stereo linking, bypass, invalid inputs, and reset.

- `tests/FactoryPresetTests.cpp`: read-only catalog/copies, reference levels, peak headroom, and stereo width.
- `tests/SessionStateTests.cpp`: startup round trips, invalid settings, and atomic replacement failures.

- `tests/TunerTests.cpp`, `tests/InputMonitorTests.cpp`: fundamental detection, note/reference/cents, silence/noise rejection, resampling, fixed FIFO overflow, input advice, performance counters, and worker shutdown.
- `tests/InputToolsEngineTests.cpp`: paused hardware analysis, raw/trim separation, tuning mute, Master Mute preservation, virtual pause, and device stop.

- `tests/SongRecommendationTests.cpp`: offline YouTube URL/metadata cases, dotenv precedence, schema/catalog constraints, output validation/abstention, authentication routing, cancellation, and HTTP errors. Live API calls are not part of CTest.

- `tests/GeneratedToneTests.cpp`: complete tone decoding, boundary values, preview attenuation/cancellation, silent commit/fade-up, cancelled transitions, disconnected device commits, and generated startup state.

- `tests/ToneMatchingTests.cpp`: actual audio examples, section decoding, media request routing, bounded plans, audition refinement/failure, model fallback, cancellation, master preservation, and internal headroom checks.

- `tests/ToneSlotTests.cpp`: snapshot isolation, generated baselines, complete rack persistence, copying, validation and legacy startup compatibility.
- `tests/RigControlTests.cpp`: UI/engine integration for A/B switching, shared Master gains, keyboard repeat/typing guards, custom-cabinet failure/cache, paused startup restoration, and minimum-width layout. Tests open no audio device.

- `tests/DryPhraseTests.cpp`: selected-channel pre-trim capture, muted capture, finite samples, duration cap, continuous looping, original-source preservation, pause/device/rate changes, and simulation with the test riff.
- `tests/PhraseControlTests.cpp`: recorder UI, automatic audition, A/B continuity, pause/clear races, waveform layout, temporary-state persistence, and default/saved Output levels.
