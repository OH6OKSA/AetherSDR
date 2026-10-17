# OksaSDR — status and roadmap

Updated with every build. The newest TestFlight build is at the top; what is
queued, waiting on someone, or still to verify follows.

## Released

**Build 77** (2026-10-17), iPhone and iPad, and Android beta 2:
- Spots on the display and a DX cluster connection (spots go to the radio); tap to tune.
- TNF: hold on the display to add a notch, tap it to adjust; TNF switch in DSP.
- TX processing of this device's microphone: gate, EQ, compressor.
- Memory editing (name, frequency, mode).
- Voice keyer: Listen also for clips kept on the device.
- Mic meter reads this device's microphone; very low windows show bar meters.

**Build 76** (2026-10-17), iPhone and iPad:
- Radios on the connect page read as buttons.
- Pan slider shows C / L n / R n and snaps to centre.
- Phones open the controls only from the button beside PTT.
- The controls drawer reaches the window's bottom edge.

**Build 75** (2026-10-17), iPhone and iPad:
- OksaSDR app icon.
- Volume and pan sliders follow the finger.
- Meter peak hold resets with the meter's quantity; ALC/Mic below scale read "< min".
- MOX removed; one Mic level; device mic trim in Setup → Phone/CW.
- Callsign edited in Setup → Radio only.

**Build 74** (2026-10-17): the app is called **OksaSDR** (formerly AetherSDR
Mobile); TX soft start limited to iOS.

**Build 70** (2026-10-16): STEP fix and placement, dBm scale after band
change, info panel order, View pan settings, Band zoom, WWV/GEN, split
offsets, meter peak hold.

Earlier builds: see the git log and `TESTFLIGHT_WHATS_NEW.txt`.

## Android (beta)

Same code as iOS (`mobile/android/` holds the platform parts), built and
signed in GitHub Actions (`mobile-android.yml`); betas in
[Releases](https://github.com/OH6OKSA/OksaSDR/releases).

Beta 1: receive, spectrum and waterfall, TX, background receive (foreground
service, Qt kept running), SmartLink with the sign-in kept in the Android
Keystore, Bluetooth headsets (hands-free), log sharing, LAN discovery
(multicast lock), crash reasons logged on the next start.

Beta 2: what iOS build 77 brings (spots and DX cluster, TNF, TX
processing, memory editing, voice keyer Listen, Mic meter). Published by CI
from `mobile/android/release.env`.

Next: verify LAN discovery on the radio's network; APK size (about 150 MB).

## Later

From the comparison with upstream main and tester requests:
- Filter edge drag and custom widths
- Memory offsets and tones (repeaters); memory groups
- Dragging a notch; spot filters (band, mode, age)
- ATU tools and per-band TX settings
- Profile import and export
- QSO recording
- 3D with 768 columns
- Upstream FFT Smooth and De-comb
- Optional VOX mode (microphone always open)
- More of the desktop's TX chain (de-esser, tube, exciter, reverb) and a
  chain order
- CW profiles (new in SmartSDR 4.3)

## Waiting

- **Testers:** feedback on the TX start (no delay, no whoosh) from build 68
  on, and on build 70.
- **Upstream (aethersdr/AetherSDR):**
  - [discussion #6409](https://github.com/aethersdr/AetherSDR/discussions/6409): upstreaming, naming, distribution (GPL and Apple);
  - [#6532](https://github.com/aethersdr/AetherSDR/issues/6532): RFC, client-side Beat Cancel;
  - [#6533](https://github.com/aethersdr/AetherSDR/issues/6533): closing the TX slice leaves no slice transmitting;
  - [#6534](https://github.com/aethersdr/AetherSDR/issues/6534): RFC, Opus under Auto over a VPN;
  - [PR #6535](https://github.com/aethersdr/AetherSDR/pull/6535) for #6051: VITA-49 loss counted net of reordering.
- **To verify:** CW keying, CWX and the decoder on the air; the app against
  SmartSDR 4.3.x firmware.

## Platforms

| Area | iOS / iPadOS | Android |
|---|---|---|
| Connect by IP, SmartLink, VPN | ✅ | ✅ (SmartLink sign-in kept) |
| LAN discovery | ⏳ needs Apple's multicast entitlement | 🧪 built in, to verify |
| Spectrum, waterfall, 3D | ✅ | ✅ |
| RX audio, background audio | ✅ | ✅ (foreground service) |
| Voice TX (device mic, Bluetooth headset) | ✅ | ✅ |
| CW, CWX, decoder, voice keyer | ✅ (CW on air to verify) | built in, to verify |
| Spots and DX cluster, TNF, memory editing | ✅ | ✅ |
| TX processing (gate, EQ, compressor) | ✅ | ✅ |
| Keyboard, pencil/stylus | ✅ | to verify |
| Phone and tablet layouts | ✅ | ✅ |
| Distribution | TestFlight | APK in GitHub Releases (beta) |
