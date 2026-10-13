#include "SecureStore.h"

namespace AetherSDR::mobile {

bool saveSecret(const QString&, const QString&)
{
    return false;
}

QString loadSecret(const QString&)
{
    return {};
}

void removeSecret(const QString&)
{
}

bool secureStoreAvailable()
{
    return false;
}

} // namespace AetherSDR::mobile
