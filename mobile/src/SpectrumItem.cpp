#include "SpectrumItem.h"

#include "MobileEngine.h"

#include <QDateTime>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace AetherSDR::mobile {

namespace {

// Trace scale: bottom a little below the noise, top above the strongest
// signals, at least kTraceMinSpanDb tall, eased so the trace does not jump.
constexpr float kTraceBelowNoiseDb = 8.0f;
constexpr float kTraceAboveTopDb = 10.0f;
constexpr float kTraceMinSpanDb = 50.0f;
constexpr float kTraceSmoothing = 0.1f;
constexpr float kRangeSmoothing = 0.05f;
// Minimum colour span, in the row's units (dB-like for both Flex intensity
// and dBm), so a noise-only band stays dark instead of being stretched.
constexpr float kMinimumSpan = 30.0f;
// Row interval for the TX waterfall drawn from trace frames.
constexpr qint64 kTxRowIntervalMs = 100;
// 3D: one new ridge per interval (each redraws the CPU image).
constexpr qint64 kDssRowIntervalMs = 100;
constexpr float kDssZCurve = 0.70f;  // as the desktop's default

} // namespace

SpectrumItem::SpectrumItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setFillColor(QColor(QStringLiteral("#0b0f14")));
}

void SpectrumItem::setEngine(MobileEngine* engine)
{
    if (engine == m_engine) {
        return;
    }
    if (m_engine) {
        disconnect(m_engine, nullptr, this, nullptr);
    }
    m_engine = engine;
    if (m_engine) {
        connect(m_engine, &MobileEngine::spectrumFrame, this, &SpectrumItem::onSpectrum);
        connect(m_engine, &MobileEngine::waterfallRow, this, &SpectrumItem::onWaterfallRow);
        connect(m_engine, &MobileEngine::panGeometryChanged, this, &SpectrumItem::followView);
        connect(m_engine, &MobileEngine::displayLevelsChanged, this, [this] {
            if (!m_engine->spectrum3D() && m_dss.hasData()) {
                m_dss.clear();
            }
            m_dss.invalidate();
            update();
        });
        reportPixels();
    }
    emit engineChanged();
}

void SpectrumItem::setSpectrumFraction(double fraction)
{
    fraction = std::clamp(fraction, 0.1, 0.9);
    if (qFuzzyCompare(fraction, m_spectrumFraction)) {
        return;
    }
    m_spectrumFraction = fraction;
    reportPixels();
    update();
    emit spectrumFractionChanged();
}

void SpectrumItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    reportPixels();
}

void SpectrumItem::reportPixels()
{
    const int w = static_cast<int>(width());
    const int specH = static_cast<int>(height() * m_spectrumFraction);
    const int wfH = static_cast<int>(height()) - specH;
    if (w <= 0 || specH <= 0 || wfH <= 0) {
        return;
    }
    if (m_waterfall.width() != w || m_waterfall.height() != wfH) {
        QImage resized(w, wfH, QImage::Format_RGB32);
        resized.fill(Qt::black);
        m_waterfall = resized;
        // Rows at the new width come at a new scale; re-learn the range.
        m_rangeSeeded = false;
        m_viewLowMhz = 0.0;
        m_viewHighMhz = 0.0;
    }
    if (m_engine) {
        m_engine->setPanPixels(w, specH);
    }
}

void SpectrumItem::onSpectrum(const QVector<float>& binsDbm)
{
    m_spectrum = binsDbm;
    if (!binsDbm.isEmpty()) {
        QVector<float> sorted = binsDbm;
        std::sort(sorted.begin(), sorted.end());
        const float noise = sorted.at(sorted.size() / 2);
        const float peak = sorted.last();
        const float bottom = noise - kTraceBelowNoiseDb;
        const float top = std::max(bottom + kTraceMinSpanDb, peak + kTraceAboveTopDb);
        if (m_engine && !m_engine->traceAutoRange()) {
            // Manual: reference level at the top, range below it.
            m_traceTop = static_cast<float>(m_engine->traceRefDbm());
            m_traceBottom = m_traceTop - static_cast<float>(m_engine->traceRangeDb());
            m_traceSeeded = true;
        } else if (!m_traceSeeded) {
            m_traceBottom = bottom;
            m_traceTop = top;
            m_traceSeeded = true;
        } else {
            m_traceBottom += (bottom - m_traceBottom) * kTraceSmoothing;
            m_traceTop += (top - m_traceTop) * kTraceSmoothing;
        }
        // Tell the scale strip when the scale moved by at least 1 dB.
        if (std::abs(m_traceTop - m_reportedTop) >= 1.0f
            || std::abs(m_traceBottom - m_reportedBottom) >= 1.0f) {
            m_reportedTop = m_traceTop;
            m_reportedBottom = m_traceBottom;
            emit traceScaleChanged();
        }
        if (m_engine && m_engine->spectrum3D()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now >= m_nextDssRowMs) {
                m_nextDssRowMs = now + kDssRowIntervalMs;
                followView();
                m_dssCenterMhz = m_engine->panCenterMhz();
                m_dssBandwidthMhz = m_engine->panBandwidthMhz();
                m_dss.pushRow(binsDbm, m_dssCenterMhz, m_dssBandwidthMhz);
            }
            update();
            return;
        }
        // Own transmission in the waterfall: the radio pauses waterfall rows
        // during TX, so draw them from the trace on the trace's scale.
        if (m_engine && m_engine->radioTransmitting() && !m_waterfall.isNull()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now >= m_nextTxRowMs) {
                m_nextTxRowMs = now + kTxRowIntervalMs;
                followView();
                pushRow(binsDbm, m_viewLowMhz, m_viewHighMhz, m_traceBottom + kTraceBelowNoiseDb,
                        std::max(kMinimumSpan, m_traceTop - m_traceBottom - kTraceBelowNoiseDb));
                return;
            }
        }
    }
    update();
}

