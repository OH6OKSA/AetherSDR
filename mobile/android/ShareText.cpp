#include "ShareText.h"

#include "AndroidJni.h"

#include <QClipboard>
#include <QGuiApplication>

namespace AetherSDR::mobile {

void copyText(const QString& text)
{
    QGuiApplication::clipboard()->setText(text);
}

// Android: the system share sheet (Gmail, Messages, …).
bool shareText(const QString& subject, const QString& text)
{
    return QJniObject::callStaticMethod<jboolean>(kPlatformClass, "share",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        androidContext().object(), QJniObject::fromString(subject).object<jstring>(),
        QJniObject::fromString(text).object<jstring>());
}

} // namespace AetherSDR::mobile
