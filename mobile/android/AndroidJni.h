#pragma once

#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace AetherSDR::mobile {

// The app's Android context and the Java helper class (Platform.java).
inline QJniObject androidContext()
{
    return QNativeInterface::QAndroidApplication::context();
}
inline constexpr const char* kPlatformClass = "fi/oh6oksa/oksasdr/Platform";

// Android services kept for the whole run: the multicast lock LAN discovery
// needs. Called once at start.
void androidStartup();

} // namespace AetherSDR::mobile
