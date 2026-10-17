#include "ScreenAwake.h"

#include <QCoreApplication>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace AetherSDR::mobile {

// Android: the window's keep-screen-on flag, and the RadioService foreground
// service (notification, wake and Wi-Fi locks) so receive keeps running while
// another app is in front.
void setKeepScreenAwake(bool awake)
{
    static constexpr jint kFlagKeepScreenOn = 0x00000080;  // WindowManager.LayoutParams
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([awake] {
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        const QJniObject window = activity.callMethod<jobject>("getWindow", "()Landroid/view/Window;");
        if (window.isValid()) {
            window.callMethod<void>(awake ? "addFlags" : "clearFlags", "(I)V", kFlagKeepScreenOn);
        }
    });
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>("fi/oh6oksa/oksasdr/RadioService", awake ? "start" : "stop",
                                       "(Landroid/content/Context;)V", context.object());
}

} // namespace AetherSDR::mobile
