#pragma once

namespace AetherSDR::mobile {

// Keeps the display from auto-locking while a radio is connected (iOS idle
// timer; Android keep-screen-on flag). On Android it also runs the
// connection's foreground service, so receive continues in the background.
// A no-op on platforms without either.
void setKeepScreenAwake(bool awake);

} // namespace AetherSDR::mobile
