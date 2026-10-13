#pragma once

#include "RadioListModel.h"

#include "core/TxCoordinator.h"
#include "core/backends/RadioCapabilities.h"
#include "models/RadioSession.h"
#include "models/TxController.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
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
    Q_PROPERTY(bool controlsVisible READ controlsVisible WRITE setControlsVisible NOTIFY controlsVisibleChanged)
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
    Q_PROPERTY(bool radioTransmitting READ radioTransmitting NOTIFY txMetersChanged)
    Q_PROPERTY(double compressionDb READ compressionDb NOTIFY txMetersChanged)
    Q_PROPERTY(double alcDbfs READ alcDbfs NOTIFY txMetersChanged)
    Q_PROPERTY(bool alcAvailable READ alcAvailable NOTIFY txMetersChanged)
    Q_PROPERTY(double paTempC READ paTempC NOTIFY txMetersChanged)
    Q_PROPERTY(bool hasPaTemp READ hasPaTemp NOTIFY txMetersChanged)
    Q_PROPERTY(double supplyVolts READ supplyVolts NOTIFY txMetersChanged)
    Q_PROPERTY(bool hasSupplyVolts READ hasSupplyVolts NOTIFY txMetersChanged)
    Q_PROPERTY(QStringList meterKinds READ meterKinds CONSTANT)
    Q_PROPERTY(QString meterLeft READ meterLeft WRITE setMeterLeft NOTIFY meterSelectionChanged)
    Q_PROPERTY(QString meterRight READ meterRight WRITE setMeterRight NOTIFY meterSelectionChanged)
    Q_PROPERTY(bool tuning READ tuning NOTIFY tuneStateChanged)
    Q_PROPERTY(bool tuneAvailable READ tuneAvailable NOTIFY tuneStateChanged)
    Q_PROPERTY(int tunePower READ tunePower WRITE setTunePower NOTIFY tuneStateChanged)
    Q_PROPERTY(bool hasTuner READ hasTuner NOTIFY atuChanged)
    Q_PROPERTY(bool hasTunerMemories READ hasTunerMemories NOTIFY atuChanged)
    Q_PROPERTY(QString atuStatusText READ atuStatusText NOTIFY atuChanged)
    Q_PROPERTY(bool atuTuned READ atuTuned NOTIFY atuChanged)
    Q_PROPERTY(bool atuMemories READ atuMemories WRITE setAtuMemories NOTIFY atuChanged)
    Q_PROPERTY(bool procEnabled READ procEnabled WRITE setProcEnabled NOTIFY txAudioChanged)
    Q_PROPERTY(int procLevel READ procLevel WRITE setProcLevel NOTIFY txAudioChanged)
    Q_PROPERTY(int procLevelMax READ procLevelMax NOTIFY txAudioChanged)
    Q_PROPERTY(bool monitor READ monitor WRITE setMonitor NOTIFY txAudioChanged)
    Q_PROPERTY(int monitorGain READ monitorGain WRITE setMonitorGain NOTIFY txAudioChanged)
    Q_PROPERTY(int radioMicLevel READ radioMicLevel WRITE setRadioMicLevel NOTIFY txAudioChanged)

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
    // Whether the operate page shows its control panel or gives the spectrum
    // the room (PTT always stays). Remembered on the device.
    bool controlsVisible() const;
    void setControlsVisible(bool visible);

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
    // Radio actually on the air (MOX, tune, ATU, VOX); TX-only meters show
    // a value only then.
    bool radioTransmitting() const;
    double compressionDb() const { return m_compressionDb; }
    double alcDbfs() const { return m_alcDbfs; }
    bool alcAvailable() const { return m_alcAvailable; }
    double paTempC() const { return m_paTempC; }
    bool hasPaTemp() const;
    double supplyVolts() const { return m_supplyVolts; }
    bool hasSupplyVolts() const;
    // Selectable meters for the two meter faces, remembered on the device.
    QStringList meterKinds() const;
    QString meterLeft() const;
    void setMeterLeft(const QString& kind);
    QString meterRight() const;
    void setMeterRight(const QString& kind);

    // Tune carrier and the radio's ATU. Both transmit, so both go through
    // the local TxController (Activity::Tune / ::Atu) like PTT.
    bool tuning() const;
    bool tuneAvailable() const;
    int tunePower() const;
    void setTunePower(int power);
    bool hasTuner() const;
    bool hasTunerMemories() const;
    QString atuStatusText() const;
    bool atuTuned() const;
    bool atuMemories() const;
    void setAtuMemories(bool on);

    // Radio-side TX audio: speech processor (PROC), monitor, mic level.
    bool procEnabled() const;
    void setProcEnabled(bool on);
    int procLevel() const;
    void setProcLevel(int level);
    int procLevelMax() const;
    bool monitor() const;
    void setMonitor(bool on);
    int monitorGain() const;
    void setMonitorGain(int gain);
    int radioMicLevel() const;
    void setRadioMicLevel(int level);

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
    // Tune carrier on/off (latched, like the desktop TUNE button).
    Q_INVOKABLE void toggleTune();
    // ATU: start a tune cycle, or bypass when already tuned on this frequency.
    Q_INVOKABLE void atuPressed();
    // Ends every transmission this client keyed (PTT, tune, ATU cycle).
    Q_INVOKABLE void stopTransmit(const QString& reason);
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
    void controlsVisibleChanged();
    void rfPowerChanged();
    void micGainChanged();
    void txMetersChanged();
    void dspChanged();
    void meterSelectionChanged();
    void tuneStateChanged();
    void atuChanged();
    void txAudioChanged();
    void stepChanged();
    // Frames of the active panadapter only. Spectrum bins are dBm and span
    // the pan. Waterfall rows are intensities on a backend-dependent scale
    // covering lowMhz..highMhz, which need not equal the pan's range.
    void spectrumFrame(const QVector<float>& binsDbm);
    void waterfallRow(const QVector<float>& bins, double lowMhz, double highMhz);

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
    bool ensureMicrophonePermission();
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
    TxController::Input m_tuneInput;
    TxController::Input m_atuInput;
    double m_atuTunedFreqMhz{-1.0};
    double m_compressionDb{0.0};
    double m_alcDbfs{-100.0};
    bool m_alcAvailable{false};
    double m_paTempC{0.0};
    double m_supplyVolts{0.0};
    bool m_pttHeld{false};
    bool m_micPermissionRequested{false};
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
    QTimer m_panPixelsTimer;
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
