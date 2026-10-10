#include "MobileEngine.h"

#include "core/AppActivity.h"
#include "core/AudioEngine.h"
#include "core/RadioDiscovery.h"
#include "core/backends/IRadioBackend.h"
#include "core/backends/flex/PanadapterStream.h"
#include "core/discovery/RadioDiscoverySource.h"
#include "models/MeterModel.h"
#include "models/PanadapterModel.h"
#include "models/SliceModel.h"

#include <QHostAddress>
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
    connect(&rm, &RadioModel::backendRebuilt, this, &MobileEngine::wireRxAudio);
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
    m_radioName = radio->nickname.isEmpty() ? radio->model : radio->nickname;
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
    setPtt(false);
    m_session.radioModel().disconnectFromRadio();
}

void MobileEngine::setPtt(bool down)
{
    // No keying path exists yet; a release must never be lost once it does.
    Q_UNUSED(down);
}

QString MobileEngine::transmitUnavailableReason() const
{
    return tr("Transmit is not available in this prototype yet: the microphone path is not wired.");
}

void MobileEngine::onConnectionStateChanged(bool connected)
{
    if (connected) {
        wireRxAudio();
        startAudio();
        setConnectionState(QStringLiteral("connected"));
        selectActiveSlice();
        return;
    }
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
    }
    emit frequencyChanged();
    emit modeChanged();
    emit modesChanged();
    emit afGainChanged();
    emit mutedChanged();
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
