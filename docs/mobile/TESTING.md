# Joining the AetherSDR Mobile test group

AetherSDR Mobile is tested through Apple's **TestFlight** by a small group of
amateur radio operators. This page explains what you need, what to send, and
what happens after you join.

## What you need

- **An iPhone or iPad** with iOS or iPadOS 18 or newer.
- **A FlexRadio.** The FLEX-6400, FLEX-6600 and FLEX-6600M are tested so
  far; other FLEX-6000/8000 models and Aurora should work. If yours is one of
  those, say so: reports on new models are especially welcome.
- **A way for the device to reach the radio**, any of these:
  - the same home network (enter the radio's IP address in the app);
  - **SmartLink**, from anywhere (sign in with your FlexRadio account);
  - your own VPN (WireGuard, OpenVPN, Tailscale, …).
- For transmit tests, a **dummy load** or low power, at least at first.

## How to join

Send an email to **oh6oksa@gmail.com** with the subject
**"AetherSDR Mobile tester"** and these details:

1. **Your name and callsign.**
2. **The email address for the TestFlight invitation.** You can accept it
   with any Apple Account; it is easiest if it is the one you use on the
   device.
3. **Your device or devices**: model (e.g. iPhone 15, iPad Pro 12.9") and
   iOS/iPadOS version (Settings → General → About).
4. **Your radio**: model and SmartSDR firmware version (e.g. FLEX-6600M,
   4.2.18).
5. **How you will connect**: home network, SmartLink, or a VPN (which one).
6. **What you would like to test**: receive, phone (voice) transmit, CW,
   Bluetooth headset, keyboard, iPad and iPhone layouts, …

Template to copy:

```text
Subject: AetherSDR Mobile tester

Name and callsign:
Email for the TestFlight invitation:
Device(s) and iOS/iPadOS version:
Radio model and firmware version:
Connection (home network / SmartLink / VPN, which):
What I would like to test:
```

## What happens next

1. You receive an email invitation from TestFlight.
2. Install the free **TestFlight** app from the App Store, then open the
   invitation on your device and tap **Accept** and **Install**.
3. New builds arrive in TestFlight; it shows what is new in each one
   (*What to Test*). Turn on automatic updates in TestFlight, or update by
   hand.
4. Each build works for 90 days, so keep updating.

The [user manual](MANUAL.md) (also the app's **Manual** button) describes
every function.

## While testing

- **Start with receive.** Connect, check the spectrum, waterfall and audio.
- **Transmit at low power or into a dummy load first.** The app takes the
  radio's mic input to `PC` (this device's microphone) when it connects.
- **Reporting a problem:** right after it happens, open **Log** in the top
  row, tap **Copy**, and paste the text into an email to oh6oksa@gmail.com.
  Add the device model and what you were doing. The build number is in the
  app's top row. Screenshots or a screen recording help a lot.
- Leave **Log → Developer link** off; it is only for the developer's own
  devices.

## Privacy

The app collects no data ([privacy policy](../../mobile/PRIVACY.md)). Your
email address is used only to send the TestFlight invitation. As with any
TestFlight app, Apple shows the developer your name and email address, your
device model and iOS version, and the crash reports and feedback you choose
to send through TestFlight.

## Leaving the test group

Delete the app, or open TestFlight → AetherSDR Mobile → **Stop Testing**.
Send an email if you would like your address removed from the tester list.

This is an unofficial community build, not affiliated with FlexRadio and not
an official AetherSDR release. 73!
