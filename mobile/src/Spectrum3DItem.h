#pragma once

#include <QPointer>
#include <QQuickRhiItem>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <array>

namespace AetherSDR::mobile {

class MobileEngine;

// GPU 3D stacked trace (the desktop's 3DSS look, simplified for mobile):
// trace frames go into a ring texture, one row at a time, and a vertex
// shader raises a perspective curtain per row. Retune and zoom reproject old
// rows by their own frequency frame instead of clearing them.
class Spectrum3DItem : public QQuickRhiItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AetherSDR::mobile::MobileEngine* engine READ engine WRITE setEngine NOTIFY engineChanged)
    // dBm at the baseline, and the dB from there to full ridge height.
    Q_PROPERTY(double floorDbm READ floorDbm WRITE setFloorDbm NOTIFY levelsChanged)
    Q_PROPERTY(double rangeDb READ rangeDb WRITE setRangeDb NOTIFY levelsChanged)
    // 0..100: colour gamma, as the desktop's 3D Gain (50 = linear).
    Q_PROPERTY(int colorGain READ colorGain WRITE setColorGain NOTIFY levelsChanged)

public:
    static constexpr int kRows = 96;       // must match rowFrames[] in the shaders
    static constexpr int kTexCols = 512;
    static constexpr int kMeshCols = 384;
    static constexpr qint64 kRowIntervalMs = 66;  // ~15 rows per second

    explicit Spectrum3DItem(QQuickItem* parent = nullptr);

    MobileEngine* engine() const { return m_engine; }
    void setEngine(MobileEngine* engine);
    double floorDbm() const { return m_floorDbm; }
    void setFloorDbm(double v);
    double rangeDb() const { return m_rangeDb; }
    void setRangeDb(double v);
    int colorGain() const { return m_colorGain; }
    void setColorGain(int v);

    // Filled by the GUI thread, consumed in synchronize().
    struct PendingRow {
        QVector<float> dbm;  // kTexCols
        double centerMhz{0.0};
        double bandwidthMhz{0.0};
    };

    QQuickRhiItemRenderer* createRenderer() override;

signals:
    void engineChanged();
    void levelsChanged();

private:
    friend class Spectrum3DRenderer;
    void onSpectrum(const QVector<float>& binsDbm);

    QPointer<MobileEngine> m_engine;
    double m_floorDbm{-120.0};
    double m_rangeDb{60.0};
    int m_colorGain{50};
    qint64 m_nextRowMs{0};
    QVector<PendingRow> m_pending;
    bool m_clear{false};
};

} // namespace AetherSDR::mobile
