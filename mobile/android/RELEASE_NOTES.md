**OksaSDR for Android — beta 2.** A FlexRadio client for Android phones and tablets, built on the [AetherSDR](https://github.com/aethersdr/AetherSDR) engine: the same app as on the iPhone and iPad (TestFlight build 77), from the same code.

### New in beta 2
- **Spots and the DX cluster** — View → Spots connects to a DX cluster (your radio's callsign, or one you set); spots show as callsigns on the display, tap one to tune. Spots from other programs on the radio show too.
- **Notch filters (TNF)** — press and hold on the display to put a notch there; tap it for width, depth, permanent or remove. DSP tab: TNF on/off and *+ TNF*.
- **Processing of the phone's microphone** (TX tab) — gate, EQ (low cut, bass, presence) and compressor before the audio is sent. Test at low power first.
- **Memory editing** — press and hold a memory to change its name, frequency and mode.
- **Voice keyer** — *Listen* plays a recording back without transmitting.
- The **Mic** meter moves with the phone's microphone while you transmit; very low windows (a foldable's outer screen in landscape) show bar meters.
- **Microphone permission**: beta 1 never asked for it (it had to be turned on in Android's settings). Beta 2 asks on the first connect, and PTT asks again if it was refused.

### Install or update
1. Download `OksaSDR-android-beta2.apk` below on the phone (about 150 MB).
2. Open it; allow installing from your browser or file manager when Android asks. If Chrome keeps the download waiting, open ⋮ → *Downloads* → *Keep*.
3. Beta 2 installs over beta 1 and keeps your settings. If you have one of the earlier hand-delivered test APKs, uninstall it first (different signing key).

Android 9 or newer, 64-bit ARM. On the first connect, allow notifications, the microphone and "no battery optimization" so receive keeps going in the background and PTT can transmit.

### Still to verify
- LAN discovery on the radio's own network (connect by IP works everywhere)
- More phones and tablets: please report your model and Android version

Full guide: [tester guide](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/TESTING.md#android) and [user manual](https://github.com/OH6OKSA/OksaSDR/blob/mobile/oksasdr/docs/mobile/MANUAL.md#13-android).

OksaSDR is a community project, not affiliated with FlexRadio and not an official AetherSDR app. Licensed under GPLv3; the source is this repository (branch `mobile/oksasdr`).
