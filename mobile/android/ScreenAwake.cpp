#include "ScreenAwake.h"

#include "AndroidJni.h"

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcAndroid, "aether.mobile.android")

namespace AetherSDR::mobile {

void androidStartup()
{
    QJniObject::callStaticMethod<void>(kPlatformClass, "setMulticast", "(Landroid/content/Context;Z)V",
                                       androidContext().object(), jboolean(true));
    qCInfo(lcAndroid) << "multicast lock held for LAN discovery";
    const QString exit = QJniObject::callStaticObjectMethod(kPlatformClass, "lastExit",
        "(Landroid/content/Context;)Ljava/lang/String;", androidContext().object()).toString();
    if (!exit.isEmpty()) {
        for (const QString& line : exit.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            qCInfo(lcAndroid).noquote() << "previous run ended:" << line;
        }
    }
}

// Android, while a radio is connected: the window's keep-screen-on flag; the
// RadioService foreground service (notification, wake and Wi-Fi locks) so
// receive keeps running while another app is in front; and voice through a
// connected Bluetooth headset's hands-free link, as on iOS.
void setKeepScreenAwake(bool awake)
{
    static constexpr jint kFlagKeepScreenOn = 0x00000080;  // WindowManager.LayoutParams
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([awake] {
        const QJniObject window = androidContext().callMethod<jobject>("getWindow", "()Landroid/view/Window;");
        if (window.isValid()) {
            window.callMethod<void>(awake ? "addFlags" : "clearFlags", "(I)V", kFlagKeepScreenOn);
        }
    });
    QJniObject::callStaticMethod<void>("fi/oh6oksa/oksasdr/RadioService", awake ? "start" : "stop",
                                       "(Landroid/content/Context;)V", androidContext().object());
    const QString headset = QJniObject::callStaticObjectMethod(kPlatformClass, "useHeadset",
        "(Landroid/content/Context;Z)Ljava/lang/String;", androidContext().object(),
        jboolean(awake)).toString();
    if (awake) {
        qCInfo(lcAndroid).noquote() << "Bluetooth headset:"
                                    << (headset.isEmpty() ? QStringLiteral("none (device speaker and mic)") : headset);
    }
}

} // namespace AetherSDR::mobile
