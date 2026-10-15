#include "MobileEngine.h"

#include <QCoreApplication>

#include "gui/ModeFilterPresets.h"
#include "core/NetworkMtuPolicy.h"
#include "gui/VoiceModeGate.h"

#include "AudioSession.h"

#include "ScreenAwake.h"
#include "SecureStore.h"

#include "core/AppActivity.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/RadioDiscovery.h"
#include "core/backends/IRadioBackend.h"
#include "core/backends/flex/PanadapterStream.h"
#include "core/discovery/RadioDiscoverySource.h"
#include "core/CwSidetoneGenerator.h"
#include "models/CwRxModel.h"
#include "models/EqualizerModel.h"
#include "gui/MemoryCommands.h"
#include "models/CwxModel.h"
#include "models/DecoderAudioModel.h"
#include "models/MeterModel.h"
#include "models/PanadapterModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/XvtrPolicy.h"

#include <QGuiApplication>
#include <QHostAddress>
#include <QPermissions>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QThread>
#include <QDateTime>
#include <QNetworkInterface>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

Q_LOGGING_CATEGORY(lcMobile, "aether.mobile")

namespace AetherSDR::mobile {

namespace {

MobileEngine* s_instance = nullptr;

constexpr double kS9Dbm = -73.0;
// The radio's remote_audio_tx VITA-49 port (FlexLib).
constexpr quint16 kRadioTxAudioPort = 4991;
const QString kLastAddressKey = QStringLiteral("MobileLastRadioAddress");
const QString kControlsVisibleKey = QStringLiteral("MobileControlsVisible");
const QString kDeviceMicSelection = QStringLiteral("PC");
constexpr int kPanPixelsSettleMs = 400;
constexpr int kMinPanYPixels = 200;
const QString kSmartLinkTokenKey = QStringLiteral("smartlink_refresh_token");
// SmartLink UDP port when the radio reports none (FlexLib default).
constexpr quint16 kDefaultWanUdpPort = 4993;
constexpr int kWanReconnectMs = 5000;
// Fraction of the span kept between the slice and the edge while tuning.
constexpr double kPanFollowEdge = 0.1;
// Coalescing interval for pan moves during a drag.
constexpr int kPanMoveMs = 40;
// How long a dragged pan centre is shown before the radio's echo rules again.
constexpr int kPanPreviewHoldMs = 700;
constexpr double kDbPerSUnit = 6.0;

} // namespace

MobileEngine::MobileEngine(QObject* parent)
    : QObject(parent)
{
    Q_ASSERT(!s_instance);
    s_instance = this;
    m_bandPlan.loadPlans();
    if (!bandPlan().isEmpty()) {
        m_bandPlan.setActivePlan(bandPlan());
    }
    RadioModel& rm = m_session.radioModel();
    m_session.setSessionId(1);

    // AudioEngine reads AppSettings in its constructor; main() loads them first.
    m_audioThread = new QThread(this);
    m_audioThread->setObjectName(QStringLiteral("AudioEngine"));
    m_audio = new AudioEngine;
    m_audio->moveToThread(m_audioThread);
    // Receive audio: the chosen buffer, and a line in the log whenever the
    // playback buffer ran dry in the last 10 s (with what the link did), so
    // stutter can be told apart: late packets or too short a buffer.
    QTimer::singleShot(0, this, [this] { applyAudioBuffer(); });
    m_audioDiagTimer.setInterval(10000);
    connect(&m_audioDiagTimer, &QTimer::timeout, this, [this] {
        if (!m_connected || !m_audio) {
            return;
        }
        const quint64 underruns = m_audio->rxBufferUnderrunCount();
        if (underruns > m_lastUnderruns && m_lastUnderruns > 0) {
            const RadioModel& model = m_session.radioModel();
            qCInfo(lcMobile).noquote()
                << QStringLiteral("audio underrun: %1 in 10 s, buffer %2 ms, queued %3 ms, "
                                  "packet gap max %4 ms, jitter %5 ms, buffer mode %6")
                       .arg(underruns - m_lastUnderruns)
                       .arg(m_audio->rxBufferMs(), 0, 'f', 0)
                       .arg(m_audio->rxPlaybackQueuedMs())
                       .arg(model.audioPacketGapMaxMs())
                       .arg(model.audioPacketJitterMs())
                       .arg(audioBuffer());
        }
        m_lastUnderruns = underruns;
    });
    m_audioDiagTimer.start();
    startStreamThread(m_audioThread);

    connect(&rm, &RadioModel::connectionStateChanged, this, &MobileEngine::onConnectionStateChanged);
    connect(&rm, &RadioModel::connectionError, this, [this](const QString& msg) {
        setLastError(msg);
        if (m_connectionState == QLatin1String("connecting")) {
            setConnectionState(QStringLiteral("idle"));
        }
    });
    connect(&rm, &RadioModel::backendRebuilt, this, [this] {
        stopTransmit(QStringLiteral("backend rebuilt"));
        wireRxAudio();
        wireTxPacketSink();
    });
    wireTransmit();
    // Losing the foreground (call, app switch, lock) must never leave the
    // transmitter keyed.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) {
            stopTransmit(QStringLiteral("application inactive"));
        }
        // Nobody sees the display while the app is not in front: stop feeding
        // the spectrum, waterfall and 3D view (audio keeps playing). iOS kills
        // an app whose main thread is busy when it moves to the background
        // (scene-update watchdog, seen on testers' iPads), and it saves power.
        const bool active = state == Qt::ApplicationActive;
        if (active != m_displayActive) {
            m_displayActive = active;
            qCInfo(lcMobile) << "display" << (active ? "resumed" : "paused (app not in front)");
        }
    });
    connect(&rm, &RadioModel::sliceAdded, this, &MobileEngine::onSliceAdded);
    connect(&rm, &RadioModel::sliceRemoved, this, [this](int) { applyBeatCancel(); });
    connect(&rm, &RadioModel::sliceRemoved, this, [this](int id) {
        // The split's TX slice closed (here, another client or the radio):
        // transmit goes back to the RX slice; either side gone ends split.
        if (id == m_splitTxId || id == m_splitRxId) {
            if (id == m_splitTxId) {
                if (SliceModel* rx = m_session.radioModel().slice(m_splitRxId)) {
                    rx->setTxSlice(true);
                }
            }
            qCInfo(lcMobile) << "split off: slice" << id << "closed";
            m_splitRxId = m_splitTxId = -1;
            m_splitPending = false;
        }
        selectActiveSlice();
        emit slicesChanged();
    });
    // A reconnect reuses the previous session's slice and pan objects
    // without sliceAdded/panadapterAdded; these are the signals it does send.
    connect(&rm, &RadioModel::slotOccupancyChanged, this, [this](int) {
        selectActiveSlice();
        emit slicesChanged();
    });
    connect(&rm, &RadioModel::panadapterReclaimed, this, [this](PanadapterModel*) { selectActiveSlice(); });
    // The radio's reason when its interlock refuses transmit (no TX slice,
    // TX antenna, band inhibit...), not just "did not accept".
    connect(&rm, &RadioModel::interlockNotificationRequested, this,
            [this](const QString& message, const QString& key, const QString&) {
        qCWarning(lcMobile).noquote() << "transmit refused:" << message << "(" + key + ")";
        setLastError(message);
    });
    // Which slice transmits changes what PTT/TUNE can do.
    connect(this, &MobileEngine::slicesChanged, this, &MobileEngine::transmitStateChanged);
    connect(&rm, &RadioModel::callsignChanged, this, &MobileEngine::callsignChanged);
    connect(&rm, &RadioModel::networkQualityChanged, this, &MobileEngine::networkChanged);
    connect(&rm, &RadioModel::sliceCreateFailed, this, [this](int limit, const QString&) {
        RadioModel& model = m_session.radioModel();
        // The first slice refused on a pan the radio restored from an earlier
        // session (seen with free slices, and another client working at once):
        // close that pan and start on a fresh one, once per connection.
        if (model.slices().isEmpty() && !m_sliceRecoveryTried && !model.panadapters().isEmpty()) {
            m_sliceRecoveryTried = true;
            m_sliceRecoveryPending = true;
            const QList<PanadapterModel*> pans = model.panadapters();
            qCWarning(lcMobile) << "slice refused on the restored pan"
                                << (pans.isEmpty() ? QString() : pans.first()->panId())
                                << ": recreating the panadapter";
            for (PanadapterModel* pan : pans) {
                model.removePanadapter(pan->panId());
            }
            QTimer::singleShot(500, this, [this] { m_session.radioModel().createPanadapter(); });
            return;
        }
        // With no slice at all the app cannot tune: say why and what helps.
        if (model.slices().isEmpty()) {
            setLastError(tr("The radio did not give this app a slice. Its %1 slices may all be "
                            "in use by other programs (SmartSDR, another station): close one "
                            "there, then try again.").arg(limit));
        } else {
            setLastError(tr("The radio allows %1 slices.").arg(limit));
        }
    });
    connect(&rm, &RadioModel::panadapterLimitReached, this, [this](int limit, const QString&) {
        setLastError(tr("The radio allows %1 panadapters.").arg(limit));
    });
    connect(&rm, &RadioModel::panadapterAdded, this, [this](PanadapterModel* pan) {
        sendPanDimensions(pan->panId());
        // The fresh pan of a slice recovery: put the slice on it.
        if (m_sliceRecoveryPending && m_session.radioModel().slices().isEmpty()) {
            m_sliceRecoveryPending = false;
            qCInfo(lcMobile) << "slice recovery: creating the slice on the new pan" << pan->panId();
            const QString panId = pan->panId();
            QTimer::singleShot(400, this, [this, panId] { m_session.radioModel().addSliceOnPan(panId); });
        }
        selectActiveSlice();
        emit slicesChanged();
    });
    connect(&rm, &RadioModel::panadapterRemoved, this, [this](const QString&) {
        selectActiveSlice();
        emit slicesChanged();
    });
    connect(&rm, &RadioModel::panDimensionsNeeded, this, &MobileEngine::sendPanDimensions);
    m_panPreviewEndTimer.setSingleShot(true);
    m_panPreviewEndTimer.setInterval(kPanPreviewHoldMs + 20);
    // Back to the radio's centre (normally the same by now).
    connect(&m_panPreviewEndTimer, &QTimer::timeout, this, &MobileEngine::panGeometryChanged);
    m_panMoveTimer.setSingleShot(true);
    m_panMoveTimer.setInterval(kPanMoveMs);
    connect(&m_panMoveTimer, &QTimer::timeout, this, [this] {
        if (m_pan && m_pendingPanCenterMhz > 0.0) {
            m_session.radioModel().requestPanCenter(m_pan->panId(), m_pendingPanCenterMhz);
        }
    });
    m_panPixelsTimer.setSingleShot(true);
    m_panPixelsTimer.setInterval(kPanPixelsSettleMs);
    connect(&m_panPixelsTimer, &QTimer::timeout, this, [this] {
        if (m_pan) {
            sendPanDimensions(m_pan->panId());
        }
    });

    connect(&rm, &RadioModel::panFeedSpectrumReady, this,
            [this](quint32 streamId, const QVector<float>& bins, qint64, const SpectrumDecodeScale&) {
        if (m_pan && m_pan->panStreamId() == streamId) {
            ++m_fftFramesSeen;
            if (m_displayActive) {
                emit spectrumFrame(bins);
            }
        }
    });
    connect(&rm, &RadioModel::panFeedWaterfallRowReady, this,
            [this](quint32 streamId, const QVector<float>& bins, double lowMhz, double highMhz,
                   quint32, qint64, bool) {
        if (m_pan && m_pan->wfStreamId() == streamId) {
            ++m_wfRowsSeen;
            if (m_displayActive) {
                emit waterfallRow(bins, lowMhz, highMhz);
            }
        } else {
            m_otherWfStreamSeen = streamId;
        }
    });
    connect(&rm.meterModel(), &MeterModel::sLevelChanged, this, [this](int sliceIndex, float dbm) {
        if (m_slice && m_slice->sliceId() == sliceIndex) {
            m_sMeterDbm = dbm;
            emit sMeterChanged();
        }
    });

    // Opus for remote audio: "Auto" compresses only over WAN (SmartLink),
    // where uncompressed PCM would not fit a mobile link. An explicit choice
    // is kept.
    if (AppSettings::instance().value(QStringLiteral("AudioCompression")).toString().isEmpty()) {
        AppSettings::instance().setValue(QStringLiteral("AudioCompression"), QStringLiteral("Auto"));
        AppSettings::instance().save();
    }
    wireSmartLink();
    wireCw();

    m_discovery = makeLocalRadioDiscoverySource({.local = true, .simulator = true});
    connect(m_discovery.get(), &RadioDiscoverySource::radioChanged, this,
            [this](const DiscoveredRadio& radio) { m_radios.upsert(radio); });
    connect(m_discovery.get(), &RadioDiscoverySource::radioLost, this,
            [this](const QString& family, const QString& serial) { m_radios.remove(family, serial); });
    m_discovery->start();
}

MobileEngine::~MobileEngine()
{
    s_instance = nullptr;
    stopTransmit(QStringLiteral("shutdown"));
    setKeepScreenAwake(false);
    if (m_discovery) {
        m_discovery->stop();
    }
    m_session.radioModel().disconnectFromRadio();
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio] {
        audio->stopRxStream();
        delete audio;
    }, Qt::BlockingQueuedConnection);
    m_audio = nullptr;
    m_audioThread->quit();
    m_audioThread->wait();
}

MobileEngine* MobileEngine::instance()
{
    return s_instance;
}

MobileEngine* MobileEngineForeign::create(QQmlEngine*, QJSEngine*)
{
    MobileEngine* engine = MobileEngine::instance();
    Q_ASSERT(engine);
    QJSEngine::setObjectOwnership(engine, QJSEngine::CppOwnership);
    return engine;
}

void MobileEngine::connectToRadio(int row)
{
    const DiscoveredRadio* radio = m_radios.radioAt(row);
    if (!radio || m_connectionState != QLatin1String("idle")) {
        return;
    }
    RadioInfo info;
    info.family = radio->family;
    info.serial = radio->serial;
    info.name = radio->name;
    info.model = radio->model;
    info.nickname = radio->nickname;
    info.version = radio->version;
    info.address = radio->address.isEmpty() ? QHostAddress(QHostAddress::LocalHost)
                                            : QHostAddress(radio->address);
    if (radio->port != 0) {
        info.port = radio->port;
    }
    connectWithInfo(info, radio->nickname.isEmpty() ? radio->model : radio->nickname);
}