void SpectrumItem::followView()
{
    if (!m_engine || m_waterfall.isNull() || m_engine->panBandwidthMhz() <= 0.0) {
        return;
    }
    const double low = m_engine->panCenterMhz() - m_engine->panBandwidthMhz() / 2.0;
    const double high = m_engine->panCenterMhz() + m_engine->panBandwidthMhz() / 2.0;
    const bool hadView = m_viewHighMhz > m_viewLowMhz;
    if (hadView && qFuzzyCompare(low, m_viewLowMhz) && qFuzzyCompare(high, m_viewHighMhz)) {
        return;
    }
    if (hadView) {
        // Old pixel x showed m_viewLow + x/w * oldSpan; place it at the same
        // frequency in the new view. Uncovered parts turn black.
        const double w = m_waterfall.width();
        const double newSpan = high - low;
        const QRectF target((m_viewLowMhz - low) / newSpan * w, 0.0,
                            (m_viewHighMhz - m_viewLowMhz) / newSpan * w, m_waterfall.height());
        QImage moved(m_waterfall.size(), QImage::Format_RGB32);
        moved.fill(Qt::black);
        QPainter painter(&moved);
        painter.drawImage(target, m_waterfall);
        painter.end();
        m_waterfall = moved;
        update();
    }
    m_viewLowMhz = low;
    m_viewHighMhz = high;
    if (m_dss.hasData() && m_dssBandwidthMhz > 0.0) {
        const double center = (low + high) / 2.0;
        const double bandwidth = high - low;
        m_dss.reprojectFrequencyFrame(m_dssCenterMhz, m_dssBandwidthMhz, center, bandwidth,
                                      m_traceBottom + kTraceBelowNoiseDb);
        m_dssCenterMhz = center;
        m_dssBandwidthMhz = bandwidth;
    }
}

void SpectrumItem::onWaterfallRow(const QVector<float>& bins, double lowMhz, double highMhz)
{
    // While transmitting the TX rows come from the trace (onSpectrum).
    if (bins.isEmpty() || m_waterfall.isNull() || (m_engine && m_engine->radioTransmitting())) {
        return;
    }
    followView();
    // Black at the median (the band's noise), full scale near the 99.5th
    // percentile; smoothed across rows so the picture does not pump with
    // every strong signal.
    QVector<float> sorted = bins;
    std::sort(sorted.begin(), sorted.end());
    const float floor = sorted.at(sorted.size() / 2);
    const float top = sorted.at(std::min<qsizetype>(sorted.size() - 1, sorted.size() * 995 / 1000));
    const float span = std::max(kMinimumSpan, (top - floor) * 1.2f);
    const bool autoLevel = !m_engine || m_engine->wfAutoLevel();
    if (!m_rangeSeeded) {
        m_floor = floor;
        m_span = span;
        m_rangeSeeded = true;
    } else if (autoLevel) {
        m_floor += (floor - m_floor) * kRangeSmoothing;
        m_span += (span - m_span) * kRangeSmoothing;
    }
    // Black level shifts the black point (50 = the tracked noise floor, as
    // the desktop's SW auto black offset); colour gain stretches (100) or
    // widens (0) the colour range by up to 2x.
    float wfFloor = m_floor;
    float wfSpan = m_span;
    if (m_engine) {
        wfFloor += (50 - m_engine->wfBlackLevel()) * 0.4f;
        wfSpan *= std::pow(2.0f, (50 - m_engine->wfColorGain()) / 50.0f);
    }
    pushRow(bins, lowMhz, highMhz, wfFloor, wfSpan);
}

