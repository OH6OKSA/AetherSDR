#pragma once

#include <QString>

namespace AetherSDR::mobile {

// Small secrets (the SmartLink refresh token) in the platform's secure
// store: the iOS Keychain. Other platforms keep nothing; plaintext storage
// is never used as a fallback.
bool saveSecret(const QString& key, const QString& value);
QString loadSecret(const QString& key);
void removeSecret(const QString& key);
bool secureStoreAvailable();

} // namespace AetherSDR::mobile