void MobileEngine::connectToAddress(const QString& host)
{
    const QHostAddress address(host.trimmed());
    if (address.isNull()) {
        setLastError(tr("“%1” is not an IP address.").arg(host.trimmed()));
        return;
    }
    if (m_connectionState != QLatin1String("idle")) {
        return;
    }
    RadioInfo info;
    info.family = QStringLiteral("flex");
    info.address = address;
    AppSettings::instance().setValue(kLastAddressKey, address.toString());
    AppSettings::instance().save();
    emit lastAddressChanged();
    connectWithInfo(info, address.toString());
}

void MobileEngine::connectWithInfo(const RadioInfo& info, const QString& displayName)
{
    applyAudioCompression(info.address, false);
    m_wanActive = false;
    m_wanReconnectTimer.stop();
    m_radioName = displayName;
    setLastError({});
    setConnectionState(QStringLiteral("connecting"));
    m_session.radioModel().connectToRadio(info);
}

void MobileEngine::connectToDemoWhenListed()
{
    auto tryConnect = [this] {
        for (int row = 0; row < m_radios.rowCount(); ++row) {
            if (m_radios.radioAt(row)->transport == QLatin1String("sim")) {
                connectToRadio(row);
                return true;
            }
        }
        return false;
    };
    if (tryConnect()) {
        return;
    }
    auto pending = std::make_shared<QMetaObject::Connection>();
    *pending = connect(&m_radios, &RadioListModel::countChanged, this, [pending, tryConnect] {
        if (tryConnect()) {
            disconnect(*pending);
        }
    });
}

void MobileEngine::disconnectRadio()
{
    m_wanUserDisconnect = true;
    m_wanReconnectTimer.stop();
    stopTransmit(QStringLiteral("operator disconnect"));
    m_session.radioModel().disconnectFromRadio();
}

// ---- SmartLink ---------------------------------------------------------------

void MobileEngine::wireSmartLink()
{
    connect(&m_smartLink, &SmartLinkClient::authenticated, this, [this] {
        // Auth0 may rotate the refresh token on every refresh; keep the latest.
        saveSecret(kSmartLinkTokenKey, m_smartLink.refreshToken());
        setSmartLinkState(QStringLiteral("signedIn"));
    });
    connect(&m_smartLink, &SmartLinkClient::authFailed, this, [this](const QString& error) {
        removeSecret(kSmartLinkTokenKey);
        m_wanReconnectTimer.stop();
        setLastError(tr("SmartLink: %1").arg(error));
        setSmartLinkState(QStringLiteral("signedOut"));
    });
    connect(&m_smartLink, &SmartLinkClient::serverConnected, this, [this] {
        setSmartLinkState(QStringLiteral("signedIn"));
    });
    connect(&m_smartLink, &SmartLinkClient::serverDisconnected, this, [this] {
        if (m_smartLink.isAuthenticated()) {
            setSmartLinkState(QStringLiteral("offline"));
        }
    });
    connect(&m_smartLink, &SmartLinkClient::radioListReceived, this,
            [this](const QList<WanRadioInfo>& radios) {
        m_wanRadios = radios;
        emit smartLinkRadiosChanged();
    });
    connect(&m_smartLink, &SmartLinkClient::connectReady, this,
            [this](const QString& handle, const QString& serial) {
        if (!m_wanActive || serial != m_pendingWan.serial) {
            return;
        }
        qCInfo(lcMobile) << "SmartLink connect_ready for" << serial;
        applyAudioCompression(QHostAddress(), true);
        m_wan.connectToRadio(m_pendingWan.publicIp, static_cast<quint16>(m_pendingWan.publicTlsPort), handle);
    });
    // The mismatch fires before WanConnection::connected, so it is taken from
    // the connection itself; the connect stays paused until answered.
    connect(&m_wan, &WanConnection::certFingerprintMismatch, this, &MobileEngine::certificateChanged);
    connect(&m_wan, &WanConnection::errorOccurred, this, [this](const QString& message) {
        setLastError(tr("Remote connection: %1").arg(message));
        if (m_connectionState == QLatin1String("connecting")) {
            setConnectionState(QStringLiteral("idle"));
        }
    });
    connect(&m_wan, &WanConnection::connected, this, [this] {
        if (!m_wanActive) {
            return;
        }
        const quint16 udpPort = m_pendingWan.publicUdpPort > 0
            ? static_cast<quint16>(m_pendingWan.publicUdpPort) : kDefaultWanUdpPort;
        m_session.radioModel().connectViaWan(&m_wan, m_pendingWan.publicIp, udpPort);
    });

    // An unexpected drop of a remote session retries every few seconds until
    // it is back, the user disconnects, or sign-in is needed again.
    m_wanReconnectTimer.setSingleShot(true);
    m_wanReconnectTimer.setInterval(kWanReconnectMs);
    connect(&m_wanReconnectTimer, &QTimer::timeout, this, [this] {
        if (!m_wanActive || m_connected) {
            return;
        }
        if (!m_smartLink.isConnected()) {
            m_smartLink.reconnect();
            m_wanReconnectTimer.start();
            return;
        }
        startWanConnect();
    });

    const QString token = loadSecret(kSmartLinkTokenKey);
    // SmartLinkClient's own QtKeychain store is not built in here (it logs
    // "persistence: disabled"); the app keeps the sign-in itself.
    qCInfo(lcMobile).noquote() << QStringLiteral("SmartLink sign-in kept by the app in the %1: %2")
        .arg(secureStoreAvailable() ? QStringLiteral("iOS Keychain") : QStringLiteral("(no secure store)"),
             token.isEmpty() ? QStringLiteral("none saved") : QStringLiteral("saved"));
    if (!token.isEmpty()) {
        setSmartLinkState(QStringLiteral("signingIn"));
        m_smartLink.loginWithRefreshToken(token);
    }
}

void MobileEngine::setSmartLinkState(const QString& state)
{
    if (state != m_smartLinkState) {
        m_smartLinkState = state;
        qCInfo(lcMobile) << "SmartLink state" << state;
    }
    emit smartLinkChanged();
}

QString MobileEngine::smartLinkAccount() const
{
    const QString name = (m_smartLink.firstName() + QLatin1Char(' ') + m_smartLink.lastName()).trimmed();
    if (!m_smartLink.callsign().isEmpty()) {
        return name.isEmpty() ? m_smartLink.callsign()
                              : QStringLiteral("%1 (%2)").arg(m_smartLink.callsign(), name);
    }
    return name;
}

QString MobileEngine::smartLinkEmail() const
{
    // SmartLinkClient keeps the last e-mail Base64-encoded for pre-filling.
    const QByteArray stored = AppSettings::instance().value(QStringLiteral("SmartLinkEmail")).toByteArray();
    return QString::fromUtf8(QByteArray::fromBase64(stored));
}

QVariantList MobileEngine::smartLinkRadios() const
{
    QVariantList list;
    for (const WanRadioInfo& radio : m_wanRadios) {
        list.append(QVariantMap{
            {QStringLiteral("nickname"), radio.nickname},
            {QStringLiteral("model"), radio.model},
            {QStringLiteral("serial"), radio.serial},
            {QStringLiteral("status"), radio.status},
            {QStringLiteral("callsign"), radio.callsign},
            {QStringLiteral("reachable"), radio.publicTlsPort > 0 && !radio.publicIp.isEmpty()},
        });
    }
    return list;
}

bool MobileEngine::remoteSession() const
{
    return m_connected && m_session.radioModel().isWan();
}

void MobileEngine::signInSmartLink(const QString& email, const QString& password)
{
    if (email.trimmed().isEmpty() || password.isEmpty()) {
        setLastError(tr("Enter your SmartLink e-mail and password."));
        return;
    }
    setLastError({});
    setSmartLinkState(QStringLiteral("signingIn"));
    m_smartLink.login(email.trimmed(), password);
}

void MobileEngine::signOutSmartLink()
{
    m_wanReconnectTimer.stop();
    m_smartLink.logout();
    removeSecret(kSmartLinkTokenKey);
    m_wanRadios.clear();
    emit smartLinkRadiosChanged();
    setSmartLinkState(QStringLiteral("signedOut"));
}

void MobileEngine::connectSmartLinkRadio(int index)
{
    if (index < 0 || index >= m_wanRadios.size() || m_connectionState != QLatin1String("idle")) {
        return;
    }
    const WanRadioInfo& radio = m_wanRadios.at(index);
    if (radio.publicTlsPort <= 0 || radio.publicIp.isEmpty()) {
        setLastError(tr("%1 is not reachable through SmartLink right now.").arg(radio.nickname));
        return;
    }
    m_pendingWan = radio;
    m_wanActive = true;
    m_wanUserDisconnect = false;
    m_radioName = radio.nickname.isEmpty() ? radio.model : radio.nickname;
    setLastError({});
    setConnectionState(QStringLiteral("connecting"));
    startWanConnect();
}

void MobileEngine::startWanConnect()
{
    RadioModel& rm = m_session.radioModel();
    rm.ensureBackendFamily(QStringLiteral("flex"));
    PanadapterStream* ps = rm.panStream();
    if (!ps) {
        setLastError(tr("Remote connection: no radio stream available."));
        setConnectionState(QStringLiteral("idle"));
        return;
    }
    const QHostAddress address(m_pendingWan.publicIp);
    const quint16 udpPort = m_pendingWan.publicUdpPort > 0
        ? static_cast<quint16>(m_pendingWan.publicUdpPort) : kDefaultWanUdpPort;
    // Bind the stream's UDP socket first: its port is the hole-punch port
    // SmartLink hands the radio.
    if (ps->localPort() == 0) {
        QMetaObject::invokeMethod(ps, [ps, address, udpPort] { ps->startWan(address, udpPort); },
                                  Qt::BlockingQueuedConnection);
    }
    qCInfo(lcMobile) << "SmartLink connect request for" << m_pendingWan.serial;
    m_smartLink.requestConnect(m_pendingWan.serial, ps->localPort());
}

void MobileEngine::answerCertificate(bool accept)
{
    if (accept) {
        m_wan.acceptPresentedCert();
    } else {
        m_wan.rejectPresentedCert();
        m_wanActive = false;
        setConnectionState(QStringLiteral("idle"));
    }
}

QString MobileEngine::lastAddress() const
{
    return AppSettings::instance().value(kLastAddressKey).toString();
}

bool MobileEngine::controlsVisible() const
{
    return AppSettings::instance().value(kControlsVisibleKey, QStringLiteral("True")).toString()
        == QLatin1String("True");
}

void MobileEngine::setControlsVisible(bool visible)
{
    if (visible == controlsVisible()) {
        return;
    }
    AppSettings::instance().setValue(kControlsVisibleKey, visible ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    emit controlsVisibleChanged();
}

// ---- transmit -------------------------------------------------------------

void MobileEngine::wireTransmit()
{
    RadioModel& rm = m_session.radioModel();
    // Host-routed PCM has no continuous contract: stamp it at PTT admission.
    const TxCoordinator::Producer hostProducer = rm.registerTxProducer(m_audio);
    connect(&rm, &RadioModel::localTransmitEngaged, this, [this, hostProducer] {
        const TxCoordinator::Context context = m_session.radioModel().captureTxMedia(hostProducer);
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, context] {
            audio->setHostMicrophoneContext(context);
        }, Qt::QueuedConnection);
    });

    // The radio always forces Opus on remote_audio_tx (SmartSDR v1.4.0.0).
    connect(&rm, &RadioModel::remoteTxStreamReady, this, [this](quint32 streamId) {
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, streamId] {
            audio->setRemoteTxStreamId(streamId);
            audio->setOpusTxEnabled(true);
        }, Qt::QueuedConnection);
        emit transmitStateChanged();
    });
    connect(&rm, &RadioModel::txAudioStreamReady, this, [this](quint32 streamId) {
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, streamId] {
            audio->setTxStreamId(streamId);
            audio->setOpusTxEnabled(true);
        }, Qt::QueuedConnection);
    });
    connect(&rm.transmitModel(), &TransmitModel::micStateChanged, this, [this] {
        defaultMicToDevice();
        if (m_session.radioModel().transmitModel().micSelection() != kDeviceMicSelection) {
            releasePtt(QStringLiteral("mic input left this device"));
        }
        emit transmitStateChanged();
    });
    connect(&rm.transmitModel(), &TransmitModel::micInputListChanged, this, [this] {
        defaultMicToDevice();
        emit transmitStateChanged();
    });
    connect(&rm.transmitModel(), &TransmitModel::transmittingChanged, this, &MobileEngine::transmittingChanged);
    connect(&rm.transmitModel(), &TransmitModel::rfPowerChanged, this, &MobileEngine::rfPowerChanged);
    connect(&rm.transmitModel(), &TransmitModel::txFilterCutoffChanged, this, &MobileEngine::txFilterChanged);
    connect(&rm, &RadioModel::globalProfilesChanged, this, &MobileEngine::profilesChanged);
    connect(&rm, &RadioModel::memoryChanged, this, &MobileEngine::memoriesChanged);
    connect(&rm, &RadioModel::memoryRemoved, this, &MobileEngine::memoriesChanged);
    connect(&rm, &RadioModel::memoriesCleared, this, &MobileEngine::memoriesChanged);
    connect(&rm.transmitModel(), &TransmitModel::profileListChanged, this, &MobileEngine::profilesChanged);
    connect(&rm.transmitModel(), &TransmitModel::micProfileListChanged, this, &MobileEngine::profilesChanged);
    connect(&rm.transmitModel(), &TransmitModel::stateChanged, this, &MobileEngine::profilesChanged);
    connect(&rm.transmitModel(), &TransmitModel::micStateChanged, this, &MobileEngine::profilesChanged);
    connect(&rm.equalizerModel(), &EqualizerModel::txStateChanged, this, &MobileEngine::eqChanged);
    connect(&rm.equalizerModel(), &EqualizerModel::rxStateChanged, this, &MobileEngine::eqChanged);
    connect(&rm.meterModel(), &MeterModel::micMetersChanged, this,
            [this](float micLevel, float, float micPeak, float compPeak) {
        m_micLevelDbfs = micLevel;
        m_micPeakDbfs = micPeak;
        m_compressionDb = std::clamp(static_cast<double>(compPeak), 0.0, 25.0);
        emit txMetersChanged();
    });
    connect(&rm.meterModel(), &MeterModel::alcValueChanged, this,
            [this](float value, const QString& unit) {
        m_alcDbfs = value;
        m_alcAvailable = !unit.isEmpty();
        emit txMetersChanged();
    });
    connect(&rm.meterModel(), &MeterModel::hwTelemetryChanged, this,
            [this](float paTemp, float supplyVolts) {
        m_paTempC = paTemp;
        m_supplyVolts = supplyVolts;
        emit txMetersChanged();
    });
    connect(&rm, &RadioModel::radioTransmittingChanged, this, &MobileEngine::txMetersChanged);
    // Tune, ATU and radio-side TX audio state.
    TransmitModel& tm = rm.transmitModel();
    connect(&tm, &TransmitModel::tuneChanged, this, &MobileEngine::tuneStateChanged);
    connect(&tm, &TransmitModel::tuneAvailabilityChanged, this, &MobileEngine::tuneStateChanged);
    connect(&tm, &TransmitModel::tunePowerChanged, this, &MobileEngine::tuneStateChanged);
    connect(&tm, &TransmitModel::hasTunerChanged, this, &MobileEngine::atuChanged);
    connect(&tm, &TransmitModel::hasTunerMemoriesChanged, this, &MobileEngine::atuChanged);
    connect(&tm, &TransmitModel::atuStateChanged, this, [this] {
        const TransmitModel& t = m_session.radioModel().transmitModel();
        const auto status = t.atuStatus();
        if (status == ATUStatus::Successful || status == ATUStatus::OK) {
            m_atuTunedFreqMhz = t.transmitFreq();
        } else if (status == ATUStatus::Bypass
                   || status == ATUStatus::ManualBypass) {
            m_atuTunedFreqMhz = -1.0;
        }
        emit atuChanged();
    });
    connect(&tm, &TransmitModel::micStateChanged, this, &MobileEngine::txAudioChanged);
    connect(&rm.meterModel(), &MeterModel::txMetersChanged, this,
            [this](float fwdPower, float swr, bool swrValid) {
        m_forwardPowerW = fwdPower;
        m_swr = swr;
        m_swrValid = swrValid;
        emit txMetersChanged();
    });
    connect(&rm, &RadioModel::capabilitiesChanged, this, &MobileEngine::applyTxAudioCapabilities);
    // Ask for microphone permission as soon as transmitting from this device
    // is possible, so the iOS prompt never interrupts a PTT press. Asking
    // does not open the microphone.
    connect(this, &MobileEngine::transmitStateChanged, this, [this] {
        if (transmitAvailable()) {
            ensureMicrophonePermission();
        }
    });
    wireTxPacketSink();
}

