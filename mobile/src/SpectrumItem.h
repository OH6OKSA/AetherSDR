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
    // The trace's current scale (dBm at the top and bottom of the spectrum
    // area), for the dBm scale strip and its drag.
    Q_PROPERTY(double traceTopDbm READ traceTopDbm NOTIFY traceScaleChanged)
    Q_PROPERTY(double traceBottomDbm READ traceBottomDbm NOTIFY traceScaleChanged)
    // Height of the band plan strip at the bottom of the spectrum (0 when no
    // plan is shown): the 3D view ends above it.
    Q_PROPERTY(double bandPlanHeight READ bandPlanHeight NOTIFY bandPlanHeightChanged)

public:
    explicit SpectrumItem(QQuickItem* parent = nullptr);

    MobileEngine* engine() const { return m_engine; }
    void setEngine(MobileEngine* engine);
    double spectrumFraction() const { return m_spectrumFraction; }
    double traceTopDbm() const { return m_traceTop; }
    double traceBottomDbm() const { return m_traceBottom; }
    double bandPlanHeight() const;
    void setSpectrumFraction(double fraction);

    void paint(QPainter* painter) override;

signals:
    void engineChanged();
    void spectrumFractionChanged();
    void traceScaleChanged();
    void bandPlanHeightChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void onSpectrum(const QVector<float>& binsDbm);
    void onWaterfallRow(const QVector<float>& bins, double lowMhz, double highMhz);
    // Moves and stretches the waterfall history so it stays on the same
    // frequencies after the pan is retuned or zoomed.
    void followView();
    void reportPixels();
    // Scrolls the waterfall by one row coloured from bins over
    // lowMhz..highMhz, with black at floor and full scale at floor + span.
    void pushRow(const QVector<float>& bins, double lowMhz, double highMhz, float floor, float span);
    QRgb colourFor(float normalised) const;
    void drawFrequencyAxis(QPainter* painter, qreal w, qreal specH) const;
    void drawBandPlan(QPainter* painter, qreal w, qreal specH) const;

    QPointer<MobileEngine> m_engine;
    QVector<float> m_spectrum;
    QImage m_waterfall;
    float m_floor{0.0f};
    float m_span{0.0f};
    bool m_rangeSeeded{false};
    double m_viewLowMhz{0.0};
    double m_viewHighMhz{0.0};
    float m_traceBottom{-130.0f};
    float m_traceTop{-40.0f};
    bool m_traceSeeded{false};
    float m_reportedTop{0.0f};
    float m_reportedBottom{0.0f};
    double m_spectrumFraction{0.4};
    // The radio sends no waterfall rows while transmitting; the trace's
    // frames fill in, paced like the RX waterfall (as SpectrumWidget does).
    qint64 m_nextTxRowMs{0};
};

} // namespace AetherSDR::mobile
