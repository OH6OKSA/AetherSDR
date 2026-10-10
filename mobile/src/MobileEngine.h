#pragma once

#include "RadioListModel.h"

#include "models/RadioSession.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QJSEngine;
class QQmlEngine;
class QThread;

namespace AetherSDR {
class AudioEngine;
class PanadapterModel;
class RadioDiscoverySource;
class SliceModel;
struct PcmFrame;
struct SpectrumDecodeScale;
}

namespace AetherSDR::mobile {

// Composition root of the touch client: owns the radio session, discovery and
// the audio engine, and exposes the active slice to QML. Lives on the main
// thread, like RadioModel and its sub-models.
class MobileEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(AetherSDR::mobile::RadioListModel* radios READ radios CONSTANT)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString radioName READ radioName NOTIFY connectionStateChanged)
    Q_PROPERTY(double frequencyMhz READ frequencyMhz WRITE setFrequencyMhz NOTIFY frequencyChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QStringList modes READ modes NOTIFY modesChanged)
    Q_PROPERTY(double afGain READ afGain WRITE setAfGain NOTIFY afGainChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double sMeterDbm READ sMeterDbm NOTIFY sMeterChanged)
    Q_PROPERTY(QString sMeterText READ sMeterText NOTIFY sMeterChanged)
    Q_PROPERTY(double panCenterMhz READ panCenterMhz NOTIFY panGeometryChanged)
    Q_PROPERTY(double panBandwidthMhz READ panBandwidthMhz NOTIFY panGeometryChanged)
    Q_PROPERTY(bool transmitAvailable READ transmitAvailable CONSTANT)
    Q_PROPERTY(QString transmitUnavailableReason READ transmitUnavailableReason CONSTANT)
    Q_PROPERTY(bool transmitting READ transmitting NOTIFY transmittingChanged)

public:
    explicit MobileEngine(QObject* parent = nullptr);
    ~MobileEngine() override;

    // The instance main() constructed; QML's Engine singleton resolves to it.
    static MobileEngine* instance();

    RadioListModel* radios() { return &m_radios; }
    QString connectionState() const { return m_connectionState; }
    QString lastError() const { return m_lastError; }
    QString radioName() const { return m_radioName; }

    double frequencyMhz() const;
    void setFrequencyMhz(double mhz);
    QString mode() const;
    void setMode(const QString& mode);
    QStringList modes() const;
    double afGain() const;
    void setAfGain(double gain);
    bool muted() const;
    void setMuted(bool muted);
    double sMeterDbm() const { return m_sMeterDbm; }
    QString sMeterText() const;
    double panCenterMhz() const;
    double panBandwidthMhz() const;

    // TX stays unavailable until the mic path and TxController wiring land.
    bool transmitAvailable() const { return false; }
    QString transmitUnavailableReason() const;
    bool transmitting() const { return false; }

    Q_INVOKABLE void connectToRadio(int row);
    // Development aid (--demo): connect once the simulator entry appears.
    void connectToDemoWhenListed();
    Q_INVOKABLE void disconnectRadio();
    Q_INVOKABLE void setPtt(bool down);
    // The spectrum view reports its size so the radio sends matching frames.
    Q_INVOKABLE void setPanPixels(int width, int height);

signals:
    void connectionStateChanged();
    void lastErrorChanged();
    void frequencyChanged();
    void modeChanged();
    void modesChanged();
    void afGainChanged();
    void mutedChanged();
    void sMeterChanged();
    void panGeometryChanged();
    void transmittingChanged();
    // Frames of the active panadapter only. Spectrum bins are dBm.
    // Waterfall rows are intensities on a backend-dependent scale.
    void spectrumFrame(const QVector<float>& binsDbm);
    void waterfallRow(const QVector<float>& bins);

private:
    void startAudio();
    void stopAudio();
    void wireRxAudio();
    void onConnectionStateChanged(bool connected);
    void onSliceAdded(SliceModel* slice);
    void selectActiveSlice();
    void bindSlice(SliceModel* slice);
    void bindPan(PanadapterModel* pan);
    void sendPanDimensions(const QString& panId);
    void setConnectionState(const QString& state);
    void setLastError(const QString& error);

    RadioSession m_session;
    RadioListModel m_radios;
    std::unique_ptr<RadioDiscoverySource> m_discovery;
    QThread* m_audioThread{nullptr};
    AudioEngine* m_audio{nullptr};
    QList<QMetaObject::Connection> m_rxAudioConnections;
    QList<QMetaObject::Connection> m_sliceConnections;
    QList<QMetaObject::Connection> m_panConnections;
    QPointer<SliceModel> m_slice;
    QPointer<PanadapterModel> m_pan;
    QString m_connectionState{QStringLiteral("idle")};
    QString m_lastError;
    QString m_radioName;
    double m_sMeterDbm{-130.0};
    int m_panWidth{0};
    int m_panHeight{0};
};

// Exposes the existing MobileEngine to QML as the Engine singleton instead of
// letting QML default-construct a second engine.
struct MobileEngineForeign {
    Q_GADGET
    QML_FOREIGN(AetherSDR::mobile::MobileEngine)
    QML_NAMED_ELEMENT(Engine)
    QML_SINGLETON

public:
    static MobileEngine* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
};

} // namespace AetherSDR::mobile