void SpectrumItem::pushRow(const QVector<float>& bins, double lowMhz, double highMhz, float floor, float span)
{
    if (bins.isEmpty() || m_waterfall.isNull()) {
        return;
    }
    const int w = m_waterfall.width();
    const int h = m_waterfall.height();
    const qsizetype rowBytes = m_waterfall.bytesPerLine();
    uchar* bits = m_waterfall.bits();
    std::memmove(bits + rowBytes, bits, rowBytes * (h - 1));
    auto* row = reinterpret_cast<QRgb*>(bits);
    const qsizetype n = bins.size();
    // Place the row by frequency: it covers lowMhz..highMhz, which need not
    // be the pan's range (Flex rows lag a zoom or retune). Without a range,
    // stretch it across the view.
    const bool placed = highMhz > lowMhz && m_viewHighMhz > m_viewLowMhz;
    const double viewSpan = m_viewHighMhz - m_viewLowMhz;
    const double rowSpan = highMhz - lowMhz;
    for (int x = 0; x < w; ++x) {
        qsizetype i = 0;
        if (placed) {
            const double f = m_viewLowMhz + (x + 0.5) / w * viewSpan;
            const double pos = (f - lowMhz) / rowSpan * n;
            if (pos < 0.0 || pos >= n) {
                row[x] = qRgb(0, 0, 0);
                continue;
            }
            i = static_cast<qsizetype>(pos);
        } else {
            i = std::min<qsizetype>(n - 1, static_cast<qsizetype>(x) * n / w);
        }
        row[x] = colourFor((bins.at(i) - floor) / span);
    }
    update();
}

QRgb SpectrumItem::colourFor(float v) const
{
    v = std::clamp(v, 0.0f, 1.0f);
    // black → blue → cyan → yellow → red → white
    struct Stop { float at; int r; int g; int b; };
    static constexpr Stop kStops[] = {
        {0.00f, 0, 0, 0}, {0.12f, 0, 0, 70}, {0.30f, 0, 40, 200}, {0.50f, 0, 170, 220},
        {0.65f, 240, 230, 0}, {0.85f, 230, 30, 0}, {1.00f, 255, 255, 255},
    };
    for (size_t i = 1; i < std::size(kStops); ++i) {
        if (v <= kStops[i].at) {
            const Stop& a = kStops[i - 1];
            const Stop& b = kStops[i];
            const float t = (v - a.at) / (b.at - a.at);
            return qRgb(static_cast<int>(a.r + (b.r - a.r) * t),
                        static_cast<int>(a.g + (b.g - a.g) * t),
                        static_cast<int>(a.b + (b.b - a.b) * t));
        }
    }
    return qRgb(255, 255, 255);
}

void SpectrumItem::paint(QPainter* painter)
{
    const qreal w = width();
    const qreal specH = height() * m_spectrumFraction;

    if (m_engine && m_engine->spectrum3D()) {
        // Floor at the noise, height scale from the trace range; colour over
        // at most 45 dB, gamma-shaped by colour gain (the desktop's 3D Gain).
        const QSize px(static_cast<int>(w), static_cast<int>(height() - specH));
        if (m_dss.hasData() && !px.isEmpty()) {
            const float floorDbm = std::round((m_traceBottom + kTraceBelowNoiseDb) * 2.0f) / 2.0f;
            const float rangeDb = std::round(std::clamp(m_traceTop - floorDbm, 20.0f, 100.0f) * 2.0f) / 2.0f;
            const float colorRange = std::min(rangeDb, DssRenderer::kColorSpanDb);
            const int gain = m_engine->wfColorGain();
            const float gamma = std::pow(4.0f, (50.0f - gain) / 50.0f);
            auto palette = [this, floorDbm, colorRange, gamma](float dbm) {
                return colourFor(std::pow(std::clamp((dbm - floorDbm) / colorRange, 0.0f, 1.0f), gamma));
            };
            painter->drawImage(QPointF(0, specH),
                               m_dss.image(px, 0, floorDbm, rangeDb, kDssZCurve, palette,
                                           static_cast<quint64>(gain), QColor(QStringLiteral("#0b0f14"))));
        }
    } else if (!m_waterfall.isNull()) {
        painter->drawImage(QPointF(0, specH), m_waterfall);
    }

    painter->setPen(QColor(255, 255, 255, 25));
    for (int i = 1; i < 9; ++i) {
        const qreal y = specH * i / 9.0;
        painter->drawLine(QPointF(0, y), QPointF(w, y));
    }

    if (m_spectrum.size() > 1) {
        QPainterPath path;
        const qsizetype n = m_spectrum.size();
        for (qsizetype i = 0; i < n; ++i) {
            const qreal x = w * i / (n - 1);
            const float norm = (m_spectrum.at(i) - m_traceBottom) / (m_traceTop - m_traceBottom);
            const qreal y = specH * (1.0 - std::clamp(norm, 0.0f, 1.0f));
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        QPainterPath fill = path;
        fill.lineTo(w, specH);
        fill.lineTo(0, specH);
        fill.closeSubpath();
        painter->fillPath(fill, QColor(63, 182, 255, 50));
        painter->setPen(QPen(QColor(63, 182, 255), 1.5));
        painter->drawPath(path);
    }

    // Centre line marks the pan centre; the slice marker is drawn in QML.
    painter->setPen(QColor(255, 255, 255, 60));
    painter->drawLine(QPointF(0, specH), QPointF(w, specH));
}

} // namespace AetherSDR::mobile