void MobileEngine::wireTxPacketSink()
{
    disconnect(m_txSinkConnection);
    PanadapterStream* ps = m_session.radioModel().panStream();
    if (!ps) {
        return;
    }
    // Every TX packet passes the coordinator's dispatch fence before it
    // reaches the radio; a stale or revoked context sends nothing.
    m_txSinkConnection = connect(m_audio, &AudioEngine::txPacketReady, ps,
        [ps](const QByteArray& packet, const TxCoordinator::Context& context) {
            const TxCoordinator::Dispatch dispatch = context.beginDispatch(TxCoordinator::monotonicMs());
            if (dispatch) {
                ps->sendToRadio(packet);
            }
        });
}

void MobileEngine::applyTxAudioCapabilities(bool connected, const RadioCapabilities& caps)
{
    m_caps = caps;
    const bool pcAudioRequired = caps.takesTxAudioOverSeam && caps.canTransmit && caps.hostModulates;
    const bool pcAudioEnabled = pcAudioRequired
        || AppSettings::instance().value(QStringLiteral("PcAudioEnabled"), QStringLiteral("True")).toString()
               == QLatin1String("True");
    RadioModel& rm = m_session.radioModel();
    if (!m_microphoneProducer.valid()) {
        m_microphoneProducer = rm.registerTxProducer(m_audio, true);
    }
    const TxCoordinator::Context microphone = connected
        ? rm.captureTxMedia(m_microphoneProducer) : TxCoordinator::Context{};
    AudioEngine* audio = m_audio;
    const QHostAddress address = rm.radioAddress();
    QMetaObject::invokeMethod(audio, [audio, connected, caps, pcAudioEnabled, address, microphone] {
        audio->setMicrophoneContext(microphone);
        audio->applyBackendAudioCapabilities(connected, caps, pcAudioEnabled, address);
    });
    if (connected) {
        rm.notePcAudioEnabled(pcAudioEnabled);
    }
    emit transmitStateChanged();
}

bool MobileEngine::ensureMicrophonePermission()
{
    QMicrophonePermission permission;
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Granted:
        return true;
    case Qt::PermissionStatus::Denied:
        setLastError(tr("Microphone access is off. Allow it in Settings to transmit."));
        return false;
    case Qt::PermissionStatus::Undetermined:
        if (!m_micPermissionRequested) {
            m_micPermissionRequested = true;
            qApp->requestPermission(permission, this, [this](const QPermission& result) {
                if (result.status() != Qt::PermissionStatus::Granted) {
                    setLastError(tr("Microphone access is off. Allow it in Settings to transmit."));
                }
            });
        }
        return false;
    }
    return false;
}

void MobileEngine::startDeviceMicrophoneCapture()
{
    // The microphone is open only while PTT is held.
    if (!ensureMicrophonePermission()) {
        return;
    }
    // Make sure the play-and-record session is active before the input opens
    // (it can be lost to another app or an interruption).
    configureAudioSession();
    const int gain = AppSettings::instance().value(QStringLiteral("PcMicGain"), 100).toInt();
    const QHostAddress address = m_session.radioModel().radioAddress();
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio, gain, address] {
        audio->setPcMicGain(gain);
        if (!audio->isTxStreaming() && audio->hasAnyTxStream()) {
            audio->startTxStream(address, kRadioTxAudioPort);
        }
        // For the Log page: why PTT might carry no audio.
        qCInfo(lcMobile).noquote()
            << QStringLiteral("mic capture: streaming=%1 remoteTx=0x%2 daxTx=0x%3 gain=%4")
                   .arg(audio->isTxStreaming() ? 1 : 0)
                   .arg(QString::number(audio->remoteTxStreamId(), 16))
                   .arg(QString::number(audio->txStreamId(), 16))
                   .arg(gain);
    }, Qt::QueuedConnection);
}

void MobileEngine::stopDeviceMicrophoneCapture()
{
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio] {
        if (audio->isTxStreaming()) {
            audio->stopTxStream();
        }
    }, Qt::QueuedConnection);
}

bool MobileEngine::transmitAvailable() const
{
    return m_connected && m_caps.canTransmit
        && m_session.radioModel().transmitModel().micSelection() == kDeviceMicSelection;
}

bool MobileEngine::canUseDeviceMicrophone() const
{
    const TransmitModel& tm = m_session.radioModel().transmitModel();
    return m_connected && m_caps.canTransmit && tm.micSelection() != kDeviceMicSelection
        && tm.micInputList().contains(kDeviceMicSelection);
}

QString MobileEngine::transmitUnavailableReason() const
{
    if (!m_connected) {
        return tr("Not connected.");
    }
    if (!m_caps.canTransmit) {
        return tr("This radio is receive-only.");
    }
    // No slice transmits (e.g. the TX slice was closed): the radio's
    // interlock refuses PTT and TUNE.
    const QList<SliceModel*> slices = m_session.radioModel().slices();
    if (!slices.isEmpty()
        && std::none_of(slices.cbegin(), slices.cend(), [](SliceModel* s) { return s->isTxSlice(); })) {
        return tr("No slice transmits: press and hold a slice and choose Make TX slice.");
    }
    const QString mic = m_session.radioModel().transmitModel().micSelection();
    if (mic != kDeviceMicSelection) {
        return tr("The radio takes transmit audio from its %1 input. Switch it to this device's microphone to transmit from here.")
            .arg(mic);
    }
    return {};
}

bool MobileEngine::transmitting() const
{
    return m_session.radioModel().transmitModel().isTransmitting();
}

int MobileEngine::rfPower() const
{
    return m_session.radioModel().transmitModel().rfPower();
}

void MobileEngine::setRfPower(int power)
{
    if (m_connected && m_caps.canTransmit) {
        m_session.radioModel().transmitModel().setRfPower(std::clamp(power, 0, 100));
    }
}

bool MobileEngine::radioTransmitting() const
{
    const RadioModel& rm = m_session.radioModel();
    return rm.isRadioTransmitting() || rm.transmitModel().isTransmitting() || rm.transmitModel().isTuning();
}

bool MobileEngine::hasPaTemp() const
{
    return m_session.radioModel().meterModel().hasPaTemp();
}

bool MobileEngine::hasSupplyVolts() const
{
    return m_session.radioModel().meterModel().hasSupplyVoltage();
}

QStringList MobileEngine::meterKinds() const
{
    return {QStringLiteral("S"), QStringLiteral("Power"), QStringLiteral("SWR"), QStringLiteral("ALC"),
            QStringLiteral("Mic"), QStringLiteral("Comp"), QStringLiteral("PA temp"), QStringLiteral("Volts")};
}

QString MobileEngine::meterChoice(const char* key, const char* fallback) const
{
    const QString kind = AppSettings::instance().value(QLatin1String(key), QLatin1String(fallback)).toString();
    return meterKinds().contains(kind) ? kind : QString::fromLatin1(fallback);
}

void MobileEngine::setMeter(const QString& side, bool tx, const QString& kind)
{
    if (!tx) {
        side == QLatin1String("left") ? setMeterLeft(kind) : setMeterRight(kind);
        return;
    }
    const QString key = side == QLatin1String("left") ? QStringLiteral("MobileMeterLeftTx")
                                                      : QStringLiteral("MobileMeterRightTx");
    if (meterKinds().contains(kind) && AppSettings::instance().value(key).toString() != kind) {
        AppSettings::instance().setValue(key, kind);
        AppSettings::instance().save();
        emit meterSelectionChanged();
    }
}

QString MobileEngine::meterLeft() const
{
    return AppSettings::instance().value(QStringLiteral("MobileMeterLeft"), QStringLiteral("S")).toString();
}

void MobileEngine::setMeterLeft(const QString& kind)
{
    if (meterKinds().contains(kind) && kind != meterLeft()) {
        AppSettings::instance().setValue(QStringLiteral("MobileMeterLeft"), kind);
        AppSettings::instance().save();
        emit meterSelectionChanged();
    }
}

QString MobileEngine::meterRight() const
{
    return AppSettings::instance().value(QStringLiteral("MobileMeterRight"), QStringLiteral("Power")).toString();
}

void MobileEngine::setMeterRight(const QString& kind)
{
    if (meterKinds().contains(kind) && kind != meterRight()) {
        AppSettings::instance().setValue(QStringLiteral("MobileMeterRight"), kind);
        AppSettings::instance().save();
        emit meterSelectionChanged();
    }
}

bool MobileEngine::tuning() const
{
    return m_session.radioModel().transmitModel().isTuning();
}

bool MobileEngine::tuneAvailable() const
{
    return m_connected && m_caps.canTransmit && m_session.radioModel().transmitModel().tuneAvailable();
}

int MobileEngine::tunePower() const
{
    return m_session.radioModel().transmitModel().tunePower();
}

void MobileEngine::setTunePower(int power)
{
    if (m_connected && m_caps.canTransmit) {
        m_session.radioModel().transmitModel().setTunePower(std::clamp(power, 0, 100));
    }
}

bool MobileEngine::hasTuner() const
{
    return m_connected && m_session.radioModel().transmitModel().hasTuner();
}

bool MobileEngine::hasTunerMemories() const
{
    return m_connected && m_session.radioModel().transmitModel().hasTunerMemories();
}

QString MobileEngine::atuStatusText() const
{
    using S = ATUStatus;
    switch (m_session.radioModel().transmitModel().atuStatus()) {
    case S::InProgress: return tr("Tuning…");
    case S::Successful:
    case S::OK: return tr("Tuned");
    case S::Bypass:
    case S::ManualBypass: return tr("Bypass");
    case S::FailBypass:
    case S::Fail: return tr("Tune failed");
    case S::Aborted: return tr("Aborted");
    case S::None:
    case S::NotStarted: return {};
    }
    return {};
}

bool MobileEngine::atuTuned() const
{
    const auto status = m_session.radioModel().transmitModel().atuStatus();
    return status == ATUStatus::Successful || status == ATUStatus::OK;
}

bool MobileEngine::atuMemories() const
{
    return m_session.radioModel().transmitModel().memoriesEnabled();
}

void MobileEngine::setAtuMemories(bool on)
{
    if (hasTunerMemories()) {
        m_session.radioModel().transmitModel().setAtuMemories(on);
    }
}

bool MobileEngine::procEnabled() const
{
    return m_session.radioModel().transmitModel().speechProcessorEnable();
}

void MobileEngine::setProcEnabled(bool on)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setSpeechProcessorEnable(on);
    }
}

int MobileEngine::procLevel() const
{
    return m_session.radioModel().transmitModel().speechProcessorLevel();
}

void MobileEngine::setProcLevel(int level)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setSpeechProcessorLevel(std::clamp(level, 0, procLevelMax()));
    }
}

int MobileEngine::procLevelMax() const
{
    return m_session.radioModel().transmitModel().speechProcessorLevelMaximum();
}

bool MobileEngine::monitor() const
{
    return m_session.radioModel().transmitModel().sbMonitor();
}

void MobileEngine::setMonitor(bool on)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setSbMonitor(on);
    }
}

int MobileEngine::monitorGain() const
{
    return m_session.radioModel().transmitModel().monGainSb();
}

void MobileEngine::setMonitorGain(int gain)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setMonGainSb(std::clamp(gain, 0, 100));
    }
}

int MobileEngine::radioMicLevel() const
{
    return m_session.radioModel().transmitModel().micLevel();
}

void MobileEngine::setRadioMicLevel(int level)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setMicLevel(std::clamp(level, 0, 100));
    }
}

void MobileEngine::toggleTune()
{
    RadioModel& rm = m_session.radioModel();
    if (rm.transmitModel().isTuning()) {
        stopTransmit(QStringLiteral("tune button"));
        return;
    }
    if (!tuneAvailable()) {
        qCInfo(lcMobile) << "tune press ignored: tune not available";
        return;
    }
    m_tuneInput = rm.localTxController()->capture(TxController::Activity::Tune);
    if (!m_tuneInput.start()) {
        m_tuneInput.stop();
        qCWarning(lcMobile) << "tune start refused by the TX controller";
        setLastError(tr("The radio did not accept the tune request."));
        return;
    }
    qCInfo(lcMobile) << "tune keyed";
}

void MobileEngine::atuPressed()
{
    if (!hasTuner() || !m_caps.canTransmit) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    m_atuInput = rm.localTxController()->capture(TxController::Activity::Atu);
    // Already tuned on this transmit frequency: a press bypasses the ATU.
    const bool sameFreq = m_atuTunedFreqMhz > 0.0
        && std::abs(rm.transmitModel().transmitFreq() - m_atuTunedFreqMhz) < 1e-6;
    if (atuTuned() && sameFreq) {
        if (!m_atuInput.bypassAtu()) {
            setLastError(tr("The radio did not accept the ATU bypass."));
        }
        qCInfo(lcMobile) << "ATU bypass requested";
        return;
    }
    if (!m_atuInput.start()) {
        m_atuInput.stop();
        setLastError(tr("The radio did not accept the ATU tune request."));
        return;
    }
    qCInfo(lcMobile) << "ATU tune cycle started";
}

