# AetherSDR Mobile — status and roadmap

Updated with every build. The newest TestFlight build is at the top; what is
queued, waiting on someone, or still to verify follows.

## Released

**Build 70** (2026-10-16), iPhone and iPad:
- Tuning step fixed; **STEP** sits under the mode.
- Spectrum and 3D keep the right dBm scale after a band change.
- Info panel: always-on settings first (antennas, width, AGC, speaker,
  lock), then split and the DSP flags; tighter iPhone header.
- View: the radio's FFT average, FFT frames/s and waterfall speed.
- **Band** zoom beside −/+; **WWV** and **GEN** band buttons.
- Press and hold **SPLIT** for +1, +5 or +10 kHz.
- Meters hold their peak for 1.5 s.
- VOX and DEXP shown only with the radio's own microphone.

Earlier builds: see the git log and `TESTFLIGHT_WHATS_NEW.txt`.

## Next build

- TX soft start (60 ms mute + 40 ms ramp at key-up) limited to iOS, so the
  shared engine's desktop path is unchanged.
- iOS workflow builds only for changes that go into the app.

## Later

From the comparison with upstream main and tester requests:
- DX cluster spots on the panadapter
- TNF (tracking notch filters)
- Filter edge drag and custom widths
- Memory editing (name, mode, offsets)
- ATU tools and per-band TX settings
- Profile import and export
- QSO recording
- 3D with 768 columns
- Upstream FFT Smooth and De-comb
- Optional VOX mode (microphone always open)
- CW profiles (new in SmartSDR 4.3)

## Waiting

- **Testers:** feedback on the TX start (no delay, no whoosh) from build 68
  on, and on build 70.
- **Upstream (aethersdr/AetherSDR):**
  - [discussion #6409](https://github.com/aethersdr/AetherSDR/discussions/6409): upstreaming, naming, distribution (GPL and Apple);
  - [#6532](https://github.com/aethersdr/AetherSDR/issues/6532): RFC, client-side Beat Cancel;
  - [#6533](https://github.com/aethersdr/AetherSDR/issues/6533): closing the TX slice leaves no slice transmitting;
  - [#6534](https://github.com/aethersdr/AetherSDR/issues/6534): RFC, Opus under Auto over a VPN;
  - PR for [#6051](https://github.com/aethersdr/AetherSDR/issues/6051): VITA-49 loss counted net of reordering.
- **To verify:** CW keying, CWX and the decoder on the air; the app against
  SmartSDR 4.3.x firmware.

## Platforms

| Area | iOS / iPadOS | Android |
|---|---|---|
| Connect by IP, SmartLink, VPN | ✅ | — not started |
| LAN discovery | ⏳ needs Apple's multicast entitlement | — |
| Spectrum, waterfall, 3D | ✅ | — |
| RX audio, background audio | ✅ | — |
| Voice TX (device mic, Bluetooth headset) | ✅ | — |
| CW, CWX, decoder, voice keyer | ✅ (CW on air to verify) | — |
| Keyboard, pencil/stylus | ✅ | — |
| Phone and tablet layouts | ✅ | — |
| Distribution | TestFlight | — |
