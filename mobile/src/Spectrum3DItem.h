#pragma once

#include <QElapsedTimer>
#include <QPointer>
#include <QQuickRhiItem>
#include <QTimer>
#include <QVector>
#include <QtCore/qfloat16.h>
#include <QtQml/qqmlregistration.h>

#include <array>

namespace AetherSDR::mobile {

class MobileEngine;

// GPU 3D stacked trace, the desktop's 3DSS (DssRenderer + dss_mesh shaders on
// main) brought to mobile: a ring of smoothed trace rows, one per waterfall
// line, each with its own frequency frame, in an RGBA16F texture (FFT dBm and
// coverage, the wider waterfall tile calibrated to dBm and its coverage). A
// static perspective mesh samples it; the surface glides between rows, the
// ridges are anti-aliased ribbons, the near rows widen over the side wedges
// from the tile, and the slices' passbands are darkened into the surface.
class Spectrum3DItem : public QQuickRhiItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AetherSDR::mobile::MobileEngine* engine READ engine WRITE setEngine NOTIFY engineChanged)
    // dBm at the baseline (the 3D zero), and the dB from there to full height.
    Q_PROPERTY(double floorDbm READ floorDbm WRITE setFloorDbm NOTIFY levelsChanged)
    Q_PROPERTY(double rangeDb READ rangeDb WRITE setRangeDb NOTIFY levelsChanged)
    // 0..100: the desktop's 3D Gain, the colour gamma (50 = linear).
    Q_PROPERTY(int colorGain READ colorGain WRITE setColorGain NOTIFY levelsChanged)

public:
    // The desktop's geometry and ring (DssRenderer.h); 512 columns instead of
    // 768 to spare a phone's GPU, the mesh sized so a widened row still
    // samples every column.
    static constexpr int kVisibleRows = 96;
    static constexpr int kTransitionRows = 8;
    static constexpr int kRows = kVisibleRows + kTransitionRows;  // rowFrames[104] in the shaders
    static constexpr int kCols = 512;
    static constexpr float kBackWidthFrac = 0.60f;
    static constexpr float kDepthSpanFrac = 0.58f;
    static constexpr float kFrontMaxRidgeFrac = 0.46f;
    static constexpr float kHaze = 0.16f;
    static constexpr float kColorSpanDb = 45.0f;
    static constexpr float kZCurve = 0.70f;
    static constexpr float kMaxRowSpanFactor = 1.0f / kBackWidthFrac;
    static constexpr int kMeshCols = static_cast<int>(kCols * kMaxRowSpanFactor) + 1;
    static_assert(static_cast<float>(kMeshCols) >= kCols * kMaxRowSpanFactor,
                  "the mesh must keep at least one column per texel at the widest row");
    static constexpr int kShadowSlots = 8;

    explicit Spectrum3DItem(QQuickItem* parent = nullptr);

    MobileEngine* engine() const { return m_engine; }
    void setEngine(MobileEngine* engine);
    double floorDbm() const { return m_floorDbm; }
    void setFloorDbm(double v);
    double rangeDb() const { return m_rangeDb; }
    void setRangeDb(double v);
    int colorGain() const { return m_colorGain; }
    void setColorGain(int v);

    QQuickRhiItemRenderer* createRenderer() override;

signals:
    void engineChanged();
    void levelsChanged();

private:
    friend class Spectrum3DRenderer;
    using Row = std::array<float, kCols>;
    using Coverage = std::array<quint8, kCols>;

    void onSpectrum(const QVector<float>& binsDbm);
    void onWaterfallRow(const QVector<float>& bins, double lowMhz, double highMhz);
    // One new history row from the latest trace (and the tile, when it came
    // with a waterfall line).
    void pushRow(const QVector<float>& tile, double tileLowMhz, double tileHighMhz);
    void clearHistory();
    void tick();
    float scrollProgressRows() const;

    QPointer<MobileEngine> m_engine;
    double m_floorDbm{-120.0};
    double m_rangeDb{90.0};
    int m_colorGain{70};

    // Latest trace frame and the pan's frame when it came.
    QVector<float> m_fft;
    double m_fftCenterMhz{0.0};
    double m_fftBandwidthMhz{0.0};

    // The ring (newest at m_head), as DssRenderer.
    std::array<Row, kRows> m_rows{};
    std::array<Row, kRows> m_supplemental{};
    std::array<Coverage, kRows> m_supplementalCoverage{};
    std::array<double, kRows> m_rowCenterMhz{};
    std::array<double, kRows> m_rowBandwidthMhz{};
    std::array<double, kRows> m_supplementalCenterMhz{};
    std::array<double, kRows> m_supplementalBandwidthMhz{};
    int m_head{0};
    int m_count{0};
    quint64 m_rowGeneration{0};
    // Smoothing state (median of three raw rows, then the temporal blend).
    Row m_rawPrev1{};
    Row m_rawPrev2{};
    int m_rawHistCount{0};
    Row m_supplementalPrev1{};
    Row m_supplementalPrev2{};
    int m_supplementalHistCount{0};

    // Rows arrive at the waterfall's line rate; between them the surface
    // moves toward the back (scrollProgressRows).
    QElapsedTimer m_rowClock;
    float m_msPerRow{100.0f};
    qint64 m_lastWaterfallRowMs{0};
    qint64 m_nextFftRowMs{0};
    float m_rowSpanFactor{1.0f};
    QTimer m_frameTimer;
};

} // namespace AetherSDR::mobile
