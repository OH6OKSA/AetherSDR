#pragma once

namespace AetherSDR::mobile {

// Keeps the display from auto-locking while a radio is connected (iOS idle
// timer). A no-op on platforms without one.
void setKeepScreenAwake(bool awake);

} // namespace AetherSDR::mobile
