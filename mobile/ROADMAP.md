# OksaSDR — status and roadmap

Updated with every build. The newest TestFlight build is at the top; what is
queued, waiting on someone, or still to verify follows.

## Released

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

## Android (test APKs)

Same code as iOS (`mobile/android/` holds the platform parts). Test APK 7:
receive, spectrum and waterfall, TX, background receive (foreground
service), OpenSSL for SmartLink, OksaSDR launcher icon.

Next: GitHub Actions APK build, SmartLink sign-in kept in the Android
Keystore, log sharing, Bluetooth headsets, LAN discovery.

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
- The desktop's client-side TX chain (gate, EQ, compressor: CHAIN)
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
| Connect by IP, SmartLink, VPN | ✅ | ✅ (test APK; SmartLink sign-in not kept yet) |
| LAN discovery | ⏳ needs Apple's multicast entitlement | — |
| Spectrum, waterfall, 3D | ✅ | ✅ |
| RX audio, background audio | ✅ | ✅ (foreground service) |
| Voice TX (device mic, Bluetooth headset) | ✅ | device mic ✅, Bluetooth to do |
| CW, CWX, decoder, voice keyer | ✅ (CW on air to verify) | — |
| Keyboard, pencil/stylus | ✅ | — |
| Phone and tablet layouts | ✅ | ✅ |
| Distribution | TestFlight | test APK (GitHub Actions to do) |
