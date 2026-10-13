#pragma once

#include "RadioListModel.h"

#include "core/TxCoordinator.h"
#include "core/backends/flex/SmartLinkClient.h"
#include "core/backends/flex/WanConnection.h"
#include "core/backends/RadioCapabilities.h"
#include "models/RadioSession.h"
#include "models/TxController.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QJSEngine;
class QQmlEngine;
class QThread;

namespace AetherSDR {
class AudioEngine;
class CwRxModel;
class DecoderAudioModel;
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
    // The preset nearest the current passband width, or 0 if none is close.
    Q_PROPERTY(int activeFilterPreset READ activeFilterPreset NOTIFY filterChanged)
    Q_PROPERTY(QStringList bands READ bands CONSTANT)
    Q_PROPERTY(bool bandsAvailable READ bandsAvailable NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastAddress READ lastAddress NOTIFY lastAddressChanged)
    Q_PROPERTY(QString smartLinkState READ smartLinkState NOTIFY smartLinkChanged)
    Q_PROPERTY(QString smartLinkAccount READ smartLinkAccount NOTIFY smartLinkChanged)
    Q_PROPERTY(QString smartLinkEmail READ smartLinkEmail NOTIFY smartLinkChanged)
    Q_PROPERTY(QVariantList smartLinkRadios READ smartLinkRadios NOTIFY smartLinkRadiosChanged)
    Q_PROPERTY(bool remoteSession READ remoteSession NOTIFY connectionStateChanged)
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
    Q_PROPERTY(bool ritOn READ ritOn WRITE setRitOn NOTIFY ritXitChanged)
    Q_PROPERTY(int ritHz READ ritHz WRITE setRitHz NOTIFY ritXitChanged)
    Q_PROPERTY(bool xitOn READ xitOn WRITE setXitOn NOTIFY ritXitChanged)
    Q_PROPERTY(int xitHz READ xitHz WRITE setXitHz NOTIFY ritXitChanged)
    Q_PROPERTY(bool squelchAvailable READ squelchAvailable NOTIFY modeChanged)
    Q_PROPERTY(bool squelchOn READ squelchOn WRITE setSquelchOn NOTIFY squelchChanged)
    Q_PROPERTY(int squelchLevel READ squelchLevel WRITE setSquelchLevel NOTIFY squelchChanged)
    Q_PROPERTY(QString rxAntenna READ rxAntenna WRITE setRxAntenna NOTIFY antennaChanged)
    Q_PROPERTY(QStringList rxAntennas READ rxAntennas NOTIFY antennaChanged)
    Q_PROPERTY(QString txAntenna READ txAntenna WRITE setTxAntenna NOTIFY antennaChanged)
    Q_PROPERTY(QStringList txAntennas READ txAntennas NOTIFY antennaChanged)
    Q_PROPERTY(QVariantList slices READ slices NOTIFY slicesChanged)
    Q_PROPERTY(int activeSliceId READ activeSliceId NOTIFY slicesChanged)
    Q_PROPERTY(bool canAddSlice READ canAddSlice NOTIFY slicesChanged)
    Q_PROPERTY(bool canAddPanadapter READ canAddPanadapter NOTIFY slicesChanged)
    // This client's panadapters. The app shows one; the others still stream.
    Q_PROPERTY(int panCount READ panCount NOTIFY slicesChanged)
    Q_PROPERTY(bool cwMode READ cwMode NOTIFY modeChanged)
    Q_PROPERTY(bool cwKeyAvailable READ cwKeyAvailable NOTIFY transmitStateChanged)
    Q_PROPERTY(bool cwxAvailable READ cwxAvailable NOTIFY transmitStateChanged)
    Q_PROPERTY(bool cwKeyDown READ cwKeyDown NOTIFY cwChanged)
    Q_PROPERTY(int cwSpeed READ cwSpeed WRITE setCwSpeed NOTIFY cwChanged)
    Q_PROPERTY(int cwPitch READ cwPitch WRITE setCwPitch NOTIFY cwChanged)
    Q_PROPERTY(bool cwBreakIn READ cwBreakIn WRITE setCwBreakIn NOTIFY cwChanged)
    Q_PROPERTY(bool cwDecoderEnabled READ cwDecoderEnabled WRITE setCwDecoderEnabled NOTIFY cwDecoderChanged)
    // Tuning lock of the active slice (the radio's slice lock; SliceModel
    // refuses tuning while it is on). tuneBlocked fires on a refused tune.
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)
    // The radio's profiles (as SmartSDR/AetherSDR): global (band, pans,
    // slices…), transmit and mic. Type is "global", "transmit" or "mic".
    Q_PROPERTY(QStringList globalProfiles READ globalProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeGlobalProfile READ activeGlobalProfile NOTIFY profilesChanged)
    Q_PROPERTY(QStringList txProfiles READ txProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeTxProfile READ activeTxProfile NOTIFY profilesChanged)
    Q_PROPERTY(QStringList micProfiles READ micProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeMicProfile READ activeMicProfile NOTIFY profilesChanged)
    // TX audio passband (radio: transmit filter_low/filter_high).
    Q_PROPERTY(int txFilterLow READ txFilterLow WRITE setTxFilterLow NOTIFY txFilterChanged)
    Q_PROPERTY(int txFilterHigh READ txFilterHigh WRITE setTxFilterHigh NOTIFY txFilterChanged)
    // Radio's 8-band RX/TX equalizer (63 Hz … 8 kHz, −10…+10 dB).
    Q_PROPERTY(QStringList eqBandLabels READ eqBandLabels CONSTANT)
    Q_PROPERTY(bool eqTxEnabled READ eqTxEnabled NOTIFY eqChanged)
    Q_PROPERTY(bool eqRxEnabled READ eqRxEnabled NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqTxBands READ eqTxBands NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqRxBands READ eqRxBands NOTIFY eqChanged)
    // CW memories (six, remembered): sent through CWX; {CALL} becomes the
    // radio's callsign.
    Q_PROPERTY(QStringList cwMemories READ cwMemories NOTIFY cwMemoriesChanged)
    // Display levels (remembered, client-side like the desktop's SW auto
    // black): the waterfall's black level and colour gain shift and stretch
    // the auto-tracked range (Auto off freezes it); the trace is auto-ranged
    // or set by reference level (top) and range.
    Q_PROPERTY(bool wfAutoLevel READ wfAutoLevel WRITE setWfAutoLevel NOTIFY displayLevelsChanged)
    Q_PROPERTY(int wfBlackLevel READ wfBlackLevel WRITE setWfBlackLevel NOTIFY displayLevelsChanged)
    Q_PROPERTY(int wfColorGain READ wfColorGain WRITE setWfColorGain NOTIFY displayLevelsChanged)
    Q_PROPERTY(bool traceAutoRange READ traceAutoRange WRITE setTraceAutoRange NOTIFY displayLevelsChanged)
    Q_PROPERTY(int traceRefDbm READ traceRefDbm WRITE setTraceRefDbm NOTIFY displayLevelsChanged)
    Q_PROPERTY(int traceRangeDb READ traceRangeDb WRITE setTraceRangeDb NOTIFY displayLevelsChanged)
    // 3D stacked trace (the desktop's 3DSS, CPU-drawn) in place of the
    // 2D waterfall.
    Q_PROPERTY(bool spectrum3D READ spectrum3D WRITE setSpectrum3D NOTIFY displayLevelsChanged)
    Q_PROPERTY(QString cwDecodedText READ cwDecodedText NOTIFY cwDecoderChanged)
    Q_PROPERTY(QString cwDecoderStats READ cwDecoderStats NOTIFY cwDecoderChanged)
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
    // While transmitting the faces show their own TX choices (default SWR and
    // Power), so receive keeps S while transmit shows SWR.
    Q_PROPERTY(QString meterLeftTx READ meterLeftTx NOTIFY meterSelectionChanged)
    Q_PROPERTY(QString meterRightTx READ meterRightTx NOTIFY meterSelectionChanged)
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
    int activeFilterPreset() const;
    QStringList bands() const;
    bool bandsAvailable() const;
    QString lastAddress() const;

    // SmartLink (remote access through FlexRadio's service). State is
    // "signedOut", "signingIn", "signedIn" or "offline" (signed in, server
    // not reachable). The refresh token lives in the iOS Keychain.
    QString smartLinkState() const { return m_smartLinkState; }
    QString smartLinkAccount() const;
    QString smartLinkEmail() const;
    QVariantList smartLinkRadios() const;
    bool remoteSession() const;
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
    // RIT / XIT of the active slice, Hz (clamped to +/-9999 like the desktop).
    bool ritOn() const;
    void setRitOn(bool on);
    int ritHz() const;
    void setRitHz(int hz);
    bool xitOn() const;
    void setXitOn(bool on);
    int xitHz() const;
    void setXitHz(int hz);
    // Squelch is offered outside digital and CW modes (desktop rule).
    bool squelchAvailable() const;
    bool squelchOn() const;
    void setSquelchOn(bool on);
    int squelchLevel() const;
    void setSquelchLevel(int level);
    // Antennas of the active slice; lists come from the radio.
    QString rxAntenna() const;
    void setRxAntenna(const QString& antenna);
    QStringList rxAntennas() const;
    QString txAntenna() const;
    void setTxAntenna(const QString& antenna);
    QStringList txAntennas() const;
    // All slices: {id, letter, frequency, mode, active, tx, panId}.
    QVariantList slices() const;
    int activeSliceId() const;
    bool canAddSlice() const;
    bool canAddPanadapter() const;
    int panCount() const;
    // CW: straight key (hold), CWX text through the radio's keyer, speed,
    // pitch, break-in, and the receive decoder (ggmorse, CW modes only).
    bool cwMode() const;
    bool cwKeyAvailable() const;
    bool cwxAvailable() const;
    bool cwKeyDown() const { return m_cwKeyHeld; }
    int cwSpeed() const;
    void setCwSpeed(int wpm);
    int cwPitch() const;
    void setCwPitch(int hz);
    bool cwBreakIn() const;
    void setCwBreakIn(bool on);
    bool cwDecoderEnabled() const;
    bool locked() const;
    QStringList globalProfiles() const;
    QString activeGlobalProfile() const;
    QStringList txProfiles() const;
    QString activeTxProfile() const;
    QStringList micProfiles() const;
    QString activeMicProfile() const;
    Q_INVOKABLE void loadProfile(const QString& type, const QString& name);
    // Global: saves (overwrites) under the name. Transmit/mic: creates a new
    // profile from the current settings (the radio cannot overwrite those).
    Q_INVOKABLE void saveProfile(const QString& type, const QString& name);
    Q_INVOKABLE void deleteProfile(const QString& type, const QString& name);
    int txFilterLow() const;
    int txFilterHigh() const;
    void setTxFilterLow(int hz);
    void setTxFilterHigh(int hz);
    QStringList eqBandLabels() const;
    bool eqTxEnabled() const;
    bool eqRxEnabled() const;
    QVariantList eqTxBands() const;
    QVariantList eqRxBands() const;
    Q_INVOKABLE void setEqEnabled(bool tx, bool on);
    Q_INVOKABLE void setEqBand(bool tx, int band, int dB);
    Q_INVOKABLE void resetEq(bool tx);
    QStringList cwMemories() const;
    Q_INVOKABLE void setCwMemory(int index, const QString& text);
    Q_INVOKABLE void sendCwMemory(int index);
    void setLocked(bool on);
    bool wfAutoLevel() const { return displayInt("MobileWfAuto", 1) != 0; }
    void setWfAutoLevel(bool on) { setDisplayInt("MobileWfAuto", on ? 1 : 0); }
    int wfBlackLevel() const { return displayInt("MobileWfBlack", 50); }
    void setWfBlackLevel(int v) { setDisplayInt("MobileWfBlack", std::clamp(v, 0, 100)); }
    int wfColorGain() const { return displayInt("MobileWfGain", 50); }
    void setWfColorGain(int v) { setDisplayInt("MobileWfGain", std::clamp(v, 0, 100)); }
    bool traceAutoRange() const { return displayInt("MobileTraceAuto", 1) != 0; }
    void setTraceAutoRange(bool on) { setDisplayInt("MobileTraceAuto", on ? 1 : 0); }
    int traceRefDbm() const { return displayInt("MobileTraceRef", -40); }
    void setTraceRefDbm(int v) { setDisplayInt("MobileTraceRef", std::clamp(v, -140, 0)); }
    int traceRangeDb() const { return displayInt("MobileTraceRange", 90); }
    void setTraceRangeDb(int v) { setDisplayInt("MobileTraceRange", std::clamp(v, 20, 150)); }
    bool spectrum3D() const { return displayInt("MobileSpectrum3D", 0) != 0; }
    void setSpectrum3D(bool on) { setDisplayInt("MobileSpectrum3D", on ? 1 : 0); }
    void setCwDecoderEnabled(bool on);
    QString cwDecodedText() const { return m_cwDecodedText; }
    QString cwDecoderStats() const { return m_cwDecoderStats; }
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
    QString meterLeftTx() const { return meterChoice("MobileMeterLeftTx", "SWR"); }
    QString meterRightTx() const { return meterChoice("MobileMeterRightTx", "Power"); }
    // side "left"/"right", tx: the transmit choice instead of the receive one.
    Q_INVOKABLE void setMeter(const QString& side, bool tx, const QString& kind);

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
    Q_INVOKABLE void signInSmartLink(const QString& email, const QString& password);
    Q_INVOKABLE void signOutSmartLink();
    // Connect to the radio at `index` in smartLinkRadios.
    Q_INVOKABLE void connectSmartLinkRadio(int index);
    // Answer to certificateChanged: trust the radio's new certificate or not.
    Q_INVOKABLE void answerCertificate(bool accept);
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
    // Dev/screenshot runs: leave the radio's mic input alone.
    void setMicDefaultEnabled(bool enabled) { m_micDefaultEnabled = enabled; }
    // Tune, recentring the pan when the target lies outside it.
    Q_INVOKABLE void tuneTo(double mhz);
    // Band change through the radio's band stack (Flex), which restores the
    // last frequency and mode used on that band.
    Q_INVOKABLE void selectBand(const QString& band);
    Q_INVOKABLE void setFilterWidth(int widthHz);
    Q_INVOKABLE void selectSlice(int sliceId);
    Q_INVOKABLE void addSlice();
    Q_INVOKABLE void removeSlice(int sliceId);
    Q_INVOKABLE void makeTxSlice(int sliceId);
    // A new panadapter (with its own slice, created by the radio's defaults).
    Q_INVOKABLE void addPanadapter();
    // Closes this client's panadapters other than the one shown (and the
    // slices on them).
    Q_INVOKABLE void closeOtherPanadapters();
    // Straight key: down while held. Every release path also keys up.
    Q_INVOKABLE void setCwKey(bool down, const QString& reason = QString());
    Q_INVOKABLE void sendCwx(const QString& text);
    Q_INVOKABLE void stopCwx();
    Q_INVOKABLE void clearCwDecoded();
    // Tune by whole tuning steps, landing on the step grid.
    Q_INVOKABLE void tuneBySteps(int steps);
    // Step button: move by hz, landing on a multiple of |hz|. Off the grid
    // the first press only reaches the next grid point in that direction
    // (14.100002 +10 Hz -> 14.100010, -10 Hz -> 14.100000).
    Q_INVOKABLE void tuneByHz(int hz);
    // Tune to a point on the display, rounded to the tuning step.
    Q_INVOKABLE void tuneToSnapped(double mhz);
    // Move the view (pan centre) without retuning; coalesced so a drag sends
    // the radio at most one request per kPanMoveMs.
    Q_INVOKABLE void panTo(double centerMhz);
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
    void smartLinkChanged();
    void smartLinkRadiosChanged();
    // The radio presented a different TLS certificate than the one pinned
    // for it; the connect waits for answerCertificate().
    void certificateChanged(const QString& host, const QString& expected, const QString& presented);
    void rfPowerChanged();
    void micGainChanged();
    void txMetersChanged();
    void dspChanged();
    void meterSelectionChanged();
    void tuneStateChanged();
    void atuChanged();
    void txAudioChanged();
    void stepChanged();
    void ritXitChanged();
    void squelchChanged();
    void antennaChanged();
    void slicesChanged();
    void cwChanged();
    void cwDecoderChanged();
    void displayLevelsChanged();
    void lockedChanged();
    void profilesChanged();
    void txFilterChanged();
    void eqChanged();
    void cwMemoriesChanged();
    void tuneBlocked();
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
    void defaultMicToDevice();
    QString meterChoice(const char* key, const char* fallback) const;
    int displayInt(const char* key, int fallback) const;
    void setDisplayInt(const char* key, int value);
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
    void wireSmartLink();
    void wireCw();
    void updateCwDecoder();
    void startWanConnect();
    void setSmartLinkState(const QString& state);

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
    TxController::Input m_cwKeyInput;
    TxController::Input m_cwxInput;
    bool m_cwKeyHeld{false};
    std::unique_ptr<CwRxModel> m_cwDecoder;
    std::unique_ptr<DecoderAudioModel> m_cwAudio;
    QString m_cwDecodedText;
    QString m_cwDecoderStats;
    double m_atuTunedFreqMhz{-1.0};
    double m_compressionDb{0.0};
    double m_alcDbfs{-100.0};
    bool m_alcAvailable{false};
    double m_paTempC{0.0};
    double m_supplyVolts{0.0};
    bool m_pttHeld{false};
    bool m_micPermissionRequested{false};
    bool m_connected{false};
    bool m_micDefaulted{false};
    bool m_displayActive{true};
    int m_fftFramesSeen{0};
    int m_wfRowsSeen{0};
    quint32 m_otherWfStreamSeen{0};
    bool m_micDefaultEnabled{true};
    RadioCapabilities m_caps;
    SmartLinkClient m_smartLink;
    WanConnection m_wan;
    QList<WanRadioInfo> m_wanRadios;
    WanRadioInfo m_pendingWan;
    bool m_wanActive{false};
    bool m_wanUserDisconnect{false};
    QTimer m_wanReconnectTimer;
    QString m_smartLinkState{QStringLiteral("signedOut")};
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
    QTimer m_panMoveTimer;
    double m_pendingPanCenterMhz{0.0};
    QTimer m_panPreviewEndTimer;
    double m_panPreviewMhz{0.0};
    qint64 m_panPreviewUntilMs{0};
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
