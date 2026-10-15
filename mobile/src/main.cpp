#include "AudioSession.h"
#include "DevLink.h"
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
#include <QQuickItem>
#include <QTimer>

int main(int argc, char* argv[])
{
    AetherSDR::QtAudioBackendGuard::applyAtStartup();

    QGuiApplication app(argc, argv);
    AetherSDR::mobile::LogBuffer::install();
    // Before any audio device opens: play-and-record session (iOS).
    AetherSDR::mobile::configureAudioSession();
    // Settings and config paths derive from these names; the mobile client
    // keeps its own settings, apart from a desktop install on the same host.
    QGuiApplication::setApplicationName(QStringLiteral("AetherSDR-Mobile"));
    QGuiApplication::setOrganizationName(QStringLiteral("AetherSDR"));
    QGuiApplication::setApplicationVersion(QStringLiteral(AETHERSDR_VERSION));
    qInfo().noquote() << QStringLiteral("AetherSDR Mobile %1 (build %2)")
                             .arg(QStringLiteral(AETHERSDR_VERSION), QStringLiteral(AETHER_MOBILE_BUILD_NUMBER));

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
    const QCommandLineOption reconnectTestOption(QStringLiteral("reconnect-test"),
        QStringLiteral("With --connect: disconnect after 8 s and reconnect at 11 s."));
    parser.addOption(reconnectTestOption);
    const QCommandLineOption frequencyDialogOption(QStringLiteral("frequency-dialog"),
        QStringLiteral("Open the frequency entry dialog (screenshots)."));
    parser.addOption(frequencyDialogOption);
    const QCommandLineOption meterChooserOption(QStringLiteral("popup"),
        QStringLiteral("Open a chooser popup by object name, e.g. meterChooser or modeChooser (screenshots)."),
        QStringLiteral("name"));
    parser.addOption(meterChooserOption);
    const QCommandLineOption previewNoSliceOption(QStringLiteral("preview-no-slice"),
        QStringLiteral("Show the no-slice notice over the display (screenshots)."));
    parser.addOption(previewNoSliceOption);
    const QCommandLineOption spectrum3dOption(QStringLiteral("spectrum3d"),
        QStringLiteral("2D waterfall or 3D stacked trace: on or off (remembered)."), QStringLiteral("state"));
    parser.addOption(spectrum3dOption);
    const QCommandLineOption analogMetersOption(QStringLiteral("analog-meters"),
        QStringLiteral("Meter faces: on (analog) or off (bars) (remembered)."), QStringLiteral("state"));
    parser.addOption(analogMetersOption);
    const QCommandLineOption metersOption(QStringLiteral("meters"),
        QStringLiteral("Receive meter choices <left>,<right>, e.g. S,Volts (remembered)."), QStringLiteral("kinds"));
    parser.addOption(metersOption);
    const QCommandLineOption devLinkOption(QStringLiteral("devlink"),
        QStringLiteral("Developer link to <host>, or off (remembered)."), QStringLiteral("host"));
    parser.addOption(devLinkOption);
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
    if (parser.isSet(devLinkOption)) {
        const QString host = parser.value(devLinkOption);
        if (host == QLatin1String("off")) {
            AetherSDR::mobile::DevLink::instance()->setEnabled(false);
        } else {
            AetherSDR::mobile::DevLink::instance()->setHost(host);
            AetherSDR::mobile::DevLink::instance()->setEnabled(true);
        }
    }
    AetherSDR::mobile::DevLink::instance()->attach(&engine, &qml);

    // Frames drawn, for the performance read-out.
    if (!qml.rootObjects().isEmpty()) {
        if (auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().constFirst())) {
            QObject::connect(window, &QQuickWindow::frameSwapped, &engine,
                             [&engine] { engine.countFrame(); }, Qt::DirectConnection);
        }
    }
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
        const QString address = parser.value(connectOption);
        engine.connectToAddress(address);
        if (parser.isSet(reconnectTestOption)) {
            // Disconnect at 8 s and connect again at 11 s (screenshot later).
            QTimer::singleShot(8000, &engine, [&engine] { engine.disconnectRadio(); });
            QTimer::singleShot(11000, &engine, [&engine, address] { engine.connectToAddress(address); });
        }
    }

    if (parser.isSet(metersOption)) {
        const QStringList kinds = parser.value(metersOption).split(QLatin1Char(','));
        if (kinds.size() == 2) {
            engine.setMeterLeft(kinds.at(0));
            engine.setMeterRight(kinds.at(1));
        }
    }
    if (parser.isSet(analogMetersOption)) {
        engine.setAnalogMeters(parser.value(analogMetersOption) == QLatin1String("on"));
    }
    if (parser.isSet(spectrum3dOption)) {
        engine.setSpectrum3D(parser.value(spectrum3dOption) == QLatin1String("on"));
    }
    if (parser.isSet(frequencyDialogOption)) {
        QTimer::singleShot(6000, &app, [&qml] {
            for (QObject* root : qml.rootObjects()) {
                if (QObject* dialog = root->findChild<QObject*>(QStringLiteral("frequencyDialog"))) {
                    QMetaObject::invokeMethod(dialog, "open");
                }
            }
        });
    }
    if (parser.isSet(previewNoSliceOption)) {
        QTimer::singleShot(6000, &app, [&qml] {
            for (QObject* root : qml.rootObjects()) {
                for (QObject* notice : root->findChildren<QObject*>(QStringLiteral("noSliceNotice"))) {
                    notice->setProperty("preview", true);
                }
            }
        });
    }
    if (parser.isSet(meterChooserOption)) {
        const QString name = parser.value(meterChooserOption);
        QTimer::singleShot(6000, &app, [&qml, name] {
            for (QObject* root : qml.rootObjects()) {
                // Every match: a page can hold a hidden twin (e.g. the info
                // panel's one-line copy); only the visible one shows.
                for (QObject* chooser : root->findChildren<QObject*>(name)) {
                    const auto* host = qvariant_cast<QQuickItem*>(chooser->property("parent"));
                    if (!host || host->isVisible()) {
                        QMetaObject::invokeMethod(chooser, "open");
                    }
                }
            }
        });
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
        engine.setRedactIdentity(true);
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
