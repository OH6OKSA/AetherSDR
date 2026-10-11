#include "MobileEngine.h"

#include "ScreenAwake.h"

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
const QString kDeviceMicSelection = QStringLiteral("PC");
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
        releasePtt("backend rebuilt");
        wireRxAudio();
        wireTxPacketSink();
    });
    wireTransmit();
    // Losing the foreground (call, app switch, lock) must never leave the
    // transmitter keyed.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) {
            releasePtt("application inactive");
        }
    });
    connect(&rm, &RadioModel::sliceAdded, this, &MobileEngine::onSliceAdded);
    connect(&rm, &RadioModel::sliceRemoved, this, [this](int) { selectActiveSlice(); });
    connect(&rm, &RadioModel::panadapterAdded, this, [this](PanadapterModel* pan) {
        sendPanDimensions(pan->panId());
        selectActiveSlice();
    });
    connect(&rm, &RadioModel::panDimensionsNeeded, this, &MobileEngine::sendPanDimensions);

    connect(&rm, &RadioModel::panFeedSpectrumReady, this,
            [this](quint32 streamId, const QVector<float>& bins, qint64, const SpectrumDecodeScale&) {
        if (m_pan && m_pan->panStreamId() == streamId) {
            emit spectrumFrame(bins);
        }
    });
    connect(&rm, &RadioModel::panFeedWaterfallRowReady, this,
            [this](quint32 streamId, const QVector<float>& bins, double, double, quint32, qint64, bool) {
        if (m_pan && m_pan->wfStreamId() == streamId) {
            emit waterfallRow(bins);
        }
    });
    connect(&rm.meterModel(), &MeterModel::sLevelChanged, this, [this](int sliceIndex, float dbm) {
        if (m_slice && m_slice->sliceId() == sliceIndex) {
            m_sMeterDbm = dbm;
            emit sMeterChanged();
        }
    });

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
    releasePtt("shutdown");
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
    releasePtt("operator disconnect");
    m_session.radioModel().disconnectFromRadio();
}

QString MobileEngine::lastAddress() const
{
    return AppSettings::instance().value(kLastAddressKey).toString();
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
        if (m_session.radioModel().transmitModel().micSelection() == kDeviceMicSelection) {
            startDeviceMicrophoneCapture();
        } else {
            stopDeviceMicrophoneCapture();
        }
        emit transmitStateChanged();
    });
    connect(&rm, &RadioModel::txAudioStreamReady, this, [this](quint32 streamId) {
        AudioEngine* audio = m_audio;
        QMetaObject::invokeMethod(audio, [audio, streamId] {
            audio->setTxStreamId(streamId);
            audio->setOpusTxEnabled(true);
        }, Qt::QueuedConnection);
        if (m_session.radioModel().transmitModel().micSelection() == kDeviceMicSelection) {
            startDeviceMicrophoneCapture();
        }
    });
    connect(&rm.transmitModel(), &TransmitModel::micStateChanged, this, [this] {
        if (m_session.radioModel().transmitModel().micSelection() == kDeviceMicSelection) {
            startDeviceMicrophoneCapture();
        } else {
            releasePtt("mic input left this device");
            stopDeviceMicrophoneCapture();
        }
        emit transmitStateChanged();
    });
    connect(&rm.transmitModel(), &TransmitModel::micInputListChanged, this, &MobileEngine::transmitStateChanged);
    connect(&rm.transmitModel(), &TransmitModel::transmittingChanged, this, &MobileEngine::transmittingChanged);
    connect(&rm.transmitModel(), &TransmitModel::rfPowerChanged, this, &MobileEngine::rfPowerChanged);
    connect(&rm.meterModel(), &MeterModel::micMetersChanged, this,
            [this](float micLevel, float, float micPeak, float) {
        m_micLevelDbfs = micLevel;
        m_micPeakDbfs = micPeak;
        emit txMetersChanged();
    });
    connect(&rm.meterModel(), &MeterModel::txMetersChanged, this,
            [this](float fwdPower, float swr, bool swrValid) {
        m_forwardPowerW = fwdPower;
        m_swr = swr;
        m_swrValid = swrValid;
        emit txMetersChanged();
    });
    connect(&rm, &RadioModel::capabilitiesChanged, this, &MobileEngine::applyTxAudioCapabilities);
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

void MobileEngine::startDeviceMicrophoneCapture()
{
    QMicrophonePermission permission;
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(permission, this, [this](const QPermission& result) {
            if (result.status() == Qt::PermissionStatus::Granted) {
                startDeviceMicrophoneCapture();
            } else {
                setLastError(tr("Microphone access is off. Allow it in Settings to transmit."));
            }
            emit transmitStateChanged();
        });
        return;
    case Qt::PermissionStatus::Denied:
        setLastError(tr("Microphone access is off. Allow it in Settings to transmit."));
        return;
    case Qt::PermissionStatus::Granted:
        break;
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

void MobileEngine::setPtt(bool down)
{
    if (!down) {
        releasePtt("operator release");
        return;
    }
    if (m_pttHeld || !transmitAvailable()) {
        return;
    }
    m_pttInput = m_session.radioModel().localTxController()->capture(TxController::Activity::Mox);
    m_pttHeld = true;
    if (!m_pttInput.start()) {
        m_pttHeld = false;
        m_pttInput.stop();
        setLastError(tr("The radio did not accept the transmit request."));
    }
}

void MobileEngine::releasePtt(const char* reason)
{
    if (!m_pttHeld) {
        return;
    }
    m_pttHeld = false;
    m_pttInput.stop();
    qCInfo(lcMobile) << "PTT released:" << reason;
}

// ---- tuning, bands, filters ------------------------------------------------

void MobileEngine::tuneTo(double mhz)
{
    if (!m_slice || mhz <= 0.0) {
        return;
    }
    const bool insidePan = m_pan && m_pan->bandwidthMhz() > 0.0
        && std::abs(mhz - m_pan->centerMhz()) < m_pan->bandwidthMhz() / 2.0;
    if (insidePan) {
        m_slice->setFrequency(mhz);
    } else {
        m_slice->tuneAndRecenter(mhz);
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
        return;
    }
    releasePtt("radio disconnected");
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
    }
    emit frequencyChanged();
    emit modeChanged();
    emit modesChanged();
    emit afGainChanged();
    emit mutedChanged();
    emit filterChanged();
    emit dspChanged();
    emit stepChanged();
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
    if (m_pan) {
        sendPanDimensions(m_pan->panId());
    }
}

void MobileEngine::sendPanDimensions(const QString& panId)
{
    RadioModel& rm = m_session.radioModel();
    if (panId.isEmpty() || m_panWidth <= 0 || m_panHeight <= 0 || !rm.panadapter(panId)) {
        return;
    }
    // Self-shaping backends take the width through the seam; the
    // xpixels/ypixels text is the Flex command-plane contract.
    if (!rm.requestLocalPanPixelWidth(panId, m_panWidth) && rm.hasCommandPlane()) {
        rm.sendCommand(QStringLiteral("display pan set %1 xpixels=%2 ypixels=%3")
                           .arg(panId).arg(m_panWidth).arg(m_panHeight));
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
