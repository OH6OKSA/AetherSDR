#include "AudioSession.h"
#include "DevLink.h"
#include "EchoAudit.h"
#include "LogBuffer.h"
#include "MobileEngine.h"
#ifdef Q_OS_ANDROID
#include "../android/AndroidJni.h"
#endif

#include "core/AppSettings.h"
#include "core/QtAudioBackendGuard.h"
#include "models/Nr2SettingsModel.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLoggingCategory>
#include <QGuiApplication>
#include <QSysInfo>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <functional>

// Objects named `name` under `root`, including inside Loader-made items
// (those have no QObject parent, so findChildren() alone misses them):
// the visual tree is walked too.
static QList<QObject*> findNamedObjects(QObject* root, const QString& name)
{
    QList<QObject*> found = root->findChildren<QObject*>(name);
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        for (QObject* child : item->children()) {
            if (child->objectName() == name && !found.contains(child)) {
                found << child;
            }
        }
        for (QQuickItem* child : item->childItems()) {
            walk(child);
        }
    };
    if (auto* window = qobject_cast<QQuickWindow*>(root)) {
        walk(window->contentItem());
    }
    return found;
}

#ifdef Q_OS_ANDROID
Q_LOGGING_CATEGORY(lcFonts, "aether.mobile.android")
// Qt falls back from Roboto only to a few Android fonts, so symbols the UI
// uses (☰ ⋯ ▾ ⌫ ● ✓) drew as boxes. The system's Noto symbol fonts carry
// them: register those files as fallbacks for common-script text.
static void addAndroidSymbolFallbacks()
{
    const QString symbols = QStringLiteral("\u2630\u22EF\u25BE\u232B\u25CF\u2713");
    QStringList families;
    const QDir dir(QStringLiteral("/system/fonts"));
    for (const QString& file : dir.entryList({QStringLiteral("*Symbols*.ttf"), QStringLiteral("*Symbols*.otf")})) {
        const int id = QFontDatabase::addApplicationFont(dir.filePath(file));
        for (const QString& family : QFontDatabase::applicationFontFamilies(id)) {
            if (!families.contains(family)) {
                families << family;
                QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Common, family);
            }
        }
    }
    QString missing;
    for (const QChar c : symbols) {
        bool found = false;
        for (const QString& family : families) {
            found = found || QFontMetrics(QFont(family)).inFont(c);
        }
        if (!found) {
            missing += c;
        }
    }
    qCInfo(lcFonts).noquote() << "symbol fonts:" << families.join(QStringLiteral(", "))
                      << "missing:" << (missing.isEmpty() ? QStringLiteral("none") : missing);
}
#endif

