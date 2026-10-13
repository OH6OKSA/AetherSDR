#include "LogBuffer.h"
#include "MobileEngine.h"

#include "core/AppSettings.h"
#include "core/QtAudioBackendGuard.h"
#include "models/Nr2SettingsModel.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char* argv[])
{
    AetherSDR::QtAudioBackendGuard::applyAtStartup();

    QGuiApplication app(argc, argv);
    AetherSDR::mobile::LogBuffer::install();
    // Settings and config paths derive from these names; the mobile client
    // keeps its own settings, apart from a desktop install on the same host.
    QGuiApplication::setApplicationName(QStringLiteral("AetherSDR-Mobile"));
    QGuiApplication::setOrganizationName(QStringLiteral("AetherSDR"));
    QGuiApplication::setApplicationVersion(QStringLiteral(AETHERSDR_VERSION));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption demoOption(QStringLiteral("demo"),
        QStringLiteral("Connect to the built-in demo radio as soon as it is listed."));
    parser.addOption(demoOption);
    const QCommandLineOption connectOption(QStringLiteral("connect"),
        QStringLiteral("Connect to the FlexRadio at <address> on start."), QStringLiteral("address"));
    parser.addOption(connectOption);
    const QCommandLineOption modeOption(QStringLiteral("mode"),
        QStringLiteral("With --demo: set the demo slice to <mode> after connecting (screenshots)."),
        QStringLiteral("mode"));
    parser.addOption(modeOption);
    const QCommandLineOption controlsOption(QStringLiteral("controls"),
        QStringLiteral("Control panel: visible or hidden (remembered), or drawer to also open the drawer (screenshots)."),
        QStringLiteral("state"));
    parser.addOption(controlsOption);
    const QCommandLineOption controlsTabOption(QStringLiteral("controls-tab"),
        QStringLiteral("With --controls drawer: tab to show (0 Mode … 4 CW)."), QStringLiteral("index"));
    parser.addOption(controlsTabOption);
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
        QStringLiteral("Save the window to <file> after <ms> (default 8000), muted, then quit."),
        QStringLiteral("file"));
    const QCommandLineOption screenshotDelayOption(QStringLiteral("screenshot-delay"),
        QStringLiteral("Delay before --screenshot, in milliseconds."), QStringLiteral("ms"),
        QStringLiteral("8000"));
    parser.addOption(screenshotOption);
    parser.addOption(screenshotDelayOption);
    const QCommandLineOption sizeOption(QStringLiteral("size"),
        QStringLiteral("Window size in points, e.g. 1024x768, to check other iPad layouts."),
        QStringLiteral("WxH"));
    parser.addOption(sizeOption);
    parser.process(app);

    // Settings load before any model or the AudioEngine is constructed.
    AetherSDR::AppSettings::instance().load();
    static_cast<void>(AetherSDR::Nr2SettingsModel::instance());
    AetherSDR::AppSettings::instance().initializeGuiClientIdentity();

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    AetherSDR::mobile::MobileEngine engine;

    if (parser.isSet(controlsOption)) {
        engine.setControlsVisible(parser.value(controlsOption) == QLatin1String("visible"));
    }

    QQmlApplicationEngine qml;
    QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    qml.loadFromModule("AetherMobile", "Main");

    if (parser.isSet(sizeOption) && !qml.rootObjects().isEmpty()) {
        const QStringList wh = parser.value(sizeOption).split(QLatin1Char('x'));
        if (auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().constFirst()); window && wh.size() == 2) {
            window->resize(wh.at(0).toInt(), wh.at(1).toInt());
        }
    }

    if (parser.isSet(demoOption)) {
        engine.connectToDemoWhenListed();
        if (parser.isSet(modeOption)) {
            const QString mode = parser.value(modeOption);
            QObject::connect(&engine, &AetherSDR::mobile::MobileEngine::modesChanged, &engine,
                             [&engine, mode] { engine.setMode(mode); });
        }
    } else if (parser.isSet(connectOption)) {
        engine.connectToAddress(parser.value(connectOption));
    }

    if (parser.value(controlsOption) == QLatin1String("drawer")) {
        const QString tab = parser.value(controlsTabOption);
        QTimer::singleShot(6000, &app, [&qml, tab] {
            for (QObject* root : qml.rootObjects()) {
                if (QObject* drawer = root->findChild<QObject*>(QStringLiteral("controlsDrawer"))) {
                    QMetaObject::invokeMethod(drawer, "open");
                }
                if (!tab.isEmpty()) {
                    for (QObject* tabs : root->findChildren<QObject*>(QStringLiteral("controlsTabs")))
                        tabs->setProperty("currentIndex", tab.toInt());
                }
            }
        });
    }

    if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        // Screenshots run against a real radio: change nothing on it.
        engine.setMicDefaultEnabled(false);
        QObject::connect(&engine, &AetherSDR::mobile::MobileEngine::connectionStateChanged, &engine,
                         [&engine] { engine.setMuted(true); });
        QTimer::singleShot(parser.value(screenshotDelayOption).toInt(), &app, [&qml, path] {
            auto* window = qml.rootObjects().isEmpty()
                ? nullptr : qobject_cast<QQuickWindow*>(qml.rootObjects().constFirst());
            const bool saved = window && window->grabWindow().save(path);
            QCoreApplication::exit(saved ? 0 : 2);
        });
    }

    return app.exec();
}