void MobileEngine::stopTransmit(const QString& reason)
{
    releasePtt(reason);
    setCwKey(false, reason);
    if (m_cwxInput.valid()) {
        m_cwxInput.stop();
        m_cwxInput = {};
    }
    if (m_session.radioModel().transmitModel().isTuning() && m_tuneInput.valid()) {
        m_tuneInput.stop();
        qCInfo(lcMobile).noquote() << "tune stopped:" << reason;
    }
    if (m_atuInput.valid()
        && m_session.radioModel().transmitModel().atuStatus() == ATUStatus::InProgress) {
        m_atuInput.stop();
        qCInfo(lcMobile).noquote() << "ATU cycle stopped:" << reason;
    }
}

int MobileEngine::micGain() const
{
    return AppSettings::instance().value(QStringLiteral("PcMicGain"), 100).toInt();
}

void MobileEngine::setMicGain(int gain)
{
    gain = std::clamp(gain, 0, 100);
    if (gain == micGain()) {
        return;
    }
    AppSettings::instance().setValue(QStringLiteral("PcMicGain"), gain);
    AppSettings::instance().save();
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio, gain] { audio->setPcMicGain(gain); }, Qt::QueuedConnection);
    emit micGainChanged();
}

void MobileEngine::defaultMicToDevice()
{
    // On a phone or tablet the only microphone is the device itself, so each
    // connection starts with the radio's mic input on PC. Once per
    // connection: a later change made elsewhere is respected.
    if (!m_micDefaultEnabled || m_micDefaulted || !canUseDeviceMicrophone()) {
        return;
    }
    m_micDefaulted = true;
    qCInfo(lcMobile) << "mic input" << m_session.radioModel().transmitModel().micSelection()
                     << "-> PC (this device)";
    useDeviceMicrophone();
}

void MobileEngine::useDeviceMicrophone()
{
    if (canUseDeviceMicrophone()) {
        m_session.radioModel().transmitModel().setMicSelection(kDeviceMicSelection);
    }
}

void MobileEngine::setPtt(bool down, const QString& reason)
{
    if (!down) {
        releasePtt(reason.isEmpty() ? QStringLiteral("operator release") : reason);
        return;
    }
    if (m_pttHeld) {
        return;
    }
    if (!transmitAvailable()) {
        qCInfo(lcMobile) << "PTT press ignored:" << transmitUnavailableReason();
        return;
    }
    m_pttInput = m_session.radioModel().localTxController()->capture(TxController::Activity::Mox);
    m_pttHeld = true;
    if (!m_pttInput.start()) {
        m_pttHeld = false;
        m_pttInput.stop();
        qCWarning(lcMobile) << "PTT start refused by the TX controller";
        setLastError(tr("The radio did not accept the transmit request."));
        return;
    }
    qCInfo(lcMobile) << "PTT keyed";
    startDeviceMicrophoneCapture();
}

void MobileEngine::releasePtt(const QString& reason)
{
    if (!m_pttHeld) {
        return;
    }
    m_pttHeld = false;
    m_pttInput.stop();
    stopDeviceMicrophoneCapture();
    qCInfo(lcMobile).noquote() << "PTT released:" << reason;
}

// ---- tuning, bands, filters ------------------------------------------------

void MobileEngine::tuneTo(double mhz)
{
    if (!m_slice || mhz <= 0.0) {
        return;
    }
    if (!m_pan || m_pan->bandwidthMhz() <= 0.0) {
        m_slice->tuneAndRecenter(mhz);
        return;
    }
    m_slice->setFrequency(mhz);
    // Near or past an edge the view follows, keeping the slice
    // kPanFollowEdge of a span inside it, so dragging keeps going up or down
    // the band instead of stopping at the edge of the display.
    const double bandwidth = m_pan->bandwidthMhz();
    const double offset = mhz - panCenterMhz();
    const double limit = bandwidth * (0.5 - kPanFollowEdge);
    if (std::abs(offset) > limit) {
        panTo(offset > 0.0 ? mhz - limit : mhz + limit);
    }
}

void MobileEngine::panTo(double centerMhz)
{
    if (!m_pan || centerMhz <= 0.0) {
        return;
    }
    m_pendingPanCenterMhz = centerMhz;
    if (!m_panMoveTimer.isActive()) {
        m_panMoveTimer.start();
    }
    m_panPreviewMhz = centerMhz;
    m_panPreviewUntilMs = QDateTime::currentMSecsSinceEpoch() + kPanPreviewHoldMs;
    m_panPreviewEndTimer.start();
    emit panGeometryChanged();
}

QStringList MobileEngine::bands() const
{
    return {QStringLiteral("160m"), QStringLiteral("80m"), QStringLiteral("60m"), QStringLiteral("40m"),
            QStringLiteral("30m"), QStringLiteral("20m"), QStringLiteral("17m"), QStringLiteral("15m"),
            QStringLiteral("12m"), QStringLiteral("10m"), QStringLiteral("6m")};
}

bool MobileEngine::bandsAvailable() const
{
    return m_connected && m_session.radioModel().usesFlexCommandPlane();
}

void MobileEngine::selectBand(const QString& band)
{
    RadioModel& rm = m_session.radioModel();
    if (!bandsAvailable() || !m_pan) {
        return;
    }
    const auto result = XvtrPolicy::resolveBandStackKey(band, {}, rm.capabilities());
    if (!result.isSupported()) {
        setLastError(result.unsupportedReason);
        return;
    }
    rm.requestPanBand(m_pan->panId(), result.key);
}

void MobileEngine::tuneBySteps(int steps)
{
    if (!m_slice || steps == 0) {
        return;
    }
    const qint64 step = stepHz();
    const qint64 hz = std::llround(m_slice->frequency() * 1e6);
    // Land on the grid first, then move: 14.074 37 +1 step(100) -> 14.074 40.
    qint64 base = (hz / step) * step;
    if (steps < 0 && base != hz) {
        base += step;
    }
    tuneTo(static_cast<double>(base + steps * step) / 1e6);
}

void MobileEngine::tuneByHz(int hz)
{
    if (hz == 0) {
        return;
    }
    const qint64 step = std::abs(static_cast<qint64>(hz));
    const qint64 freq = std::llround(frequencyMhz() * 1e6);
    // Floor/ceil to the grid; on the grid, take a full step.
    qint64 target = hz > 0 ? (freq / step + 1) * step
                           : ((freq + step - 1) / step - 1) * step;
    tuneTo(static_cast<double>(target) / 1e6);
}

void MobileEngine::tuneToSnapped(double mhz)
{
    const qint64 step = stepHz();
    tuneTo(static_cast<double>(std::llround(mhz * 1e6 / step) * step) / 1e6);
}

void MobileEngine::zoom(double factor)
{
    if (factor <= 0.0) {
        return;
    }
    if (!m_pan || m_pan->bandwidthMhz() <= 0.0) {
        setLastError(tr("Zoom is not available: no panadapter is bound yet."));
        qCWarning(lcMobile) << "zoom refused: no bound pan";
        return;
    }
    double bandwidth = m_pan->bandwidthMhz() * factor;
    if (m_pan->bandwidthLimitsKnown() && m_pan->minBandwidthMhz() < m_pan->maxBandwidthMhz()) {
        bandwidth = std::clamp(bandwidth, m_pan->minBandwidthMhz(), m_pan->maxBandwidthMhz());
    }
    // Centre on the slice so the zoom closes in on what is being heard.
    const double center = m_slice ? m_slice->frequency() : m_pan->centerMhz();
    const bool sent = m_session.radioModel().requestPanCenter(m_pan->panId(), center, bandwidth);
    qCInfo(lcMobile) << "zoom pan" << m_pan->panId() << "bandwidth" << m_pan->bandwidthMhz()
                     << "->" << bandwidth << "sent" << sent;
    if (!sent && qFuzzyCompare(bandwidth, m_pan->bandwidthMhz())) {
        setLastError(tr("Already at the radio's span limit."));
    }
}

namespace {
constexpr int kRitXitLimitHz = 9999;
}

bool MobileEngine::ritOn() const { return m_slice && m_slice->ritOn(); }
int MobileEngine::ritHz() const { return m_slice ? m_slice->ritFreq() : 0; }
bool MobileEngine::xitOn() const { return m_slice && m_slice->xitOn(); }
int MobileEngine::xitHz() const { return m_slice ? m_slice->xitFreq() : 0; }

void MobileEngine::setRitOn(bool on)
{
    if (m_slice) {
        m_slice->setRit(on, m_slice->ritFreq());
    }
}

void MobileEngine::setRitHz(int hz)
{
    if (m_slice) {
        m_slice->setRit(m_slice->ritOn(), std::clamp(hz, -kRitXitLimitHz, kRitXitLimitHz));
    }
}

void MobileEngine::setXitOn(bool on)
{
    if (m_slice) {
        m_slice->setXit(on, m_slice->xitFreq());
    }
}

void MobileEngine::setXitHz(int hz)
{
    if (m_slice) {
        m_slice->setXit(m_slice->xitOn(), std::clamp(hz, -kRitXitLimitHz, kRitXitLimitHz));
    }
}

bool MobileEngine::squelchAvailable() const
{
    static const QStringList kNoSquelch{QStringLiteral("DIGU"), QStringLiteral("DIGL"), QStringLiteral("NT"),
                                        QStringLiteral("RTTY"), QStringLiteral("CW"), QStringLiteral("CWL")};
    return m_slice && !kNoSquelch.contains(m_slice->mode());
}

bool MobileEngine::squelchOn() const { return m_slice && m_slice->receiveSquelchOn(); }
int MobileEngine::squelchLevel() const { return m_slice ? m_slice->receiveSquelchLevel() : 0; }

void MobileEngine::setSquelchOn(bool on)
{
    if (m_slice && squelchAvailable()) {
        m_slice->setManualSquelch(on, m_slice->receiveSquelchLevel());
    }
}

void MobileEngine::setSquelchLevel(int level)
{
    if (m_slice && squelchAvailable()) {
        m_slice->setManualSquelch(m_slice->receiveSquelchOn(), std::clamp(level, 0, 100));
    }
}

QString MobileEngine::rxAntenna() const { return m_slice ? m_slice->rxAntenna() : QString(); }
QString MobileEngine::txAntenna() const { return m_slice ? m_slice->txAntenna() : QString(); }

QStringList MobileEngine::rxAntennas() const
{
    if (!m_slice) {
        return {};
    }
    return m_slice->rxAntennaList().isEmpty() ? m_session.radioModel().antennaList() : m_slice->rxAntennaList();
}

QStringList MobileEngine::txAntennas() const
{
    if (!m_slice) {
        return {};
    }
    // The radio's TX list; never offer a made-up port.
    QStringList list = m_slice->txAntennaList();
    if (!m_slice->txAntenna().isEmpty() && !list.contains(m_slice->txAntenna())) {
        list.prepend(m_slice->txAntenna());
    }
    return list;
}

void MobileEngine::setRxAntenna(const QString& antenna)
{
    if (m_slice && rxAntennas().contains(antenna) && antenna != m_slice->rxAntenna()) {
        m_slice->setRxAntenna(antenna);
    }
}

void MobileEngine::setTxAntenna(const QString& antenna)
{
    if (m_slice && txAntennas().contains(antenna) && antenna != m_slice->txAntenna()) {
        m_slice->setTxAntenna(antenna);
    }
}

QVariantList MobileEngine::slices() const
{
    QVariantList list;
    for (SliceModel* s : m_session.radioModel().slices()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), s->sliceId()},
            {QStringLiteral("letter"), s->letter()},
            {QStringLiteral("frequency"), s->frequency()},
            {QStringLiteral("mode"), s->mode()},
            {QStringLiteral("active"), m_slice == s},
            {QStringLiteral("tx"), s->isTxSlice()},
            {QStringLiteral("panId"), s->panId()},
            {QStringLiteral("filterLow"), s->filterLow()},
            {QStringLiteral("filterHigh"), s->filterHigh()},
        });
    }
    return list;
}

int MobileEngine::activeSliceId() const
{
    return m_slice ? m_slice->sliceId() : -1;
}

bool MobileEngine::canAddSlice() const
{
    const RadioModel& rm = m_session.radioModel();
    return m_connected && rm.slices().size() < rm.maxSlices();
}

bool MobileEngine::canAddPanadapter() const
{
    const RadioModel& rm = m_session.radioModel();
    return m_connected && rm.usesFlexCommandPlane() && rm.panadapters().size() < rm.maxPanadapters();
}

void MobileEngine::selectSlice(int sliceId)
{
    RadioModel& rm = m_session.radioModel();
    SliceModel* s = rm.slice(sliceId);
    if (!s) {
        return;
    }
    rm.setActivePanId(s->panId());
    if (!s->isActive()) {
        s->setActive(true);
    }
    bindSlice(s);
    bindPan(rm.panadapter(s->panId()));
}

void MobileEngine::addSlice()
{
    if (!canAddSlice()) {
        return;
    }
    // With no slice there is no active pan yet: use the one the radio gave
    // this client (its restored layout), as the retry after a refused slice.
    RadioModel& rm = m_session.radioModel();
    PanadapterModel* pan = m_pan;
    if (!pan && !rm.panadapters().isEmpty()) {
        pan = rm.panadapters().first();
    }
    if (!pan) {
        return;
    }
    setLastError(QString());
    rm.addSliceOnPan(pan->panId());
}

bool MobileEngine::splitActive() const
{
    return m_splitPending || (m_splitTxId >= 0 && m_session.radioModel().slice(m_splitTxId));
}

bool MobileEngine::splitAvailable() const
{
    return splitActive() || (m_slice && canAddSlice() && m_caps.canTransmit);
}

void MobileEngine::toggleSplit()
{
    RadioModel& rm = m_session.radioModel();
    if (splitActive()) {
        SliceModel* rx = rm.slice(m_splitRxId);
        SliceModel* tx = rm.slice(m_splitTxId);
        stopTransmit(QStringLiteral("split off"));
        if (rx) {
            rx->setTxSlice(true);
        }
        m_splitPending = false;
        m_splitRxId = m_splitTxId = -1;
        if (tx && rm.slices().size() > 1) {
            rm.removeSlice(tx->sliceId());
        }
        if (rx) {
            selectSlice(rx->sliceId());
        }
        qCInfo(lcMobile) << "split off";
        emit slicesChanged();
        return;
    }
    if (!m_slice || !m_pan || !canAddSlice()) {
        setLastError(tr("No free slice for the transmit side of a split."));
        return;
    }
    const QString mode = m_slice->mode();
    const bool cw = mode == QLatin1String("CW") || mode == QLatin1String("CWL") || mode == QLatin1String("CWU");
    const double txMhz = m_slice->frequency() + (cw ? 0.001 : 0.005);
    m_splitRxId = m_slice->sliceId();
    m_splitTxId = -1;
    m_splitPending = true;
    setLastError(QString());
    rm.addSliceOnPan(m_pan->panId(), txMhz);
    emit slicesChanged();
}

