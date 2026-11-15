# Playing guide

[Back to project](../README.md)

## Choose an input

Open **Input & audio…** to select a source and configure audio devices.

| Source | Use it for | Notes |
| --- | --- | --- |
| Guitar input | Live playing through an interface | Select the connected channel; a guitar plugged into the right input usually needs channel 2. |
| Test riff | Checking the rig without hardware | An original eight-second synthetic plucked-string phrase. |
| Audio file | Auditioning your dry recordings | WAV, FLAC, MP3, AIFF, and Ogg; decoded into memory, downmixed to mono, and resampled for playback. |

File input is limited to ten minutes and 128 MB of decoded working data. A full song used here runs *all* its instruments through the rack. Use a dry guitar take when comparing effects.

Changing source or device pauses playback. **Play/Pause** starts or stops monitoring. **Restart** rewinds without changing play/pause state; **Stop** pauses and rewinds. The timeline and loop controls apply to virtual sources, including recorded phrases.

## Set levels and latency

- Set the interface to instrument/Hi-Z mode if available. Start with Master Input at 0 dB and a comfortable listening volume.
- Use interface gain to set the incoming signal. Peaks around −18 to −6 dBFS are a useful starting range. Master Input determines how hard the rack is driven; it cannot undo clipping at the interface.
- Use Master Output for listening level. New defaults use 0 dB; saved presets retain their stored levels.
- **CLIP** stays lit after an overload. **Clear clips** resets it. The final output clamp is not an automatic leveling system.
- Use wired headphones. Bluetooth buffering can dominate playing latency. If you hear both dry and processed guitar, turn down the interface's direct monitoring.
- Start with a stable buffer such as 256 samples, then lower it if your setup can handle it. If dropouts rise, increase the buffer and check the system's power profile.

**LOAD** measures callback time relative to its audio deadline, not total CPU usage. Dropouts combine driver reports and estimated overruns. The displayed latency is a driver/DSP estimate, not a physical round-trip measurement.

## Shape the tone

| Stage | Practical starting point |
| --- | --- |
| Noise gate | Set threshold just above idle noise; verify quiet notes still open it. Keep it before delay/reverb to preserve tails. |
| Compressor | Use modest Mix and Ratio for even picking. Increase Attack to retain more initial pick transient. Makeup is manual. |
| Distortion | Raise Drive for clipping; lower Mix for parallel dry signal. |
| Amp | Choose Clean, Crunch, or High Gain. Gain sets saturation; Level balances the resulting tone. |
| Cabinet | Use the built-in speaker approximation or **Load IR…** for a custom response. |
| EQ | Adjust bass, mids, and treble after the amp/cabinet, or move it earlier to change what drives saturation. |
| Chorus | Add a small amount for width and movement. |
| Delay | Set repeat time, feedback, and mix. |
| Reverb | Set room size, damping, and mix. |

Use **Up / Down** to move stages. Stereo effects retain their width through later mono-derived processors because those processors have independent left/right histories. Double-click a knob to restore its default.

Whole-rack bypass retains Master gains and uses a latency-aligned dry path. Individual effect bypasses keep their parameters. Mute silences output while transport and effect processing continue.

## Match a reference

1. Open **Song…** and choose **YouTube**, **Audio reference**, or **Artist + song**.
2. Choose **Main rhythm**, **Lead**, or **Intro**.
3. For media, enter both section times (seconds or `m:ss`), or leave both blank. A From time alone selects 30 seconds; To without From is invalid. Maximum explicit section length is six minutes.
4. Leave **Custom** on to generate a tone; turn it off to select an unchanged factory preset.
5. Leave **Keep master** on to retain current Input and Output, including edits made while generation is running.
6. Click **Match tone**. Hover the result for the base tone, reasoning, limitations, model, and any review warning.

Local references accept mono/stereo files up to 512 MB. Only the selected 1–360 second section is converted to 16 kHz mono WAV and uploaded. Without a section, the whole local reference must fit the six-minute limit. YouTube share-link timestamps are ignored; use From / To instead. Name-based results are labelled **Song estimate** because no original recording is analyzed.

**Cancel** or closing the dialog discards pending results. **Revert** restores the complete rack from before the last song-tone application, replacing subsequent edits. Revert history lasts only for the current session.

## Record once, compare repeatedly

1. Press **R** or **Record** and play a phrase.
2. Press **R / Finish** to loop the take through the current rack.
3. Adjust effects or compare A/B slots. **L / Live** returns to the original source.

The recorder captures the selected mono input before trim and all effects, retaining dynamics. It supports 0.25–30 seconds and automatically finishes at 30 seconds. Only brief boundary fades are added. **Clear** discards the take. Recording also works from files and the test riff.

The phrase stays in memory, is not uploaded, and is not saved with presets or startup state. Closing the app loses it.

## A/B and presets

Copy **A → B**, select B, edit it, then use **X** to compare. The first visit to an empty slot copies the current tone. Slot changes retain unsaved edits, effect order, amp voice, and cabinet selection. Input/Output, transport, devices, mute, and whole-rack bypass remain shared. Different effect gains can still make the slots sound different in volume.

**Save as…** creates a named user preset; **Save** updates one. Factory presets are read-only starting points. Presets store 32 controls, nine bypasses, amp voice, effect order, and the cabinet path. They do not store devices or recordings.

A normal close remembers both slots, unsaved edits, source, selected file, channel, loop, tuner preferences, and device configuration. Playback restarts paused; Mute, rack Bypass, and the tuner itself start off. Custom IRs and input files are referenced by path and must remain available. Forced termination does not save the latest setup.

## Tune

Press **T**, pluck one string at a time, and center the cents indicator. The tuner analyzes the dry input; **Mute to tune** silences output without changing Master Mute. A4 is adjustable from 430 to 450 Hz. Chords and full mixes can produce ambiguous readings.
