#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace AetherSDR::mobile {

class MobileEngine;

// Panadapter trace (top) and scrolling waterfall (bottom) for the active pan.
// The waterfall auto-ranges each row because Flex rows are radio intensities
// while seam backends deliver dBm.
class SpectrumItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AetherSDR::mobile::MobileEngine* engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(double spectrumFraction READ spectrumFraction WRITE setSpectrumFraction NOTIFY spectrumFractionChanged)

public:
    explicit SpectrumItem(QQuickItem* parent = nullptr);

    MobileEngine* engine() const { return m_engine; }
    void setEngine(MobileEngine* engine);
    double spectrumFraction() const { return m_spectrumFraction; }
    void setSpectrumFraction(double fraction);

    void paint(QPainter* painter) override;

signals:
    void engineChanged();
    void spectrumFractionChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void onSpectrum(const QVector<float>& binsDbm);
    void onWaterfallRow(const QVector<float>& bins);
    void reportPixels();
    QRgb colourFor(float normalised) const;

    QPointer<MobileEngine> m_engine;
    QVector<float> m_spectrum;
    QImage m_waterfall;
    float m_floor{0.0f};
    float m_span{0.0f};
    bool m_rangeSeeded{false};
    double m_spectrumFraction{0.4};
};

} // namespace AetherSDR::mobile