void MobileEngine::removeSlice(int sliceId)
{
    RadioModel& rm = m_session.radioModel();
    if (rm.slices().size() <= 1) {
        setLastError(tr("The last slice cannot be closed."));
        return;
    }
    if (SliceModel* s = rm.slice(sliceId); s && s->isTxSlice()) {
        stopTransmit(QStringLiteral("TX slice closed"));
        // Hand transmit to the slice that stays (the active one if it is not
        // the one going): otherwise no slice transmits and PTT/TUNE stop.
        SliceModel* heir = (m_slice && m_slice != s) ? m_slice : nullptr;
        for (SliceModel* o : rm.slices()) {
            if (!heir && o != s) {
                heir = o;
            }
        }
        if (heir) {
            qCInfo(lcMobile) << "TX slice" << s->letter() << "closed: transmit moves to" << heir->letter();
            heir->setTxSlice(true);
        }
    }
    rm.removeSlice(sliceId);
}

void MobileEngine::makeTxSlice(int sliceId)
{
    if (SliceModel* s = m_session.radioModel().slice(sliceId); s && !s->isTxSlice()) {
        stopTransmit(QStringLiteral("TX slice changed"));
        s->setTxSlice(true);
    }
}

int MobileEngine::panCount() const
{
    return m_connected ? static_cast<int>(m_session.radioModel().panadapters().size()) : 0;
}

void MobileEngine::closeOtherPanadapters()
{
    RadioModel& rm = m_session.radioModel();
    if (!m_pan) {
        return;
    }
    const QString keep = m_pan->panId();
    QStringList closing;
    for (PanadapterModel* pan : rm.panadapters()) {
        if (pan && pan->panId() != keep) {
            closing << pan->panId();
        }
    }
    for (SliceModel* s : rm.slices()) {
        if (s->isTxSlice() && closing.contains(s->panId())) {
            stopTransmit(QStringLiteral("TX slice's panadapter closed"));
        }
    }
    qCInfo(lcMobile) << "closing other panadapters" << closing << "keeping" << keep;
    for (const QString& id : std::as_const(closing)) {
        rm.removePanadapter(id);
    }
}

void MobileEngine::addPanadapter()
{
    if (canAddPanadapter()) {
        m_session.radioModel().createPanadapter();
    }
}

// ---- CW ----------------------------------------------------------------------

namespace {
constexpr int kCwDecodedMaxChars = 2000;
const QString kCwDecoderKey = QStringLiteral("MobileCwDecoder");
}

void MobileEngine::wireCw()
{
    RadioModel& rm = m_session.radioModel();
    // Local sidetone follows the key the radio actually sends, and the
    // radio's sidetone, pitch and CW monitor gain.
    connect(&rm, &RadioModel::cwKeyDownChanged, this, [this](bool down) {
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, down] { audio->setCwKeyDown(down); }, Qt::QueuedConnection);
    });
    auto syncSidetone = [this] {
        const TransmitModel& tm = m_session.radioModel().transmitModel();
        const bool on = tm.cwSidetone();
        const float pitch = static_cast<float>(tm.cwPitch());
        const float volume = static_cast<float>(tm.monGainCw()) / 100.0f;
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, on, pitch, volume] {
            if (CwSidetoneGenerator* sidetone = audio->cwSidetone()) {
                sidetone->setEnabled(on);
                sidetone->setPitchHz(pitch);
                sidetone->setVolume(volume);
            }
        }, Qt::QueuedConnection);
        emit cwChanged();
    };
    connect(&rm.transmitModel(), &TransmitModel::phoneStateChanged, this, syncSidetone);
    connect(&rm.transmitModel(), &TransmitModel::stateChanged, this, &MobileEngine::cwChanged);

    m_cwDecoder = std::make_unique<CwRxModel>();
    m_cwAudio = std::make_unique<DecoderAudioModel>(rm, DecoderAudioModel::Consumer::Cw);
    connect(m_cwAudio.get(), &DecoderAudioModel::nativePcmReady, m_cwDecoder.get(), &CwRxModel::feed);
    connect(m_cwAudio.get(), &DecoderAudioModel::pcmReady, m_cwDecoder.get(), &CwRxModel::feedFixed24);
    connect(m_cwAudio.get(), &DecoderAudioModel::sourceReset, m_cwDecoder.get(), &CwRxModel::reset);
    connect(m_cwDecoder.get(), &CwRxModel::textDecoded, this, [this](const QString& text, float) {
        m_cwDecodedText += text;
        if (m_cwDecodedText.size() > kCwDecodedMaxChars) {
            m_cwDecodedText = m_cwDecodedText.right(kCwDecodedMaxChars);
        }
        emit cwDecoderChanged();
    });
    connect(m_cwDecoder.get(), &CwRxModel::statsUpdated, this, [this](float pitch, float speed) {
        m_cwDecoderStats = QStringLiteral("%1 WPM  %2 Hz").arg(qRound(speed)).arg(qRound(pitch));
        emit cwDecoderChanged();
    });
    connect(this, &MobileEngine::modeChanged, this, &MobileEngine::updateCwDecoder);
}

void MobileEngine::updateCwDecoder()
{
    if (!m_cwDecoder || !m_cwAudio) {
        return;
    }
    const bool run = m_connected && cwDecoderEnabled() && cwMode() && m_slice;
    m_cwAudio->setSlice(run ? m_slice.data() : nullptr);
    m_cwAudio->setEnabled(run);
    if (run && !m_cwDecoder->isRunning()) {
        m_cwDecoder->start();
    } else if (!run && m_cwDecoder->isRunning()) {
        m_cwDecoder->stop();
    }
}

bool MobileEngine::cwMode() const
{
    return m_slice && (m_slice->mode() == QLatin1String("CW") || m_slice->mode() == QLatin1String("CWL"));
}

bool MobileEngine::cwKeyAvailable() const
{
    return m_connected && m_caps.canTransmit;
}

bool MobileEngine::cwxAvailable() const
{
    return cwKeyAvailable() && m_session.radioModel().hasRadioSideCwKeyer();
}

int MobileEngine::cwSpeed() const { return m_session.radioModel().transmitModel().cwSpeed(); }
int MobileEngine::cwPitch() const { return m_session.radioModel().transmitModel().cwPitch(); }
bool MobileEngine::cwBreakIn() const { return m_session.radioModel().transmitModel().cwBreakIn(); }

void MobileEngine::setCwSpeed(int wpm)
{
    if (!m_connected) {
        return;
    }
    wpm = std::clamp(wpm, 5, 100);
    m_session.radioModel().transmitModel().setCwSpeed(wpm);
    m_session.radioModel().cwxModel().setSpeed(wpm);
}

void MobileEngine::setCwPitch(int hz)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setCwPitch(std::clamp(hz, 100, 6000));
    }
}

void MobileEngine::setCwBreakIn(bool on)
{
    if (m_connected) {
        m_session.radioModel().transmitModel().setCwBreakIn(on);
    }
}

namespace {
constexpr int kCwMemoryCount = 9;
QString cwMemoryKey(int index)
{
    return QStringLiteral("MobileCwMemory%1").arg(index + 1);
}
const char* const kCwMemoryDefaults[kCwMemoryCount] = {
    "CQ CQ CQ DE {CALL} {CALL} K", "{CALL}", "5NN TU", "TU 73", "QRZ?", "AGN?",
    "QRL?", "R R TU 73", "",
};
} // namespace

QStringList MobileEngine::cwMemories() const
{
    QStringList list;
    for (int i = 0; i < kCwMemoryCount; ++i) {
        list << AppSettings::instance().value(cwMemoryKey(i), QString::fromLatin1(kCwMemoryDefaults[i])).toString();
    }
    return list;
}

void MobileEngine::setCwMemory(int index, const QString& text)
{
    if (index < 0 || index >= kCwMemoryCount) {
        return;
    }
    AppSettings::instance().setValue(cwMemoryKey(index), text.trimmed().toUpper());
    AppSettings::instance().save();
    emit cwMemoriesChanged();
}

void MobileEngine::sendCwMemory(int index)
{
    const QStringList list = cwMemories();
    if (index < 0 || index >= list.size() || list.at(index).isEmpty()) {
        return;
    }
    QString text = list.at(index);
    const QString call = m_session.radioModel().callsign().trimmed().toUpper();
    if (text.contains(QLatin1String("{CALL}")) && call.isEmpty()) {
        setLastError(tr("The radio has no callsign for {CALL}. Set it in Controls → CW → Callsign."));
        return;
    }
    text.replace(QLatin1String("{CALL}"), call);
    sendCwx(text);
}

int MobileEngine::txFilterLow() const
{
    return m_session.radioModel().transmitModel().txFilterLow();
}

int MobileEngine::txFilterHigh() const
{
    return m_session.radioModel().transmitModel().txFilterHigh();
}

void MobileEngine::setTxFilterLow(int hz)
{
    if (m_connected && hz != txFilterLow()) {
        m_session.radioModel().transmitModel().setTxFilterLow(hz);
    }
}

void MobileEngine::setTxFilterHigh(int hz)
{
    if (m_connected && hz != txFilterHigh()) {
        m_session.radioModel().transmitModel().setTxFilterHigh(hz);
    }
}

QStringList MobileEngine::eqBandLabels() const
{
    QStringList labels;
    for (int b = 0; b < EqualizerModel::BandCount; ++b) {
        labels << EqualizerModel::bandLabel(static_cast<EqualizerModel::Band>(b));
    }
    return labels;
}

bool MobileEngine::eqTxEnabled() const
{
    return m_session.radioModel().equalizerModel().txEnabled();
}

bool MobileEngine::eqRxEnabled() const
{
    return m_session.radioModel().equalizerModel().rxEnabled();
}

QVariantList MobileEngine::eqTxBands() const
{
    QVariantList list;
    const EqualizerModel& eq = m_session.radioModel().equalizerModel();
    for (int b = 0; b < EqualizerModel::BandCount; ++b) {
        list << eq.txBand(static_cast<EqualizerModel::Band>(b));
    }
    return list;
}

QVariantList MobileEngine::eqRxBands() const
{
    QVariantList list;
    const EqualizerModel& eq = m_session.radioModel().equalizerModel();
    for (int b = 0; b < EqualizerModel::BandCount; ++b) {
        list << eq.rxBand(static_cast<EqualizerModel::Band>(b));
    }
    return list;
}

void MobileEngine::setEqEnabled(bool tx, bool on)
{
    if (!m_connected) {
        return;
    }
    EqualizerModel& eq = m_session.radioModel().equalizerModel();
    tx ? eq.setTxEnabled(on) : eq.setRxEnabled(on);
}

void MobileEngine::setEqBand(bool tx, int band, int dB)
{
    if (!m_connected || band < 0 || band >= EqualizerModel::BandCount) {
        return;
    }
    const auto b = static_cast<EqualizerModel::Band>(band);
    dB = std::clamp(dB, -10, 10);
    EqualizerModel& eq = m_session.radioModel().equalizerModel();
    if ((tx ? eq.txBand(b) : eq.rxBand(b)) != dB) {
        tx ? eq.setTxBand(b, dB) : eq.setRxBand(b, dB);
    }
}

void MobileEngine::resetEq(bool tx)
{
    for (int b = 0; b < EqualizerModel::BandCount; ++b) {
        setEqBand(tx, b, 0);
    }
}

QStringList MobileEngine::globalProfiles() const { return m_session.radioModel().globalProfiles(); }
QString MobileEngine::activeGlobalProfile() const { return m_session.radioModel().activeGlobalProfile(); }
QStringList MobileEngine::txProfiles() const { return m_session.radioModel().transmitModel().profileList(); }
QString MobileEngine::activeTxProfile() const { return m_session.radioModel().transmitModel().activeProfile(); }
QStringList MobileEngine::micProfiles() const { return m_session.radioModel().transmitModel().micProfileList(); }
QString MobileEngine::activeMicProfile() const { return m_session.radioModel().transmitModel().activeMicProfile(); }

namespace {
QString quotedProfileName(const QString& name)
{
    QString n = name.trimmed();
    n.remove(QLatin1Char('"'));
    return QLatin1Char('"') + n + QLatin1Char('"');
}
} // namespace

void MobileEngine::loadProfile(const QString& type, const QString& name)
{
    if (!m_connected || name.trimmed().isEmpty()) {
        return;
    }
    // A profile changes transmit settings (or the whole radio layout); never
    // load one under an active transmission.
    stopTransmit(QStringLiteral("profile load"));
    RadioModel& rm = m_session.radioModel();
    qCInfo(lcMobile) << "load" << type << "profile" << name;
    if (type == QLatin1String("global")) {
        rm.loadGlobalProfile(name);
    } else if (type == QLatin1String("transmit")) {
        rm.transmitModel().loadProfile(name);
    } else if (type == QLatin1String("mic")) {
        rm.transmitModel().loadMicProfile(name);
    }
}

void MobileEngine::saveProfile(const QString& type, const QString& name)
{
    if (!m_connected || name.trimmed().isEmpty()) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    qCInfo(lcMobile) << "save" << type << "profile" << name;
    if (type == QLatin1String("global")) {
        rm.sendCommand(QStringLiteral("profile global save ") + quotedProfileName(name));
    } else if (type == QLatin1String("transmit") || type == QLatin1String("mic")) {
        const QStringList& existing = type == QLatin1String("mic") ? micProfiles() : txProfiles();
        if (existing.contains(name.trimmed())) {
            setLastError(tr("A %1 profile named “%2” exists. The radio cannot overwrite it: "
                            "delete it first, then save again.").arg(type, name.trimmed()));
            return;
        }
        rm.sendCommand(QStringLiteral("profile %1 create ").arg(type) + quotedProfileName(name));
    }
}

void MobileEngine::deleteProfile(const QString& type, const QString& name)
{
    if (!m_connected || name.trimmed().isEmpty()
        || (type != QLatin1String("global") && type != QLatin1String("transmit") && type != QLatin1String("mic"))) {
        return;
    }
    qCInfo(lcMobile) << "delete" << type << "profile" << name;
    m_session.radioModel().sendCommand(QStringLiteral("profile %1 delete ").arg(type) + quotedProfileName(name));
}

int MobileEngine::agcThreshold() const
{
    return m_slice ? m_slice->agcThreshold() : 0;
}

void MobileEngine::setAgcThreshold(int value)
{
    value = std::clamp(value, 0, 100);
    if (m_slice && m_slice->agcThreshold() != value) {
        m_slice->setAgcThreshold(value);
    }
}

