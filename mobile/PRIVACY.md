# OksaSDR: privacy policy

OksaSDR is a FlexRadio client for iPad and iPhone, built on the
[AetherSDR](https://github.com/aethersdr/AetherSDR) engine. It is
maintained by Niko Oksa (OH6OKSA). This policy covers the app distributed
through TestFlight from
[this source](https://github.com/OH6OKSA/OksaSDR/tree/mobile/oksasdr).

## Summary

The app collects no personal data. Nothing you do in the app is sent to the
developer or to any third party.

## Network use

The app talks only to equipment on your own network:

- **Your radio.** It connects to the IP address you enter, or to a radio
  discovered on your local network. Control commands, spectrum, waterfall and
  audio travel directly between your device and your radio. They go through a
  VPN only if you route them that way.
- **Local network discovery.** It listens for radio and accessory discovery
  broadcasts on the local network. iOS asks for permission ("Local Network")
  before this is allowed.

The app contains no analytics, advertising, tracking or crash-reporting SDKs,
and it makes no other internet connections.

## Microphone

The microphone is used only to transmit through your radio, and it is open
only while you hold PTT. Transmitting from the device requires the radio's mic
input to be set to this device ("Use this device's microphone"). The audio
goes only to your radio and is not recorded or stored. iOS asks for
microphone permission first.

## Camera

The app never uses the camera. iOS requires a camera notice because the audio
library the app includes also contains camera functions.

## Data stored on your device

The app keeps its settings in its own storage on your device. These are the
last radio address you entered, mic gain and similar operating preferences,
and an identifier the radio uses to recognise this client. They never leave
your device, except as part of the normal connection to your radio. The
in-app Log is kept in memory only. It is shared only if you copy it yourself.

Deleting the app deletes all of this data.

## TestFlight

Apple's TestFlight service may collect usage information, crash reports and
any feedback you choose to send, as described in Apple's TestFlight terms and
privacy policy. If you send feedback, the developer receives it through App
Store Connect. That information is used only to fix problems in the app.

## Children

The app is a tool for licensed radio amateurs and is not directed at
children.

## Contact

Questions about this policy: oh6oksa@gmail.com

Last updated: 2026-10-12
