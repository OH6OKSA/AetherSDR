#include "MobileEngine.h"

#include "ScreenAwake.h"
#include "SecureStore.h"

#include "core/AppActivity.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/RadioDiscovery.h"
#include "core/backends/IRadioBackend.h"
#include "core/backends/flex/PanadapterStream.h"
#include "core/discovery/RadioDiscoverySource.h"
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
constexpr double kDbPerSUnit = 6.0;

} // namespace

MobileEngine::MobileEngine(QObject* parent)
    : QObject(parent)
{
    Q_ASSERT(!s_instance);
    s_instance = this;
    RadioModel& rm = m_session.radioModel();
    m_session.setSessionId(1);

    // AudioEngine reads AppSettings in its constructor; main() loads them first.
    m_audioThread = new QThread(this);
    m_audioThread->setObjectName(QStringLiteral("AudioEngine"));
    m_audio = new AudioEngine;
    m_audio->moveToThread(m_audioThread);
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
    });
    connect(&rm, &RadioModel::sliceAdded, this, &MobileEngine::onSliceAdded);
    connect(&rm, &RadioModel::sliceRemoved, this, [this](int) {
        selectActiveSlice();
        emit slicesChanged();
    });
    connect(&rm, &RadioModel::sliceCreateFailed, this, [this](int limit, const QString&) {
        setLastError(tr("The radio allows %1 slices.").arg(limit));
    });
    connect(&rm, &RadioModel::panadapterLimitReached, this, [this](int limit, const QString&) {
        setLastError(tr("The radio allows %1 panadapters.").arg(limit));
    });
    connect(&rm, &RadioModel::panadapterAdded, this, [this](PanadapterModel* pan) {
        sendPanDimensions(pan->panId());
        selectActiveSlice();
    });
    connect(&rm, &RadioModel::panDimensionsNeeded, this, &MobileEngine::sendPanDimensions);
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
            emit spectrumFrame(bins);
        }
    });
    connect(&rm, &RadioModel::panFeedWaterfallRowReady, this,
            [this](quint32 streamId, const QVector<float>& bins, double lowMhz, double highMhz,
                   quint32, qint64, bool) {
        if (m_pan && m_pan->wfStreamId() == streamId) {
            emit waterfallRow(bins, lowMhz, highMhz);
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
        if (m_session.radioModel().transmitModel().micSelection() != kDeviceMicSelection) {
            releasePtt(QStringLiteral("mic input left this device"));
        }
        emit transmitStateChanged();
    });
    connect(&rm.transmitModel(), &TransmitModel::micInputListChanged, this, &MobileEngine::transmitStateChanged);
    connect(&rm.transmitModel(), &TransmitModel::transmittingChanged, this, &MobileEngine::transmittingChanged);
    connect(&rm.transmitModel(), &TransmitModel::rfPowerChanged, this, &MobileEngine::rfPowerChanged);
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
    const int gain = AppSettings::instance().value(QStringLiteral("PcMicGain"), 100).toInt();
    const QHostAddress address = m_session.radioModel().radioAddress();
    AudioEngine* audio = m_audio;
    QMetaObject::invokeMethod(audio, [audio, gain, address] {
        audio->setPcMicGain(gain);
        if (!audio->isTxStreaming() && audio->hasAnyTxStream()) {
            audio->startTxStream(address, kRadioTxAudioPort);
        }
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
    const double offset = mhz - m_pan->centerMhz();
    const double limit = bandwidth * (0.5 - kPanFollowEdge);
    if (std::abs(offset) > limit) {
        const double center = offset > 0.0 ? mhz - limit : mhz + limit;
        m_session.radioModel().requestPanCenter(m_pan->panId(), center);
    }
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
    if (!canAddSlice() || !m_pan) {
        return;
    }
    m_session.radioModel().addSliceOnPan(m_pan->panId());
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

void MobileEngine::addPanadapter()
{
    if (canAddPanadapter()) {
        m_session.radioModel().createPanadapter();
    }
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

QList<int> MobileEngine::filterPresets() const
{
    const QString m = mode();
    if (m == QLatin1String("CW") || m == QLatin1String("CWL")) {
        return {100, 250, 500, 1000};
    }
    if (m == QLatin1String("AM") || m == QLatin1String("SAM") || m == QLatin1String("DSB")) {
        return {6000, 8000, 10000};
    }
    if (m == QLatin1String("FM") || m == QLatin1String("NFM") || m == QLatin1String("DFM")) {
        return {};
    }
    if (m == QLatin1String("DIGU") || m == QLatin1String("DIGL") || m == QLatin1String("RTTY")) {
        return {500, 1500, 3000};
    }
    return {1800, 2400, 2700, 3000};
}

void MobileEngine::setFilterWidth(int widthHz)
{
    if (!m_slice || widthHz <= 0) {
        return;
    }
    // Edge placement per mode, as VfoWidget/rigctld place a passband width.
    const QString m = m_slice->mode();
    int lo = 95;
    int hi = widthHz;
    if (m == QLatin1String("CW") || m == QLatin1String("CWL") || m == QLatin1String("AM")
        || m == QLatin1String("SAM") || m == QLatin1String("DSB")) {
        lo = -widthHz / 2;
        hi = widthHz / 2;
    } else if (m == QLatin1String("LSB") || m == QLatin1String("DIGL")) {
        lo = -widthHz;
        hi = -95;
    }
    m_slice->setFilterWidth(lo, hi);
}

void MobileEngine::onConnectionStateChanged(bool connected)
{
    m_connected = connected;
    if (connected) {
        wireRxAudio();
        wireTxPacketSink();
        startAudio();
        setKeepScreenAwake(true);
        setConnectionState(QStringLiteral("connected"));
        selectActiveSlice();
        emit transmitStateChanged();
        emit rfPowerChanged();
        emit tuneStateChanged();
        emit atuChanged();
        emit txAudioChanged();
        return;
    }
    stopTransmit(QStringLiteral("radio disconnected"));
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
    if (!chosen && !slices.isEmpty()) {
        chosen = slices.first();
    }
    if (chosen != m_slice) {
        bindSlice(chosen);
    }
    PanadapterModel* pan = chosen ? m_session.radioModel().panadapter(chosen->panId()) : nullptr;
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
    if (slice) {
        m_sliceConnections.append(connect(slice, &SliceModel::frequencyChanged, this, &MobileEngine::frequencyChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::modeChanged, this, &MobileEngine::modeChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::modeListChanged, this, &MobileEngine::modesChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::audioGainChanged, this, &MobileEngine::afGainChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::audioMuteChanged, this, &MobileEngine::mutedChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::filterChanged, this, &MobileEngine::filterChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::nrChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::nbChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::anfChanged, this, &MobileEngine::dspChanged));
        m_sliceConnections.append(connect(slice, &SliceModel::agcModeChanged, this, &MobileEngine::dspChanged));
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
        sendPanDimensions(pan->panId());
    }
    emit panGeometryChanged();
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
    return m_pan ? m_pan->centerMhz() : 0.0;
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
    if (error != m_lastError) {
        m_lastError = error;
        emit lastErrorChanged();
    }
}

} // namespace AetherSDR::mobile