int MobileEngine::rfGain() const
{
    return m_pan ? m_pan->rfGain() : 0;
}

int MobileEngine::rfGainLow() const
{
    return m_pan && m_pan->hasRfGainRange() ? m_pan->rfGainLow() : -8;
}

int MobileEngine::rfGainHigh() const
{
    return m_pan && m_pan->hasRfGainRange() ? m_pan->rfGainHigh() : 32;
}

int MobileEngine::rfGainStep() const
{
    return m_pan && m_pan->hasRfGainRange() && m_pan->rfGainStep() > 0 ? m_pan->rfGainStep() : 8;
}

void MobileEngine::setRfGain(int gain)
{
    if (!m_pan || !m_connected) {
        return;
    }
    gain = std::clamp(gain, rfGainLow(), rfGainHigh());
    if (gain != m_pan->rfGain()) {
        m_session.radioModel().setPanRfGainFor(m_pan->panId(), gain);
    }
}

QVariantList MobileEngine::memories() const
{
    QList<MemoryEntry> list = m_session.radioModel().memories().values();
    std::sort(list.begin(), list.end(), [](const MemoryEntry& a, const MemoryEntry& b) {
        return a.freq < b.freq;
    });
    QVariantList out;
    for (const MemoryEntry& m : std::as_const(list)) {
        out << QVariantMap{{QStringLiteral("index"), m.index}, {QStringLiteral("name"), m.name},
                           {QStringLiteral("freq"), m.freq}, {QStringLiteral("mode"), m.mode},
                           {QStringLiteral("group"), m.group}};
    }
    return out;
}

void MobileEngine::recallMemory(int index)
{
    if (!m_connected) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    qCInfo(lcMobile) << "recall memory" << index;
    if (rm.usesLocalMemoryBank()) {
        rm.recallCachedMemory(index);
    } else {
        rm.sendCommand(QStringLiteral("memory apply %1").arg(index));
    }
}

void MobileEngine::saveMemory(const QString& name)
{
    if (!m_connected || !m_slice) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    if (!rm.memoriesWritable()) {
        setLastError(tr("This radio's memories cannot be written."));
        return;
    }
    createMemoryFromSlice(&rm, m_slice, name.trimmed(), this, [this](int code, const QString& body, int index) {
        if (code != 0) {
            setLastError(tr("The radio did not save the memory: %1").arg(body));
        } else {
            qCInfo(lcMobile) << "memory saved as" << index;
        }
        emit memoriesChanged();
    });
}

void MobileEngine::removeMemory(int index)
{
    if (m_connected) {
        qCInfo(lcMobile) << "remove memory" << index;
        m_session.radioModel().sendCommand(QStringLiteral("memory remove %1").arg(index));
    }
}

bool MobileEngine::locked() const
{
    return m_slice && m_slice->isLocked();
}

void MobileEngine::setLocked(bool on)
{
    if (m_slice && m_slice->isLocked() != on) {
        m_slice->setLocked(on);
    }
}

QStringList MobileEngine::pinnedSections() const
{
    return AppSettings::instance().value(QStringLiteral("MobilePinnedSections"), QStringLiteral("DSP"))
        .toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
}

void MobileEngine::setPinnedSections(const QStringList& sections)
{
    if (sections == pinnedSections()) {
        return;
    }
    AppSettings::instance().setValue(QStringLiteral("MobilePinnedSections"), sections.join(QLatin1Char(',')));
    AppSettings::instance().save();
    emit displayLevelsChanged();
}

int MobileEngine::displayInt(const char* key, int fallback) const
{
    bool ok = false;
    const int v = AppSettings::instance().value(QLatin1String(key), QString::number(fallback)).toString().toInt(&ok);
    return ok ? v : fallback;
}

void MobileEngine::setDisplayInt(const char* key, int value)
{
    if (displayInt(key, value + 1) == value) {
        return;
    }
    AppSettings::instance().setValue(QLatin1String(key), QString::number(value));
    AppSettings::instance().save();
    emit displayLevelsChanged();
}

bool MobileEngine::cwDecoderEnabled() const
{
    return AppSettings::instance().value(kCwDecoderKey, QStringLiteral("False")).toString() == QLatin1String("True");
}

void MobileEngine::setCwDecoderEnabled(bool on)
{
    AppSettings::instance().setValue(kCwDecoderKey, on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    updateCwDecoder();
    emit cwDecoderChanged();
}

void MobileEngine::clearCwDecoded()
{
    m_cwDecodedText.clear();
    emit cwDecoderChanged();
}

void MobileEngine::setCwKey(bool down, const QString& reason)
{
    if (!down) {
        if (m_cwKeyHeld) {
            m_cwKeyHeld = false;
            m_cwKeyInput.stop();
            qCInfo(lcMobile).noquote() << "CW key up:" << (reason.isEmpty() ? QStringLiteral("released") : reason);
            emit cwChanged();
        }
        return;
    }
    if (m_cwKeyHeld || !cwKeyAvailable()) {
        return;
    }
    m_cwKeyInput = m_session.radioModel().localTxController()->capture(TxController::Activity::CwKey);
    m_cwKeyHeld = true;
    if (!m_cwKeyInput.start()) {
        m_cwKeyHeld = false;
        m_cwKeyInput.stop();
        setLastError(tr("The radio did not accept the CW key."));
        return;
    }
    emit cwChanged();
}

void MobileEngine::sendCwx(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || !cwxAvailable()) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    if (!rm.cwxModel().canSend()) {
        setLastError(tr("CW text cannot be sent right now."));
        return;
    }
    m_cwxInput = rm.localTxController()->capture(TxController::Activity::Cwx);
    if (!m_cwxInput.send(trimmed)) {
        setLastError(tr("The radio did not accept the CW text."));
        return;
    }
    qCInfo(lcMobile) << "CWX sent" << trimmed.size() << "characters";
    // What we send shows in the decoder's text too, marked, so the exchange
    // reads both ways (the receiver hears nothing of it while transmitting).
    m_cwDecodedText += QStringLiteral("\n[TX] %1\n").arg(trimmed.toUpper());
    if (m_cwDecodedText.size() > kCwDecodedMaxChars) {
        m_cwDecodedText = m_cwDecodedText.right(kCwDecodedMaxChars);
    }
    emit cwDecoderChanged();
}

void MobileEngine::stopCwx()
{
    if (m_cwxInput.valid()) {
        m_cwxInput.stop();
        m_cwxInput = {};
    }
    m_session.radioModel().cwxModel().clearBuffer();
}

int MobileEngine::stepHz() const
{
    return m_slice && m_slice->stepHz() > 0 ? m_slice->stepHz() : 100;
}

bool MobileEngine::nr() const
{
    return m_slice && m_slice->nrOn();
}

void MobileEngine::setNr(bool on)
{
    if (m_slice) {
        m_slice->setNr(on);
    }
}

bool MobileEngine::nb() const
{
    return m_slice && m_slice->nbOn();
}

void MobileEngine::setNb(bool on)
{
    if (m_slice) {
        m_slice->setNb(on);
    }
}

bool MobileEngine::anf() const
{
    return m_slice && m_slice->anfOn();
}

void MobileEngine::setAnf(bool on)
{
    if (m_slice) {
        m_slice->setAnf(on);
    }
}

QString MobileEngine::agcMode() const
{
    return m_slice ? m_slice->agcMode() : QString();
}

void MobileEngine::setAgcMode(const QString& mode)
{
    if (m_slice && mode != m_slice->agcMode()) {
        m_slice->setAgcMode(mode);
    }
}

QStringList MobileEngine::agcModes() const
{
    // SmartSDR slice agc_mode values.
    return {QStringLiteral("off"), QStringLiteral("slow"), QStringLiteral("med"), QStringLiteral("fast")};
}

int MobileEngine::filterLow() const
{
    return m_slice ? m_slice->filterLow() : 0;
}

int MobileEngine::filterHigh() const
{
    return m_slice ? m_slice->filterHigh() : 0;
}

namespace {
bool voiceMode(const QString& m)
{
    return m == QLatin1String("USB") || m == QLatin1String("LSB") || m == QLatin1String("AM")
        || m == QLatin1String("SAM") || m == QLatin1String("DSB") || ModeFilters::isFmMode(m)
        || m == QLatin1String("DFM");
}
} // namespace

bool MobileEngine::beatCancelMode() const
{
    // Voice only, never CW or data: BC would notch the signal itself. The
    // receive audio mixes every audible slice, so every one that is not
    // muted must be in a voice mode, not just the active one.
    if (!m_slice || !voiceMode(m_slice->mode())) {
        return false;
    }
    for (SliceModel* s : m_session.radioModel().slices()) {
        if (!s->audioMute() && !voiceMode(s->mode())) {
            return false;
        }
    }
    return true;
}

bool MobileEngine::beatCancelSetting() const
{
    return AppSettings::instance().value(QStringLiteral("MobileBeatCancel"), QStringLiteral("0")).toString()
        == QLatin1String("1");
}

void MobileEngine::applyBeatCancel()
{
    if (m_audio) {
        m_audio->setBeatCancelEnabled(beatCancelSetting() && beatCancelMode());
    }
}

QStringList MobileEngine::dspKeys() const
{
    if (!m_slice) {
        return {};
    }
    // Visibility rules of VfoWidget's DSP grid.
    const QString m = m_slice->mode();
    const bool fm = ModeFilters::isFmMode(m) || m == QLatin1String("DFM");
    const bool cw = isCwMode(m);
    const RadioModel& rm = m_session.radioModel();
    QStringList keys{QStringLiteral("NR"), QStringLiteral("NB"), QStringLiteral("ANF")};
    if (cw) {
        keys << QStringLiteral("APF");
    }
    if (rm.hasLmsNoiseFilters()) {
        keys << QStringLiteral("NRL") << QStringLiteral("ANFL") << QStringLiteral("ANFT");
    }
    if (rm.hasExtendedDspFilters() && !fm) {
        keys << QStringLiteral("NRS");
        if (!cw) {
            keys << QStringLiteral("RNN");
        }
        keys << QStringLiteral("NRF");
    }
    // Client-side: beat cancel (this app's audio, not the radio).
    if (beatCancelMode()) {
        keys << QStringLiteral("BC");
    }
    return keys;
}

bool MobileEngine::dspOn(const QString& key) const
{
    if (!m_slice) {
        return false;
    }
    if (key == QLatin1String("NR")) return m_slice->nrOn();
    if (key == QLatin1String("NB")) return m_slice->nbOn();
    if (key == QLatin1String("ANF")) return m_slice->anfOn();
    if (key == QLatin1String("APF")) return m_slice->apfOn();
    if (key == QLatin1String("NRL")) return m_slice->nrlOn();
    if (key == QLatin1String("ANFL")) return m_slice->anflOn();
    if (key == QLatin1String("ANFT")) return m_slice->anftOn();
    if (key == QLatin1String("NRS")) return m_slice->nrsOn();
    if (key == QLatin1String("RNN")) return m_slice->rnnOn();
    if (key == QLatin1String("NRF")) return m_slice->nrfOn();
    if (key == QLatin1String("BC")) return beatCancelSetting();
    return false;
}

void MobileEngine::setDsp(const QString& key, bool on)
{
    if (!m_slice) {
        return;
    }
    if (key == QLatin1String("NR")) m_slice->setNr(on);
    else if (key == QLatin1String("NB")) m_slice->setNb(on);
    else if (key == QLatin1String("ANF")) m_slice->setAnf(on);
    else if (key == QLatin1String("APF")) m_slice->setApf(on);
    else if (key == QLatin1String("NRL")) m_slice->setNrl(on);
    else if (key == QLatin1String("ANFL")) m_slice->setAnfl(on);
    else if (key == QLatin1String("ANFT")) m_slice->setAnft(on);
    else if (key == QLatin1String("NRS")) m_slice->setNrs(on);
    else if (key == QLatin1String("RNN")) m_slice->setRnn(on);
    else if (key == QLatin1String("NRF")) m_slice->setNrf(on);
    else if (key == QLatin1String("BC")) {
        AppSettings::instance().setValue(QStringLiteral("MobileBeatCancel"), on ? QStringLiteral("1") : QStringLiteral("0"));
        AppSettings::instance().save();
        applyBeatCancel();
        emit dspChanged();
    }
}

QList<int> MobileEngine::filterPresets() const
{
    // The desktop's per-mode ladders (VFO filter grid): 8 widths a mode.
    const QVector<int>& widths = ModeFilters::widthsForMode(mode());
    return QList<int>(widths.cbegin(), widths.cend());
}

int MobileEngine::activeFilterPreset() const
{
    // The passband's labelled width, then the nearest ladder entry: the
    // radio's own defaults differ slightly from the presets.
    if (!m_slice) {
        return 0;
    }
    const int width = ModeFilters::widthForEdges(mode(), filterLow(), filterHigh());
    int best = 0;
    int bestDiff = 0;
    for (const int preset : filterPresets()) {
        const int diff = std::abs(width - preset);
        if (diff <= std::max(60, preset / 20) && (best == 0 || diff < bestDiff)) {
            best = preset;
            bestDiff = diff;
        }
    }
    return best;
}

int MobileEngine::filterWidth() const
{
    return m_slice ? ModeFilters::widthForEdges(mode(), filterLow(), filterHigh()) : 0;
}

void MobileEngine::setFilterWidth(int widthHz)
{
    if (!m_slice || widthHz <= 0) {
        return;
    }
    // Edge placement per mode, as the desktop VFO places a ladder width.
    ModeFilters::SliceContext ctx;
    ctx.diguOffset = m_slice->diguOffset();
    ctx.diglOffset = m_slice->diglOffset();
    ctx.rttyShift = m_slice->rttyShift();
    const ModeFilters::Edges e = ModeFilters::edgesForWidth(m_slice->mode(), widthHz, ctx);
    m_slice->setFilterWidth(e.lo, e.hi);
}

