#pragma once

#include <QString>

namespace AetherSDR::mobile {

// Puts text on the system pasteboard (iOS: UIPasteboard, which Qt's
// clipboard did not reliably reach for testers).
void copyText(const QString& text);

// iOS: opens the share sheet (Mail, Messages, …) with the text. Elsewhere
// it copies instead. Returns false if no sheet could be shown.
bool shareText(const QString& subject, const QString& text);

} // namespace AetherSDR::mobile
