#pragma once

namespace AetherSDR::mobile {

// iOS: one play-and-record audio session for the whole app, set up before
// any audio starts. Receive audio keeps playing in the background (with the
// "audio" background mode), the microphone works without a category switch
// mid-stream, output goes to the speaker (or Bluetooth / headphones), and
// the session comes back after a call or Siri. A no-op elsewhere.
void configureAudioSession();

} // namespace AetherSDR::mobile