void MobileEngine::onConnectionStateChanged(bool connected)
{
    m_connected = connected;
    m_micDefaulted = false;
    m_netSampleMs = 0;
    m_sliceRecoveryTried = false;
    m_sliceRecoveryPending = false;
    emit networkChanged();
    if (connected) {
        defaultMicToDevice();
        // Display diagnostics for the Log page (e.g. SmartLink without a
        // waterfall): what arrived in the first seconds, and on which streams.
        m_fftFramesSeen = 0;
        m_wfRowsSeen = 0;
        m_otherWfStreamSeen = 0;
        QTimer::singleShot(6000, this, [this] {
            if (!m_connected) {
                return;
            }
            qCInfo(lcMobile).noquote()
                << QStringLiteral("display check: %1 pans=%10 pan=%2 fft=0x%3 wf=0x%4 frames=%5 wfRows=%6 otherWf=0x%7 px=%8x%9")
                       .arg(m_session.radioModel().isWan() ? QStringLiteral("WAN") : QStringLiteral("LAN"))
                       .arg(m_pan ? m_pan->panId() : QStringLiteral("-"))
                       .arg(QString::number(m_pan ? m_pan->panStreamId() : 0, 16))
                       .arg(QString::number(m_pan ? m_pan->wfStreamId() : 0, 16))
                       .arg(m_fftFramesSeen).arg(m_wfRowsSeen)
                       .arg(QString::number(m_otherWfStreamSeen, 16))
                       .arg(m_panWidth).arg(m_panHeight)
                       .arg(m_session.radioModel().panadapters().size());
        });
        wireRxAudio();
        wireTxPacketSink();
        startAudio();
        setKeepScreenAwake(true);
        setConnectionState(QStringLiteral("connected"));
        selectActiveSlice();
        emit transmitStateChanged();
        emit rfPowerChanged();
        emit tuneStateChanged();
        emit txFilterChanged();
        emit eqChanged();
        emit profilesChanged();
        emit atuChanged();
        emit txAudioChanged();
        emit cwChanged();
        updateCwDecoder();
        return;
    }
    stopTransmit(QStringLiteral("radio disconnected"));
    updateCwDecoder();
    if (m_wanActive && !m_wanUserDisconnect) {
        qCInfo(lcMobile) << "remote session dropped; reconnecting";
        m_wanReconnectTimer.start();
    } else {
        m_wanActive = false;
    }
    stopDeviceMicrophoneCapture();
    setKeepScreenAwake(false);
    stopAudio();
    bindSlice(nullptr);
    bindPan(nullptr);
    setConnectionState(QStringLiteral("idle"));
}

void MobileEngine::startAudio()
{
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio] { audio->startRxStream(); }, Qt::QueuedConnection);
}

void MobileEngine::stopAudio()
{
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio] { audio->stopRxStream(); }, Qt::QueuedConnection);
}

void MobileEngine::wireRxAudio()
{
    for (const QMetaObject::Connection& c : std::as_const(m_rxAudioConnections)) {
        disconnect(c);
    }
    m_rxAudioConnections.clear();

    RadioModel& rm = m_session.radioModel();
    IRadioBackend* backend = rm.backend();
    if (!backend) {
        return;
    }
    // Exactly one RX source per backend: seam audio for backends that own it
    // (including the demo, whose PanadapterStream carries a different scene),
    // otherwise the Flex stream's decoded PCM. Both would sum at the sink.
    if (backend->ownsRxAudio()) {
        m_rxAudioConnections.append(connect(&rm, &RadioModel::backendAudioFrameReady,
                                            m_audio, &AudioEngine::feedPcmFrame));
    } else if (PanadapterStream* ps = rm.panStream()) {
        m_rxAudioConnections.append(connect(ps, &PanadapterStream::pcmFrameReady,
                                            m_audio, &AudioEngine::feedPcmFrame));
    }
}

void MobileEngine::onSliceAdded(SliceModel* slice)
{
    // Any audible slice's mode or mute decides whether BC may run.
    connect(slice, &SliceModel::modeChanged, this, &MobileEngine::applyBeatCancel);
    connect(slice, &SliceModel::audioMuteChanged, this, &MobileEngine::applyBeatCancel);
    connect(slice, &SliceModel::modeChanged, this, &MobileEngine::dspChanged);
    connect(slice, &SliceModel::audioMuteChanged, this, &MobileEngine::dspChanged);
    // The split's TX slice: same mode as the RX slice, muted (the RX slice
    // is what you listen to), transmits, and gets focus to tune the offset.
    if (m_splitPending && slice->sliceId() != m_splitRxId) {
        m_splitPending = false;
        m_splitTxId = slice->sliceId();
        if (SliceModel* rx = m_session.radioModel().slice(m_splitRxId); rx && slice->mode() != rx->mode()) {
            slice->setMode(rx->mode());
        }
        slice->setAudioMute(true);
        slice->setTxSlice(true);
        qCInfo(lcMobile) << "split on: RX slice" << m_splitRxId << "TX slice" << m_splitTxId
                         << "at" << slice->frequency();
        QMetaObject::invokeMethod(this, [this, id = m_splitTxId] { selectSlice(id); }, Qt::QueuedConnection);
    }
    connect(slice, &SliceModel::activeChanged, this, [this](bool) { selectActiveSlice(); });
    for (auto signal : {&SliceModel::frequencyChanged}) {
        connect(slice, signal, this, &MobileEngine::slicesChanged);
    }
    connect(slice, &SliceModel::modeChanged, this, &MobileEngine::slicesChanged);
    connect(slice, &SliceModel::txSliceChanged, this, &MobileEngine::slicesChanged);
    connect(slice, &SliceModel::letterChanged, this, &MobileEngine::slicesChanged);
    emit slicesChanged();
    selectActiveSlice();
}

void MobileEngine::selectActiveSlice()
{
    const QList<SliceModel*> slices = m_session.radioModel().slices();
    SliceModel* chosen = nullptr;
    for (SliceModel* s : slices) {
        if (s->isActive()) {
            chosen = s;
            break;
        }
    }
    // With no active slice, stay on the current one rather than jumping.
    if (!chosen && m_slice && slices.contains(m_slice.data())) {
        chosen = m_slice;
    }
    if (!chosen && !slices.isEmpty()) {
        chosen = slices.first();
    }
    if (chosen != m_slice) {
        bindSlice(chosen);
    }
    PanadapterModel* pan = chosen ? m_session.radioModel().panadapter(chosen->panId()) : nullptr;
    qCDebug(lcMobile) << "selectActiveSlice slices" << slices.size() << "chosen"
                      << (chosen ? chosen->sliceId() : -1) << "pan" << (chosen ? chosen->panId() : QString())
                      << "found" << (pan != nullptr) << "pans" << m_session.radioModel().panadapters().size();
    if (pan != m_pan) {
        bindPan(pan);
    }
}

void MobileEngine::bindSlice(SliceModel* slice)
{
    for (const QMetaObject::Connection& c : std::as_const(m_sliceConnections)) {
        disconnect(c);
    }
    m_sliceConnections.clear();
    m_slice = slice;
    applyBeatCancel();
    if (slice) {
        m_sliceConnections.append(connect(slice, &SliceModel::frequencyChanged, this, &MobileEngine::frequencyChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::modeChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::lockedChanged, this, &MobileEngine::lockedChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::tuneBlockedByLock, this, &MobileEngine::tuneBlocked));
        // The preset set changes with the mode.
        m_sliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::filterChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::modeListChanged, this, &MobileEngine::modesChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::audioGainChanged, this, &MobileEngine::afGainChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::audioMuteChanged, this, &MobileEngine::mutedChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::filterChanged, this, &MobileEngine::filterChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::nrChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::nbChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::anfChanged, this, &MobileEngine::dspChanged));
        for (auto sig : {&SliceModel::nrlChanged, &SliceModel::nrsChanged, &SliceModel::rnnChanged,
                         &SliceModel::nrfChanged, &SliceModel::anflChanged, &SliceModel::anftChanged,
                         &SliceModel::apfChanged}) {
            m_sliceConnections.append(connect(slice, sig, this, &MobileEngine::dspChanged));
        }
        // The DSP set changes with the mode (APF in CW, none of the 8000s in FM).
        m_sliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::dspChanged));
        // Beat cancel only on voice: it would notch CW and data tones.
        m_sliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::applyBeatCancel));
        m_sliceConnections.append(connect(slice, &SliceModel::agcModeChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::agcThresholdChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::stepChanged, this, &MobileEngine::stepChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::ritChanged, this, &MobileEngine::ritXitChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::xitChanged, this, &MobileEngine::ritXitChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::squelchChanged, this, &MobileEngine::squelchChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::rxAntennaChanged, this, &MobileEngine::antennaChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::txAntennaChanged, this, &MobileEngine::antennaChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::rxAntennaListChanged, this, &MobileEngine::antennaChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::txAntennaListChanged, this, &MobileEngine::antennaChanged));
    }
    emit frequencyChanged();
    emit lockedChanged();
    emit modeChanged();
    emit modesChanged();
    emit afGainChanged();
    emit mutedChanged();
    emit filterChanged();
    emit dspChanged();
    emit stepChanged();
    emit ritXitChanged();
    emit squelchChanged();
    emit antennaChanged();
    emit slicesChanged();
}

void MobileEngine::bindPan(PanadapterModel* pan)
{
    for (const QMetaObject::Connection& c : std::as_const(m_panConnections)) {
        disconnect(c);
    }
    m_panConnections.clear();
    m_pan = pan;
    if (pan) {
        m_panConnections.append(connect(pan, &PanadapterModel::infoChanged, this, &MobileEngine::panGeometryChanged));
        m_panConnections.append(connect(pan, &PanadapterModel::rfGainChanged, this, &MobileEngine::rfGainChanged));
        m_panConnections.append(connect(pan, &PanadapterModel::rfGainInfoChanged, this, &MobileEngine::rfGainChanged));
        sendPanDimensions(pan->panId());
    }
    emit panGeometryChanged();
    emit rfGainChanged();
}

void MobileEngine::setPanPixels(int width, int height)
{
    if (width <= 0 || height <= 0 || (width == m_panWidth && height == m_panHeight)) {
        return;
    }
    m_panWidth = width;
    m_panHeight = height;
    // Each new size restarts the radio's FFT stream at the new geometry, so
    // only a size that has held for a moment is sent; layout settling and
    // rotation produce bursts of intermediate sizes.
    m_panPixelsTimer.start();
}

void MobileEngine::sendPanDimensions(const QString& panId)
{
    RadioModel& rm = m_session.radioModel();
    if (panId.isEmpty() || m_panWidth <= 0 || m_panHeight <= 0 || !rm.panadapter(panId)) {
        return;
    }
    // Self-shaping backends take the width through the seam; the
    // xpixels/ypixels text is the Flex command-plane contract.
    // ypixels sets only the FFT's vertical resolution (the view scales the
    // dBm values itself). Below about 100 rows, 4.2.x firmware's pixels no
    // longer decode to the right dBm (noise read as -50 dBm at 88 rows), so
    // never ask for fewer than kMinPanYPixels.
    const int yPixels = std::max(m_panHeight, kMinPanYPixels);
    if (!rm.requestLocalPanPixelWidth(panId, m_panWidth) && rm.hasCommandPlane()) {
        rm.sendCommand(QStringLiteral("display pan set %1 xpixels=%2 ypixels=%3")
                           .arg(panId).arg(m_panWidth).arg(yPixels));
    }
}

double MobileEngine::frequencyMhz() const
{
    return m_slice ? m_slice->frequency() : 0.0;
}

void MobileEngine::setFrequencyMhz(double mhz)
{
    if (m_slice && mhz > 0.0) {
        m_slice->setFrequency(mhz);
    }
}

QString MobileEngine::mode() const
{
    return m_slice ? m_slice->mode() : QString();
}

void MobileEngine::setMode(const QString& mode)
{
    if (m_slice && mode != m_slice->mode()) {
        m_slice->setMode(mode);
    }
}

QStringList MobileEngine::modes() const
{
    if (!m_slice) {
        return {};
    }
    const QStringList preferred{QStringLiteral("LSB"), QStringLiteral("USB"), QStringLiteral("CW"),
                                QStringLiteral("AM"), QStringLiteral("FM"), QStringLiteral("DIGU")};
    const QStringList available = m_slice->modeList();
    if (available.isEmpty()) {
        return preferred;
    }
    QStringList result;
    for (const QString& m : preferred) {
        if (available.contains(m)) {
            result.append(m);
        }
    }
    return result;
}

double MobileEngine::afGain() const
{
    return m_slice ? m_slice->audioGain() : 0.0;
}

void MobileEngine::setAfGain(double gain)
{
    if (m_slice) {
        m_slice->setAudioGain(static_cast<float>(std::clamp(gain, 0.0, 100.0)));
    }
}

bool MobileEngine::muted() const
{
    return m_slice && m_slice->audioMute();
}

void MobileEngine::setMuted(bool muted)
{
    if (m_slice) {
        m_slice->setAudioMute(muted);
    }
}

QString MobileEngine::sMeterText() const
{
    if (m_sMeterDbm <= kS9Dbm) {
        const int s = std::max(0, static_cast<int>(std::lround(9.0 + (m_sMeterDbm - kS9Dbm) / kDbPerSUnit)));
        return QStringLiteral("S%1  %2 dBm").arg(s).arg(m_sMeterDbm, 0, 'f', 0);
    }
    return QStringLiteral("S9+%1  %2 dBm").arg(std::lround(m_sMeterDbm - kS9Dbm)).arg(m_sMeterDbm, 0, 'f', 0);
}

double MobileEngine::panCenterMhz() const
{
    if (!m_pan) {
        return 0.0;
    }
    // While a drag moves the view, show where it is going, not the radio's
    // last echo, so the waterfall and markers follow the finger at once
    // (as SpectrumWidget's pan-drag preview does).
    if (m_panPreviewUntilMs > QDateTime::currentMSecsSinceEpoch()) {
        return m_panPreviewMhz;
    }
    return m_pan->centerMhz();
}

double MobileEngine::panBandwidthMhz() const
{
    return m_pan ? m_pan->bandwidthMhz() : 0.0;
}

void MobileEngine::setConnectionState(const QString& state)
{
    if (state != m_connectionState) {
        m_connectionState = state;
        qCInfo(lcMobile) << "connection state" << state;
        emit connectionStateChanged();
    }
}

void MobileEngine::setLastError(const QString& error)
{
    // Every notice is announced, even a repeat of the last one: the display
    // shows each as a popup (lastErrorChanged), so a second refusal must
    // show again.
    m_lastError = error;
    emit lastErrorChanged();
}

QString MobileEngine::appVersion() const
{
    return QStringLiteral("%1 (%2)").arg(QCoreApplication::applicationVersion(),
                                         QStringLiteral(AETHER_MOBILE_BUILD_NUMBER));
}

QString MobileEngine::manualUrl() const
{
    return QStringLiteral("https://github.com/OH6OKSA/AetherSDR/blob/HEAD/docs/mobile/MANUAL.md");
}

QString MobileEngine::networkQuality() const
{
    return m_connected ? m_session.radioModel().networkQuality() : QStringLiteral("Off");
}

int MobileEngine::networkRttMs() const
{
    return m_connected ? m_session.radioModel().lastPingRtt() : 0;
}

