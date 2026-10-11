#pragma once

#include "RadioListModel.h"

#include "core/TxCoordinator.h"
#include "core/backends/RadioCapabilities.h"
#include "models/RadioSession.h"
#include "models/TxController.h"

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
struct RadioInfo;
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
    Q_PROPERTY(int filterLow READ filterLow NOTIFY filterChanged)
    Q_PROPERTY(int filterHigh READ filterHigh NOTIFY filterChanged)
    Q_PROPERTY(QList<int> filterPresets READ filterPresets NOTIFY modeChanged)
    Q_PROPERTY(QStringList bands READ bands CONSTANT)
    Q_PROPERTY(bool bandsAvailable READ bandsAvailable NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastAddress READ lastAddress NOTIFY lastAddressChanged)
    Q_PROPERTY(bool transmitAvailable READ transmitAvailable NOTIFY transmitStateChanged)
    Q_PROPERTY(QString transmitUnavailableReason READ transmitUnavailableReason NOTIFY transmitStateChanged)
    Q_PROPERTY(bool canUseDeviceMicrophone READ canUseDeviceMicrophone NOTIFY transmitStateChanged)
    Q_PROPERTY(bool transmitting READ transmitting NOTIFY transmittingChanged)
    Q_PROPERTY(bool nr READ nr WRITE setNr NOTIFY dspChanged)
    Q_PROPERTY(bool nb READ nb WRITE setNb NOTIFY dspChanged)
    Q_PROPERTY(bool anf READ anf WRITE setAnf NOTIFY dspChanged)
    Q_PROPERTY(QString agcMode READ agcMode WRITE setAgcMode NOTIFY dspChanged)
    Q_PROPERTY(QStringList agcModes READ agcModes CONSTANT)
    Q_PROPERTY(int stepHz READ stepHz NOTIFY stepChanged)
    Q_PROPERTY(int rfPower READ rfPower WRITE setRfPower NOTIFY rfPowerChanged)
    Q_PROPERTY(int micGain READ micGain WRITE setMicGain NOTIFY micGainChanged)
    Q_PROPERTY(double micLevelDbfs READ micLevelDbfs NOTIFY txMetersChanged)
    Q_PROPERTY(double micPeakDbfs READ micPeakDbfs NOTIFY txMetersChanged)
    Q_PROPERTY(double forwardPowerW READ forwardPowerW NOTIFY txMetersChanged)
    Q_PROPERTY(double swr READ swr NOTIFY txMetersChanged)
    Q_PROPERTY(bool swrValid READ swrValid NOTIFY txMetersChanged)

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

    int filterLow() const;
    int filterHigh() const;
    // Passband widths (Hz) offered for the current mode.
    QList<int> filterPresets() const;
    QStringList bands() const;
    bool bandsAvailable() const;
    QString lastAddress() const;

    // PTT is offered only when connected, the radio can transmit and its mic
    // input is this device (mic_selection=PC); the reason says which is missing.
    bool transmitAvailable() const;
    QString transmitUnavailableReason() const;
    bool canUseDeviceMicrophone() const;
    bool transmitting() const;
    bool nr() const;
    void setNr(bool on);
    bool nb() const;
    void setNb(bool on);
    bool anf() const;
    void setAnf(bool on);
    QString agcMode() const;
    void setAgcMode(const QString& mode);
    QStringList agcModes() const;
    // The active slice's tuning step (radio-reported), Hz.
    int stepHz() const;
    // Radio RF power setting (Flex rfpower, 0..100).
    int rfPower() const;
    void setRfPower(int power);
    // Client-side gain on this device's microphone, 0..100 (PcMicGain).
    int micGain() const;
    void setMicGain(int gain);
    double micLevelDbfs() const { return m_micLevelDbfs; }
    double micPeakDbfs() const { return m_micPeakDbfs; }
    double forwardPowerW() const { return m_forwardPowerW; }
    double swr() const { return m_swr; }
    bool swrValid() const { return m_swrValid; }

    Q_INVOKABLE void connectToRadio(int row);
    // A FlexRadio by address, for networks where discovery broadcasts do not
    // arrive (iOS needs a multicast entitlement to receive them).
    Q_INVOKABLE void connectToAddress(const QString& host);
    // Development aid (--demo): connect once the simulator entry appears.
    void connectToDemoWhenListed();
    Q_INVOKABLE void disconnectRadio();
    // Hold-to-talk: true keys through the local TxController, false releases
    // that same input. Every disconnect or loss of foreground also releases.
    // `reason` names the release in the log ("released", "touch cancelled").
    Q_INVOKABLE void setPtt(bool down, const QString& reason = QString());
    // Switch the radio's transmit audio input to this device's microphone.
    Q_INVOKABLE void useDeviceMicrophone();
    // Tune, recentring the pan when the target lies outside it.
    Q_INVOKABLE void tuneTo(double mhz);
    // Band change through the radio's band stack (Flex), which restores the
    // last frequency and mode used on that band.
    Q_INVOKABLE void selectBand(const QString& band);
    Q_INVOKABLE void setFilterWidth(int widthHz);
    // Tune by whole tuning steps, landing on the step grid.
    Q_INVOKABLE void tuneBySteps(int steps);
    // Tune to a point on the display, rounded to the tuning step.
    Q_INVOKABLE void tuneToSnapped(double mhz);
    // Multiply the pan span by factor (<1 zooms in), centred on the slice.
    Q_INVOKABLE void zoom(double factor);
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
    void transmitStateChanged();
    void filterChanged();
    void lastAddressChanged();
    void rfPowerChanged();
    void micGainChanged();
    void txMetersChanged();
    void dspChanged();
    void stepChanged();
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
    void connectWithInfo(const RadioInfo& info, const QString& displayName);
    void wireTransmit();
    void wireTxPacketSink();
    void applyTxAudioCapabilities(bool connected, const RadioCapabilities& caps);
    void startDeviceMicrophoneCapture();
    void stopDeviceMicrophoneCapture();
    void releasePtt(const QString& reason);

    RadioSession m_session;
    RadioListModel m_radios;
    std::unique_ptr<RadioDiscoverySource> m_discovery;
    QThread* m_audioThread{nullptr};
    AudioEngine* m_audio{nullptr};
    QList<QMetaObject::Connection> m_rxAudioConnections;
    QList<QMetaObject::Connection> m_sliceConnections;
    QList<QMetaObject::Connection> m_panConnections;
    QMetaObject::Connection m_txSinkConnection;
    TxCoordinator::Producer m_microphoneProducer;
    TxController::Input m_pttInput;
    bool m_pttHeld{false};
    bool m_connected{false};
    RadioCapabilities m_caps;
    double m_micLevelDbfs{-100.0};
    double m_micPeakDbfs{-100.0};
    double m_forwardPowerW{0.0};
    double m_swr{1.0};
    bool m_swrValid{false};
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
