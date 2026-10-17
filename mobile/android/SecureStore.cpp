#include "SecureStore.h"

#include "AndroidJni.h"

namespace AetherSDR::mobile {

// Android: AES-GCM under a key held in the Android Keystore (Platform.java);
// only the ciphertext is stored, in app-private preferences.
bool saveSecret(const QString& key, const QString& value)
{
    return QJniObject::callStaticMethod<jboolean>(kPlatformClass, "saveSecret",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        androidContext().object(), QJniObject::fromString(key).object<jstring>(),
        QJniObject::fromString(value).object<jstring>());
}

QString loadSecret(const QString& key)
{
    return QJniObject::callStaticObjectMethod(kPlatformClass, "loadSecret",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        androidContext().object(), QJniObject::fromString(key).object<jstring>()).toString();
}

void removeSecret(const QString& key)
{
    QJniObject::callStaticMethod<void>(kPlatformClass, "removeSecret",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        androidContext().object(), QJniObject::fromString(key).object<jstring>());
}

bool secureStoreAvailable()
{
    return true;
}

} // namespace AetherSDR::mobile