QVariantMap MobileEngine::networkDetails()
{
    const RadioModel& rm = m_session.radioModel();
    QVariantMap d;
    d[QStringLiteral("quality")] = networkQuality();
    d[QStringLiteral("rttMs")] = networkRttMs();
    d[QStringLiteral("path")] = remoteSession() ? QStringLiteral("SmartLink (internet)") : QStringLiteral("Local network");
    d[QStringLiteral("radio")] = rm.targetRadioIp();
    d[QStringLiteral("lossPercent")] = rm.packetLossPercent();
    d[QStringLiteral("lossWindowS")] = rm.packetLossWindowSeconds();
    d[QStringLiteral("lossDrops")] = rm.packetLossWindowDrops();
    d[QStringLiteral("lossPackets")] = rm.packetLossWindowPackets();
    d[QStringLiteral("audioGapMs")] = rm.audioPacketGapMs();
    d[QStringLiteral("audioGapMaxMs")] = rm.audioPacketGapMaxMs();
    d[QStringLiteral("jitterMs")] = rm.audioPacketJitterMs();
    d[QStringLiteral("sessionDrops")] = rm.packetDropCount();
    d[QStringLiteral("sessionPackets")] = rm.packetTotalCount();
    QVariantList streams;
    if (rm.hasStreamCategoryStats()) {
        const std::pair<const char*, PanadapterStream::StreamCategory> cats[] = {
            {"Audio", PanadapterStream::CatAudio}, {"FFT", PanadapterStream::CatFFT},
            {"Waterfall", PanadapterStream::CatWaterfall}, {"Meters", PanadapterStream::CatMeter},
        };
        for (const auto& [name, cat] : cats) {
            const PanadapterStream::CategoryStats st = rm.categoryStats(cat);
            streams << QVariantMap{{QStringLiteral("name"), QString::fromLatin1(name)},
                                   {QStringLiteral("packets"), st.packets},
                                   {QStringLiteral("errors"), st.errors}};
        }
    }
    d[QStringLiteral("streams")] = streams;
    // Rates since the previous call (the dialog polls once a second).
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 rx = rm.rxBytes();
    const qint64 tx = rm.txBytes();
    double rxKbps = 0.0;
    double txKbps = 0.0;
    if (m_netSampleMs > 0 && now > m_netSampleMs && rx >= m_netRxBytes && tx >= m_netTxBytes) {
        const double s = (now - m_netSampleMs) / 1000.0;
        rxKbps = (rx - m_netRxBytes) * 8.0 / 1000.0 / s;
        txKbps = (tx - m_netTxBytes) * 8.0 / 1000.0 / s;
    }
    m_netRxBytes = rx;
    m_netTxBytes = tx;
    m_netSampleMs = now;
    d[QStringLiteral("rxKbps")] = rxKbps;
    d[QStringLiteral("txKbps")] = txKbps;
    d[QStringLiteral("rxMB")] = rx / 1e6;
    d[QStringLiteral("txMB")] = tx / 1e6;
    return d;
}

QVariantMap MobileEngine::radioSetup() const
{
    const RadioModel& rm = m_session.radioModel();
    const TransmitModel& tx = rm.transmitModel();
    QVariantMap d;
    d[QStringLiteral("callsign")] = callsign();
    d[QStringLiteral("nickname")] = rm.nickname();
    d[QStringLiteral("model")] = rm.model();
    d[QStringLiteral("serial")] = m_redactIdentity ? QStringLiteral("(hidden)")
                                : rm.serial().isEmpty() ? rm.chassisSerial() : rm.serial();
    d[QStringLiteral("version")] = rm.version();
    d[QStringLiteral("region")] = rm.region();
    d[QStringLiteral("ip")] = rm.targetRadioIp();
    d[QStringLiteral("remoteOn")] = rm.remoteOnEnabled();
    d[QStringLiteral("muteLocalWhenRemote")] = rm.muteLocalWhenRemote();
    d[QStringLiteral("maxPower")] = tx.maxPowerLevel();
    d[QStringLiteral("txDelay")] = tx.txDelay();
    d[QStringLiteral("interlockTimeoutS")] = tx.interlockTimeout() / 1000;
    d[QStringLiteral("tuneMode")] = tx.tuneMode();
    d[QStringLiteral("cwDelay")] = tx.cwDelay();
    d[QStringLiteral("iambic")] = tx.cwIambic();
    d[QStringLiteral("iambicMode")] = tx.cwIambicMode();
    d[QStringLiteral("swapPaddles")] = tx.cwSwapPaddles();
    d[QStringLiteral("micBias")] = tx.micBias();
    d[QStringLiteral("micBoost")] = tx.micBoost();
    d[QStringLiteral("lineoutGain")] = rm.lineoutGain();
    d[QStringLiteral("lineoutMute")] = rm.lineoutMute();
    d[QStringLiteral("headphoneGain")] = rm.headphoneGain();
    d[QStringLiteral("headphoneMute")] = rm.headphoneMute();
    d[QStringLiteral("networkMtu")] = rm.networkMtuSetting();
    return d;
}

void MobileEngine::setRadioSetup(const QString& key, const QVariant& value)
{
    if (!m_connected) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    TransmitModel& tx = rm.transmitModel();
    const bool flex = rm.usesFlexCommandPlane();
    qCInfo(lcMobile) << "radio setup:" << key << "=" << value;
    if (key == QLatin1String("callsign")) {
        setCallsign(value.toString());
    } else if (key == QLatin1String("nickname") && flex) {
        const QString name = value.toString().trimmed();
        if (!name.isEmpty()) {
            rm.sendCommand(QStringLiteral("radio name ") + name);
        }
    } else if (key == QLatin1String("remoteOn")) {
        rm.setRemoteOnEnabled(value.toBool());
    } else if (key == QLatin1String("muteLocalWhenRemote") && flex) {
        rm.sendCommand(QStringLiteral("radio set mute_local_audio_when_remote=%1").arg(value.toBool() ? 1 : 0));
    } else if (key == QLatin1String("maxPower") && flex) {
        rm.sendCommand(QStringLiteral("transmit set max_power_level=%1").arg(std::clamp(value.toInt(), 0, 100)));
    } else if (key == QLatin1String("txDelay") && flex) {
        rm.sendCommand(QStringLiteral("interlock set tx_delay=%1").arg(std::max(0, value.toInt())));
    } else if (key == QLatin1String("interlockTimeoutS") && flex) {
        // The radio takes the timeout in milliseconds (FlexLib Radio.cs).
        rm.sendCommand(QStringLiteral("interlock set timeout=%1").arg(std::max(0, value.toInt()) * 1000));
    } else if (key == QLatin1String("tuneMode")) {
        tx.setTuneMode(value.toString());
    } else if (key == QLatin1String("cwDelay")) {
        tx.setCwDelay(std::clamp(value.toInt(), 0, 2000));
    } else if (key == QLatin1String("iambic")) {
        tx.setCwIambic(value.toBool());
    } else if (key == QLatin1String("iambicMode")) {
        tx.setCwIambicMode(value.toInt() ? 1 : 0);
    } else if (key == QLatin1String("swapPaddles")) {
        tx.setCwSwapPaddles(value.toBool());
    } else if (key == QLatin1String("micBias")) {
        tx.setMicBias(value.toBool());
    } else if (key == QLatin1String("micBoost")) {
        tx.setMicBoost(value.toBool());
    } else if (key == QLatin1String("lineoutGain")) {
        rm.setLineoutGain(std::clamp(value.toInt(), 0, 100));
    } else if (key == QLatin1String("lineoutMute")) {
        rm.setLineoutMute(value.toBool());
    } else if (key == QLatin1String("headphoneGain")) {
        rm.setHeadphoneGain(std::clamp(value.toInt(), 0, 100));
    } else if (key == QLatin1String("headphoneMute")) {
        rm.setHeadphoneMute(value.toBool());
    } else if (key == QLatin1String("networkMtu")) {
        // The app's own setting (VITA-49 packet size the radio sends us),
        // as the desktop's Network MTU: saved, then sent (capped over a
        // tailnet by the core).
        const int mtu = std::clamp(value.toInt(), kMinNetworkMtu, kMaxNetworkMtu);
        AppSettings::instance().setValue(QStringLiteral("NetworkMtu"), QString::number(mtu));
        AppSettings::instance().save();
        if (flex) {
            rm.sendCommand(QStringLiteral("client set enforce_network_mtu=1 network_mtu=%1")
                               .arg(rm.networkMtuParam(mtu)));
        }
    }
}

bool MobileEngine::setupSuggested() const
{
    const RadioModel& rm = m_session.radioModel();
    if (!m_connected || !rm.usesFlexCommandPlane() || !callsign().isEmpty()) {
        return false;
    }
    const QString key = QStringLiteral("MobileSetupOffered_%1").arg(rm.serial());
    return AppSettings::instance().value(key, QStringLiteral("0")).toString() != QLatin1String("1");
}

void MobileEngine::dismissSetupSuggestion()
{
    const QString key = QStringLiteral("MobileSetupOffered_%1").arg(m_session.radioModel().serial());
    AppSettings::instance().setValue(key, QStringLiteral("1"));
    AppSettings::instance().save();
}

namespace {
const QString kMobileAudioCompressionKey = QStringLiteral("MobileAudioCompression");

// The radio is reached through a VPN: no local network holds its address,
// and a tunnel interface (iOS utun/ipsec, wg, tun, ppp) carries IPv4. A
// routed home network without a tunnel is not a VPN.
bool radioThroughVpn(const QHostAddress& radio)
{
    if (radio.protocol() != QAbstractSocket::IPv4Protocol || radio.isLoopback()) {
        return false;
    }
    bool tunnel = false;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) {
            continue;
        }
        const QString name = iface.name();
        const bool tunnelName = name.startsWith(QLatin1String("utun")) || name.startsWith(QLatin1String("ipsec"))
            || name.startsWith(QLatin1String("wg")) || name.startsWith(QLatin1String("tun"))
            || name.startsWith(QLatin1String("ppp"));
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            if (tunnelName) {
                tunnel = true;
            } else if (entry.prefixLength() > 0 && radio.isInSubnet(entry.ip(), entry.prefixLength())) {
                return false;   // on a local network
            }
        }
    }
    return tunnel;
}
} // namespace

QString MobileEngine::audioBuffer() const
{
    const QString v = AppSettings::instance().value(QStringLiteral("MobileAudioBuffer"), QStringLiteral("Normal")).toString();
    return (v == QLatin1String("Low") || v == QLatin1String("Smooth")) ? v : QStringLiteral("Normal");
}

void MobileEngine::setAudioBuffer(const QString& mode)
{
    if (mode == audioBuffer()) {
        return;
    }
    AppSettings::instance().setValue(QStringLiteral("MobileAudioBuffer"), mode);
    AppSettings::instance().save();
    applyAudioBuffer();
    emit audioCompressionChanged();
}

void MobileEngine::applyAudioBuffer()
{
    const QString mode = audioBuffer();
    const int capMs = mode == QLatin1String("Low") ? 100 : mode == QLatin1String("Smooth") ? 400 : 200;
    const int prebufferMs = mode == QLatin1String("Low") ? 0 : mode == QLatin1String("Smooth") ? 150 : 60;
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio, capMs, prebufferMs] {
        audio->setRxBufferCapMs(capMs);
        audio->setReceivePresentationDelays(prebufferMs, 0);
    }, Qt::QueuedConnection);
    qCInfo(lcMobile).noquote() << QStringLiteral("receive audio buffer: %1 (%2 ms, prebuffer %3 ms)")
                                      .arg(mode).arg(capMs).arg(prebufferMs);
}

QString MobileEngine::audioCompression() const
{
    const QString v = AppSettings::instance().value(kMobileAudioCompressionKey, QStringLiteral("Auto")).toString();
    return (v == QLatin1String("Opus") || v == QLatin1String("None")) ? v : QStringLiteral("Auto");
}

void MobileEngine::setAudioCompression(const QString& mode)
{
    if (mode == audioCompression()) {
        return;
    }
    AppSettings::instance().setValue(kMobileAudioCompressionKey, mode);
    AppSettings::instance().save();
    emit audioCompressionChanged();
}

void MobileEngine::applyAudioCompression(const QHostAddress& radio, bool wan)
{
    const QString choice = audioCompression();
    QString core = choice;   // the core's AudioCompression: Auto, Opus or None
    QString why;
    if (choice == QLatin1String("Auto")) {
        if (wan) {
            why = QStringLiteral("SmartLink");
        } else if (radioThroughVpn(radio)) {
            core = QStringLiteral("Opus");
            why = QStringLiteral("VPN");
        }
    }
    AppSettings::instance().setValue(QStringLiteral("AudioCompression"), core);
    const bool opus = core == QLatin1String("Opus") || (core == QLatin1String("Auto") && wan);
    m_audioCodingInUse = opus ? (why.isEmpty() ? QStringLiteral("Opus") : QStringLiteral("Opus (%1)").arg(why))
                              : QStringLiteral("Uncompressed");
    qCInfo(lcMobile).noquote() << "receive audio:" << m_audioCodingInUse << "(setting" << choice + ")";
    emit audioCompressionChanged();
}

namespace {
const QString kBandPlanKey = QStringLiteral("MobileBandPlan");
}

QString MobileEngine::bandPlan() const
{
    // Unset: IARU Region 1 (the app's first users are in Europe); "Off"
    // hides the strip.
    const QString v = AppSettings::instance().value(kBandPlanKey, QStringLiteral("IARU Region 1")).toString();
    return v == QLatin1String("Off") ? QString() : v;
}

void MobileEngine::setBandPlan(const QString& name)
{
    const QString stored = name.isEmpty() ? QStringLiteral("Off") : name;
    AppSettings::instance().setValue(kBandPlanKey, stored);
    AppSettings::instance().save();
    if (!name.isEmpty()) {
        m_bandPlan.setActivePlan(name);
    }
    emit bandPlanChanged();
}

QStringList MobileEngine::bandPlans() const
{
    return m_bandPlan.availablePlans();
}

QVector<BandPlanManager::Segment> MobileEngine::bandPlanSegments() const
{
    if (bandPlan().isEmpty() || m_bandPlan.activePlanName() != bandPlan()) {
        return {};
    }
    return m_bandPlan.segments();
}

QString MobileEngine::callsign() const
{
    return m_session.radioModel().callsign().trimmed().toUpper();
}

void MobileEngine::setCallsign(const QString& callsign)
{
    const QString call = callsign.trimmed().toUpper();
    RadioModel& rm = m_session.radioModel();
    // Empty is "no change", never "erase" (as the desktop's station field).
    if (call.isEmpty() || call == rm.callsign() || !m_connected) {
        return;
    }
    if (rm.usesFlexCommandPlane()) {
        rm.sendCommand(QStringLiteral("radio callsign ") + call);
    }
    rm.setStationCallsign(call);
    qCInfo(lcMobile) << "callsign set to" << call;
    emit callsignChanged();
}

} // namespace AetherSDR::mobile