int main(int argc, char* argv[])
{
    AetherSDR::QtAudioBackendGuard::applyAtStartup();

    QGuiApplication app(argc, argv);
    AetherSDR::mobile::LogBuffer::install();
#ifdef Q_OS_ANDROID
    addAndroidSymbolFallbacks();
    AetherSDR::mobile::androidStartup();
#endif
    // Before any audio device opens: play-and-record session (iOS).
    AetherSDR::mobile::configureAudioSession();
    // Settings and config paths derive from these names; the mobile client
    // keeps its own settings, apart from a desktop install on the same host.
    // They stay as they are after the rename to OksaSDR, so settings and
    // voice macros carry over.
    QGuiApplication::setApplicationName(QStringLiteral("AetherSDR-Mobile"));
    QGuiApplication::setOrganizationName(QStringLiteral("AetherSDR"));
    QGuiApplication::setApplicationVersion(QStringLiteral(AETHERSDR_VERSION));
    qInfo().noquote() << QStringLiteral("OksaSDR %1 (build %2)")
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
    const QCommandLineOption dvkTestOption(QStringLiteral("dvk-record-test"),
        QStringLiteral("Development: record 3 s into this empty voice keyer slot, listen to it, then clear it "
                       "(no transmission)."), QStringLiteral("slot"));
    parser.addOption(dvkTestOption);
    const QCommandLineOption tuneTestOption(QStringLiteral("tune-test"),
        QStringLiteral("Development: key TUNE (the radio's tune power) 10 s after start for <ms>, then unkey. "
                       "Transmits: only with the operator's consent."), QStringLiteral("ms"));
    parser.addOption(tuneTestOption);
    const QCommandLineOption pttTestOption(QStringLiteral("ptt-test"),
        QStringLiteral("Development: transmit for <ms> with this device's microphone at 10 W on 14.250 MHz "
                       "(only if this app's slice transmits), logging the Mic meter; power restored after."),
        QStringLiteral("ms"));
    parser.addOption(pttTestOption);
    const QCommandLineOption panDisplayOption(QStringLiteral("pan-display"),
        QStringLiteral("Development: 12 s after start set this app's pan display setting <key>=<value> "
                       "(average, fps, wfRate)."), QStringLiteral("setting"));
    parser.addOption(panDisplayOption);
    const QCommandLineOption echoAuditOption(QStringLiteral("echo-audit"),
        QStringLiteral("Development: change each radio setting a little, check the radio's status brings it "
                       "back, restore it, then quit (no keying; VOX, antennas, MTU, callsign left out)."));
    parser.addOption(echoAuditOption);
    const QCommandLineOption memoryEditTestOption(QStringLiteral("memory-edit-test"),
        QStringLiteral("Development: save a memory \"OksaSDR test\", edit its name, frequency and mode, "
                       "log what the radio holds, remove it, then quit (other memories untouched)."));
    parser.addOption(memoryEditTestOption);
    const QCommandLineOption tnfTestOption(QStringLiteral("tnf-test"),
        QStringLiteral("Development: add (a notch at the passband centre, 200 Hz, deep) or clear "
                       "(remove every notch on the radio)."), QStringLiteral("action"));
    parser.addOption(tnfTestOption);
    const QCommandLineOption spectrum3dOption(QStringLiteral("spectrum3d"),
        QStringLiteral("2D waterfall or 3D stacked trace: on or off (remembered)."), QStringLiteral("state"));
    parser.addOption(spectrum3dOption);
    const QCommandLineOption analogMetersOption(QStringLiteral("analog-meters"),
        QStringLiteral("Meter faces: on (analog) or off (bars) (remembered)."), QStringLiteral("state"));
    parser.addOption(analogMetersOption);
    const QCommandLineOption metersOption(QStringLiteral("meters"),
        QStringLiteral("Receive meter choices <left>,<right>, e.g. S,Volts (remembered)."), QStringLiteral("kinds"));
    parser.addOption(metersOption);
    const QCommandLineOption txMetersOption(QStringLiteral("tx-meters"),
        QStringLiteral("Transmit meter choices <left>,<right>, e.g. SWR,Power (remembered)."), QStringLiteral("kinds"));
    parser.addOption(txMetersOption);
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
    // The radio shows a station name in every client's station list. iOS
    // gives apps no device name (the host name reads "localhost"), so name
    // the station after the app unless the user set one (the app's former
    // default counts as unset).
    {
        const QString station =
            AetherSDR::AppSettings::instance().value(QStringLiteral("StationName")).toString();
        if ((station.isEmpty() || station == QLatin1String("AetherSDR-Mobile"))
            && QSysInfo::machineHostName() == QLatin1String("localhost")) {
            AetherSDR::AppSettings::instance().setValue(QStringLiteral("StationName"), QStringLiteral("OksaSDR"));
        }
    }
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
    if (parser.isSet(txMetersOption)) {
        const QStringList kinds = parser.value(txMetersOption).split(QLatin1Char(','));
        if (kinds.size() == 2) {
            engine.setMeter(QStringLiteral("left"), true, kinds.at(0));
            engine.setMeter(QStringLiteral("right"), true, kinds.at(1));
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
    if (parser.isSet(dvkTestOption)) {
        const int slot = parser.value(dvkTestOption).toInt();
        QTimer::singleShot(10000, &app, [&engine, slot] {
            const QVariantList dvkList = engine.dvkSlots();
            if (slot < 1 || slot > dvkList.size() || dvkList.at(slot - 1).toMap().value(QStringLiteral("seconds")).toDouble() > 0) {
                qWarning() << "dvk-record-test: slot" << slot << "is not empty; not touching it";
                return;
            }
            engine.dvkRecord(slot);
            QTimer::singleShot(3000, &engine, [&engine, slot] { engine.dvkRecord(slot); });
            QTimer::singleShot(5000, &engine, [&engine, slot] {
                qInfo() << "dvk-record-test: slot after recording" << engine.dvkSlots().at(slot - 1);
                engine.dvkPreview(slot);
            });
            QTimer::singleShot(9000, &engine, [&engine, slot] {
                engine.dvkStop();
                engine.dvkClear(slot);
            });
            QTimer::singleShot(11000, &engine, [&engine, slot] {
                qInfo() << "dvk-record-test: slot after clear" << engine.dvkSlots().at(slot - 1);
            });
        });
    }
    if (parser.isSet(echoAuditOption)) {
        auto* audit = new AetherSDR::mobile::EchoAudit(engine, &app);
        QObject::connect(audit, &AetherSDR::mobile::EchoAudit::finished, &app, &QCoreApplication::quit);
        QTimer::singleShot(14000, audit, &AetherSDR::mobile::EchoAudit::start);
    }
    if (parser.isSet(panDisplayOption)) {
        const QStringList kv = parser.value(panDisplayOption).split(QLatin1Char('='));
        if (kv.size() == 2) {
            QTimer::singleShot(12000, &app, [&engine, kv] {
                qInfo() << "pan-display test: before" << engine.panDisplay();
                engine.setPanDisplay(kv.at(0), kv.at(1).toInt());
                QTimer::singleShot(3000, &engine, [&engine] {
                    qInfo() << "pan-display test: after" << engine.panDisplay();
                });
            });
        }
    }
    if (parser.isSet(memoryEditTestOption)) {
        auto findTest = [&engine](const QString& name) {
            for (const QVariant& v : engine.memories()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("name")).toString() == name) {
                    return m;
                }
            }
            return QVariantMap();
        };
        QTimer::singleShot(8000, &app, [&engine] { engine.saveMemory(QStringLiteral("OksaSDR test")); });
        QTimer::singleShot(11000, &app, [&engine, findTest] {
            const QVariantMap m = findTest(QStringLiteral("OksaSDR test"));
            qInfo() << "memory-edit-test: saved" << m;
            if (!m.isEmpty()) {
                engine.editMemory(m.value(QStringLiteral("index")).toInt(), QStringLiteral("OksaSDR edited"), 14.2345,
                                  QStringLiteral("CW"));
            }
        });
        QTimer::singleShot(14000, &app, [&engine, findTest] {
            const QVariantMap m = findTest(QStringLiteral("OksaSDR edited"));
            qInfo() << "memory-edit-test: edited" << m;
            const QVariantMap left = m.isEmpty() ? findTest(QStringLiteral("OksaSDR test")) : m;
            if (!left.isEmpty()) {
                engine.removeMemory(left.value(QStringLiteral("index")).toInt());
            }
        });
        QTimer::singleShot(17000, &app, [&engine] {
            qInfo() << "memory-edit-test: left" << engine.memories().size() << "memories";
            QCoreApplication::quit();
        });
    }
    if (parser.isSet(tuneTestOption)) {
        const int ms = std::clamp(parser.value(tuneTestOption).toInt(), 500, 5000);
        // A clear frequency first (never the 14.100 beacon), and only when
        // this app's own slice is the one that transmits.
        QTimer::singleShot(8000, &app, [&engine] { engine.setFrequencyMhz(14.250); });
        QTimer::singleShot(10000, &app, [&engine, ms] {
            bool ownTx = false;
            for (const QVariant& v : engine.slices()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == engine.activeSliceId()) {
                    ownTx = m.value(QStringLiteral("tx")).toBool();
                }
            }
            if (!ownTx || std::abs(engine.frequencyMhz() - 14.250) > 0.0005) {
                qWarning() << "tune-test: not keying (own slice transmits:" << ownTx
                           << "frequency:" << engine.frequencyMhz() << ")";
                return;
            }
            if (!engine.tuning()) {
                qInfo() << "tune-test: TUNE on for" << ms << "ms";
                engine.toggleTune();
            }
            QTimer::singleShot(ms, &engine, [&engine] {
                if (engine.tuning()) {
                    engine.toggleTune();
                }
                qInfo() << "tune-test: TUNE off";
            });
        });
    }
    if (parser.value(tnfTestOption) == QLatin1String("add")) {
        QTimer::singleShot(5000, &app, [&engine] { engine.addTnf(0.0); });
        QTimer::singleShot(6000, &app, [&engine] {
            const QVariantList list = engine.tnfs();
            if (!list.isEmpty()) {
                const int id = list.last().toMap().value(QStringLiteral("id")).toInt();
                engine.setTnf(id, QStringLiteral("width"), 200);
                engine.setTnf(id, QStringLiteral("depth"), 2);
            }
            qInfo() << "tnf-test: notches" << list;
        });
    } else if (parser.value(tnfTestOption) == QLatin1String("clear")) {
        QTimer::singleShot(5000, &app, [&engine] {
            for (const QVariant& v : engine.tnfs()) {
                engine.removeTnf(v.toMap().value(QStringLiteral("id")).toInt());
            }
        });
        QTimer::singleShot(7000, &app, [&engine] { qInfo() << "tnf-test: left" << engine.tnfs(); });
    }
    if (parser.isSet(pttTestOption)) {
        const int ms = std::clamp(parser.value(pttTestOption).toInt(), 500, 5000);
        QTimer::singleShot(8000, &app, [&engine] { engine.setFrequencyMhz(14.250); });
        QTimer::singleShot(10000, &app, [&engine, ms] {
            bool ownTx = false;
            for (const QVariant& v : engine.slices()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == engine.activeSliceId()) {
                    ownTx = m.value(QStringLiteral("tx")).toBool();
                }
            }
            if (!ownTx || std::abs(engine.frequencyMhz() - 14.250) > 0.0005) {
                qWarning() << "ptt-test: not keying (own slice transmits:" << ownTx
                           << "frequency:" << engine.frequencyMhz() << ")";
                return;
            }
            const int power = engine.rfPower();
            engine.setRfPower(10);
            auto* sampler = new QTimer(&engine);
            QObject::connect(sampler, &QTimer::timeout, &engine, [&engine] {
                qInfo().noquote() << QStringLiteral("ptt-test: tx=%1 micLive=%2 mic=%3 dB peak=%4 dB power=%5 W")
                                         .arg(engine.radioTransmitting()).arg(engine.micMeterLive())
                                         .arg(engine.micLevelDbfs(), 0, 'f', 1).arg(engine.micPeakDbfs(), 0, 'f', 1)
                                         .arg(engine.forwardPowerW(), 0, 'f', 1);
            });
            QTimer::singleShot(1000, &engine, [&engine, sampler, ms, power] {
                qInfo() << "ptt-test: PTT on for" << ms << "ms";
                sampler->start(250);
                engine.setPtt(true, QStringLiteral("ptt-test"));
                QTimer::singleShot(ms, &engine, [&engine, sampler, power] {
                    engine.setPtt(false, QStringLiteral("ptt-test"));
                    qInfo() << "ptt-test: PTT off";
                    QTimer::singleShot(1500, &engine, [&engine, sampler, power] {
                        sampler->stop();
                        engine.setRfPower(power);
                        qInfo() << "ptt-test: power back to" << power;
                    });
                });
            });
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
        // After --controls drawer (6 s) has shown its tab.
        QTimer::singleShot(7000, &app, [&qml, name] {
            for (QObject* root : qml.rootObjects()) {
                // Every match: a page can hold a hidden twin (e.g. the info
                // panel's one-line copy); only the visible one shows.
                for (QObject* chooser : findNamedObjects(root, name)) {
                    const auto* host = qvariant_cast<QQuickItem*>(chooser->property("parent"));
                    if (!host || host->isVisible()) {
                        // A dialog that needs content offers preview() with sample data.
                        const bool hasPreview = chooser->metaObject()->indexOfMethod("preview()") >= 0;
                        QMetaObject::invokeMethod(chooser, hasPreview ? "preview" : "open");
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
