# AetherSDR Mobile

**An unofficial iPad and iPhone port of [AetherSDR](https://github.com/aethersdr/AetherSDR), the open-source FlexRadio client.**

[![Mobile iOS build](https://github.com/OH6OKSA/AetherSDR/actions/workflows/mobile-ios.yml/badge.svg?branch=mobile/aether-mobile-core)](https://github.com/OH6OKSA/AetherSDR/actions/workflows/mobile-ios.yml)
![Platform](https://img.shields.io/badge/platform-iPadOS%20%7C%20iOS%2018%2B-blue)
![Status](https://img.shields.io/badge/status-TestFlight%20beta-orange)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

AetherSDR Mobile runs the AetherSDR engine (`libaethercore`) on the iPad or
iPhone and talks to the radio directly. There is no PC, server, `aetherd` or
SmartSDR in between: at home on the same network, or from anywhere over
SmartLink (or your own VPN), it is a complete station control surface.

> This fork is maintained by Niko Oksa, OH6OKSA. It is **not** an official
> AetherSDR release. The aim is to offer the work upstream; see
> [discussion #6409](https://github.com/aethersdr/AetherSDR/discussions/6409).

<p align="center">
  <img src="docs/mobile/ipad-landscape.jpg" width="70%" alt="AetherSDR Mobile on a 12.9-inch iPad: panadapter, waterfall and controls"/>
  &nbsp;
  <img src="docs/mobile/iphone-portrait.jpg" width="22%" alt="AetherSDR Mobile on iPhone in portrait"/>
</p>

**[User manual](docs/mobile/MANUAL.md)** — every function, with screenshots
(also the app's *Manual* button).

## Why

The desktop AetherSDR already runs on a Raspberry Pi. An iPad or iPhone has
more than enough CPU for the same engine, a better screen and a touch
interface. A tablet in the shack, or a phone on the couch, can be the whole
radio interface, with no computer running alongside.

## Features

**Receive**
- Live panadapter and waterfall, or the 3D stacked trace: the desktop's
  3DSS renderer and shaders, drawn on the GPU through Metal. Spectrum,
  waterfall and 3D levels follow the noise floor as on the desktop. Tap to tune (snapped to the tuning step). Drag inside the
  filter passband to tune, anywhere else to move the view, as on the
  desktop; while tuning, holding the finger at either edge keeps scrolling,
  faster nearer the edge. Pinch or use −/+ (bottom left of the waterfall)
  to zoom around the slice. A frequency scale runs along the top of the
  waterfall, as on the desktop.
- Your own transmission shows in the waterfall.
- dBm scale on the spectrum: drag it up or down to move the trace, as
  the reference level in SmartSDR and AetherSDR.
- Waterfall black level and colour gain, the spectrum's reference level
  and range (or Auto), and the 3D floor, gain and smoothing in the View
  tab, remembered on the device; plus the radio's own FFT average, FFT
  frames/s and waterfall speed.
- **Band** beside −/+ zooms out to the whole band.
- RF gain; the radio's memory channels (recall, save, delete).
- The radio's global, TX and mic profiles: load, save and delete, as in
  SmartSDR and AetherSDR.
- Filter passband drawn on the display, on the correct side for USB/LSB and
  centred for CW/AM.
- Mode: tap the mode next to the frequency for the mode buttons.
- **STEP** under the mode: the tuning step, from the radio's step list.
- Info panel next to the frequency, after the slice flag of SmartSDR-style
  clients: the settings that are always there first (slice letter and TX
  tag, RX and TX antenna, filter width, AGC, speaker, tuning lock), then
  split and the DSP flags (lit when on). Each is a
  touch target with its choices behind it: the antennas open their
  chooser; the width and the flags the mode's eight filter widths and the
  radio's DSP (NR, NB, ANF, APF in CW, NRL/ANFL/ANFT and NRS/RNN/NRF where
  the radio has them, and WNB; every one the radio offers is listed, lit
  when on), with a level slider for each DSP that is on; AGC its modes, AGC
  threshold and RF gain; the speaker every slice's volume, mute and pan;
  the lock locks.
- Connected stations (multiFLEX): every program on the radio, and
  disconnecting one to free its slices.
- **Split**, as SmartSDR and AetherSDR: SPLIT on the info panel adds a TX
  slice 1 kHz (CW) or 5 kHz up, muted, with transmit and focus to tune the
  offset; tap again to end it. Press and hold SPLIT for +1, +5 or +10 kHz.
- RF gain shown at the top right of the spectrum; a preamp or attenuator
  step shows on the waterfall and trace, as on the desktop.
- **Beat cancel (BC)**, as on Kenwood rigs: the app finds steady carriers
  in the receive audio (someone tuning up on the frequency) and notches
  them out within ~0.15 s, leaving voice alone. Voice modes only.
- RIT and XIT −/+ run while held, faster the longer.
- Notices and refusals pop up in the middle of the display.
- Two meters on the slice row, SmartSDR style. Tap either one to choose S,
  Power, SWR, ALC, Mic, Compression, PA temperature or supply voltage,
  separately for receive and transmit (e.g. S and Power, then SWR and Power
  on the air). Each one a bar or an analog gauge (arc, needle, S1–S9 and
  +20…+60 in red). The choices are remembered. Each meter holds its
  peak for 1.5 s (amber needle or tick).
- Band buttons (160 m … 6 m, WWV, GEN) use the radio's own band stack, so
  each band comes back to its last frequency and mode.
- Step buttons from ±1 Hz to ±1 kHz land on round frequencies: +10 Hz from
  14.100002 goes to 14.100010, not 14.100012.
- RIT and XIT, squelch (FM/AM).
- Several slices and panadapters: add, select, close, choose the TX slice
  (marked with a red TX tag); other slices are marked on the display and tap
  to select.
- Direct frequency entry from a keypad or a hardware keyboard.
- RX audio on the device, also in the background (home screen, other
  apps). The screen stays awake while connected.

**Transmit**
- TX audio passband (low and high cut) and the radio's 8-band RX and TX
  equalizer, as in SmartSDR.
- TUNE carrier with tune power, MOX, the radio's ATU (tune or bypass,
  memories) and the speech processor (PROC NOR/DX/DX+), VOX (level, delay)
  and DEXP when the radio's own microphone is in use, AM carrier, monitor
  and radio mic level.
- **Voice keyer**: twelve voice macros F1–F12. With SmartSDR+ they are the
  radio's DVK; without it the app records and plays them itself, kept on
  the device.
- Hold-to-talk PTT from the device's microphone (the radio's mic input is
  set to `PC` when the app connects), with RF power, mic gain and mic level, plus forward power and SWR
  while transmitting. The microphone is open only while PTT is held.
- CW: in CW modes the PTT button is the straight key (radio-side keyer and
  sidetone); editable memories ({CALL} = your callsign) and CWX text
  sent by the radio at the set speed; speed, pitch and break-in, plus the CW decoder
  with the decoded text along the top of the waterfall (what you send shows
  there too). Twelve memories, also on F1–F12 from a keyboard (Esc stops),
  and a Live mode that sends letters as you type; the callsign {CALL} sends
  can be set in the app (written to the radio).
- Safety first. Keying goes through AetherSDR's `TxController`, and every TX
  audio packet passes the engine's `TxCoordinator` dispatch fence. The
  transmitter unkeys on release, on disconnect, when the app loses focus,
  when the layout changes and when the mic input leaves the device.

**Remote (SmartLink)**
- Sign in with your FlexRadio SmartLink account and connect to your radio
  from anywhere, with Opus-compressed audio. The sign-in is kept in the iOS
  Keychain on this device only; the password is never stored.

**Made for the device**
- A tablet layout and a phone layout. They switch live on rotation, Split
  View, Stage Manager and folding (iPhone Duo).
- The waterfall gets the screen. On iPhone, the controls (DSP, Audio, TX,
  CW, View, Profiles, Memory) live in a drawer that slides in over it when needed. On iPad the side
  panel can be hidden into the same drawer. PTT/KEY always stays on screen:
  the drawer carries it at the bottom, never covering it.
- Apple Pencil drags tune 10× finer, for zero-beating.
- Magic Keyboard and trackpad: arrows tune, Space is PTT, +/− zoom, F opens
  frequency entry, scroll tunes and ⌘-scroll zooms.
- Safe-area aware: notch, Dynamic Island and the home indicator.
- Network quality in the top row (as the desktop's indicator), with
  link timing, packet loss per stream and traffic on tap.
- **Radio Setup** (top row): callsign, radio name, remote, transmit limits
  and timing, microphone, CW keyer and the radio's audio outputs; offered
  once when a radio has no callsign yet (a new radio).
- App version and build in the top row; **Manual** opens the
  [user manual](docs/mobile/MANUAL.md).
- Built-in demo radio, so you can try it without a radio.
- In-app **Log** with Copy, so testers can report exactly what happened.

<p align="center">
  <img src="docs/mobile/iphone-landscape.jpg" width="56%" alt="iPhone landscape layout"/>
  &nbsp;
  <img src="docs/mobile/ipad-mini-portrait.jpg" width="25%" alt="iPad mini portrait layout"/>
</p>
<p align="center">
  <img src="docs/mobile/iphone-landscape-drawer.jpg" width="56%" alt="iPhone landscape with the controls drawer slid in over the waterfall"/>
  &nbsp;
  <img src="docs/mobile/passband-lsb.jpg" width="38%" alt="LSB filter passband: fill over the spectrum, edge lines through the waterfall"/>
</p>

<sub>Screenshots are rendered by
[`mobile/tools/update-screenshots.sh`](mobile/tools/update-screenshots.sh) and
refreshed with every user-interface change.</sub>

## Status

| Area | State |
|---|---|
| Connect by IP, slice and panadapter, spectrum and waterfall, S-meter | ✅ Tested against a FLEX on SmartSDR API 1.4 |
| Mode, filters, DSP, AGC, band stack, RF power and mic level | ✅ Tested against a FLEX |
| iPad and iPhone builds, signing, TestFlight delivery | ✅ Automated in GitHub Actions |
| RX audio on device | ✅ In daily use by testers, also with Bluetooth headsets |
| Voice transmit on air (device mic, Bluetooth headset) | ✅ Used on air by testers. Test at low power or into a dummy load first. |
| SmartLink remote (sign in, connect from anywhere, Opus audio) | ✅ Supported |
| Remote over your own VPN (connect by IP) | ✅ In use by testers (OpenVPN, WireGuard) |
| Several slices and panadapters, multiFLEX | ✅ Supported |
| CW keying, CWX and decoder on air | ⏳ Not yet verified |
| LAN discovery on iOS | ⏳ Needs Apple's multicast entitlement. Connect by IP for now. |

**Next:** CW on air, and offering the engine-side work
(beat cancel, packet-loss tracking) upstream; see
[discussion #6409](https://github.com/aethersdr/AetherSDR/discussions/6409).

## Try it

- **TestFlight beta.** Ask for an invitation at oh6oksa@gmail.com. You need
  iPadOS or iOS 18 or newer and a FlexRadio: on the same network, through
  SmartLink from anywhere, or over your own VPN.
- **Build it yourself.** Linux or macOS works for development with the
  built-in demo radio; see [`mobile/README.md`](mobile/README.md):

  ```bash
  scripts/setup/setup-qt.sh
  cmake -B build-mobile -DAETHER_MOBILE=ON
  cmake --build build-mobile --target aether-mobile --parallel
  build-mobile/mobile/aether-mobile --demo
  ```

## How it is built

- **Same engine as the desktop.** The new CMake option `AETHER_MOBILE`
  builds `libaethercore` without QtWidgets or desktop peripherals. Desktop
  builds are unchanged, and the option is documented in
  [BUILD-OPTIONS.md](BUILD-OPTIONS.md).
- **New touch UI** in Qt Quick/QML under [`mobile/`](mobile/): the engine
  composition (`MobileEngine`), the spectrum and waterfall view (`SpectrumItem`), and the
  tablet and phone pages.
- **iOS support in core** is limited to platform guards. macOS-only
  IOKit/CoreAudio code sits behind `Q_OS_MACOS`, process helpers behind
  `QT_CONFIG(process)`, and the unused FFmpeg media plugin is excluded.
- **CI** ([`mobile-ios.yml`](.github/workflows/mobile-ios.yml)) builds on
  GitHub's macOS 26 runners with Xcode 26. It cross-compiles FFTW, archives
  the app and signs it with cloud-managed distribution signing through an App
  Store Connect API key. Each push uploads a build to TestFlight. No Mac is
  needed.
- Developed with Claude Code, following the upstream
  [constitution](CONSTITUTION.md) and [AGENTS.md](AGENTS.md) conventions.
  Commits are signed.

## Relationship to AetherSDR

The radio, DSP and protocol engine is AetherSDR's: this fork adds a mobile
build option, iOS platform guards and a new touch client on top, plus a few
engine additions that are being offered upstream: beat cancel (BC) and
per-stream VITA packet-loss tracking. For the
desktop application (Linux, macOS, Windows), downloads, supported radios and
the full feature list, see the upstream project:
**https://github.com/aethersdr/AetherSDR**

Upstream issue [#3128](https://github.com/aethersdr/AetherSDR/issues/3128)
plans a mobile client (AetherLink). This port is a standalone alternative,
and its QML client could also become an AetherLink thin client later.

## Support

AetherSDR Mobile is free. If it is useful to you and you would like to buy
me a beer, you can do it through
**[PayPal](https://www.paypal.me/NikoOksa)**. The desktop project that
makes all of this possible takes support through
[Open Collective](https://opencollective.com/aethersdr).

## Privacy, license, contact

- Privacy: the app collects no data. See [mobile/PRIVACY.md](mobile/PRIVACY.md).
- License: GPLv3, like AetherSDR ([LICENSE](LICENSE), [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES)).
- Contact: Niko Oksa, OH6OKSA, oh6oksa@gmail.com. 73!
