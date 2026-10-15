#include "SpectrumItem.h"

#include <QFontMetricsF>

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
        connect(m_engine, &MobileEngine::displayLevelsChanged, this, [this] { update(); });
        connect(m_engine, &MobileEngine::bandPlanChanged, this, [this] { update(); });
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
    // Non-finite bins (empty frames) would make the scale infinite.
    m_spectrum = binsDbm;
    for (float& v : m_spectrum) {
        if (!std::isfinite(v)) {
            v = -200.0f;
        }
        v = std::clamp(v, -200.0f, 50.0f);
    }
    if (!m_spectrum.isEmpty()) {
        QVector<float> sorted = m_spectrum;
        std::sort(sorted.begin(), sorted.end());
        // Auto follows the band without the RF gain, so a preamp or
        // attenuator step shows on the trace (as on a fixed SmartSDR scale)
        // instead of being levelled away.
        const float gain = m_engine ? static_cast<float>(m_engine->rfGain()) : 0.0f;
        const float noise = sorted.at(sorted.size() / 2) - gain;
        const float peak = sorted.last() - gain;
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
        // Own transmission in the waterfall: the radio pauses waterfall rows
        // during TX, so draw them from the trace on the trace's scale.
        if (m_engine && m_engine->radioTransmitting() && !m_waterfall.isNull()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now >= m_nextTxRowMs) {
                m_nextTxRowMs = now + kTxRowIntervalMs;
                followView();
                pushRow(m_spectrum, m_viewLowMhz, m_viewHighMhz, m_traceBottom + kTraceBelowNoiseDb,
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
    // Tracked without the RF gain, so a gain step brightens or darkens the
    // picture as on the desktop instead of being levelled away.
    QVector<float> sorted = bins;
    std::sort(sorted.begin(), sorted.end());
    const float gain = m_engine ? static_cast<float>(m_engine->rfGain()) : 0.0f;
    const float floor = sorted.at(sorted.size() / 2) - gain;
    const float top = sorted.at(std::min<qsizetype>(sorted.size() - 1, sorted.size() * 995 / 1000)) - gain;
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

void SpectrumItem::drawBandPlan(QPainter* painter, qreal w, qreal specH) const
{
    // The band plan as a strip along the bottom of the spectrum, as the
    // desktop draws it: each segment in its colour, mixed into the dark
    // background, with its label where it fits.
    if (!m_engine || m_engine->panBandwidthMhz() <= 0.0 || w <= 0) {
        return;
    }
    const QVector<BandPlanManager::Segment> segments = m_engine->bandPlanSegments();
    if (segments.isEmpty()) {
        return;
    }
    const double span = m_engine->panBandwidthMhz();
    const double low = m_engine->panCenterMhz() - span / 2.0;
    const double high = low + span;
    const qreal stripH = std::clamp<qreal>(height() * 0.025, 14.0, 20.0);
    const qreal y = specH - stripH;
    QFont font = painter->font();
    font.setPixelSize(static_cast<int>(stripH * 0.62));
    font.setBold(true);
    painter->setFont(font);
    const QFontMetricsF fm(font);
    const QColor bg(0x0a, 0x0a, 0x14);
    for (const BandPlanManager::Segment& seg : segments) {
        if (seg.highMhz <= low || seg.lowMhz >= high) {
            continue;
        }
        const qreal x1 = (std::max(seg.lowMhz, low) - low) / span * w;
        const qreal x2 = (std::min(seg.highMhz, high) - low) / span * w;
        if (x2 - x1 < 1.0) {
            continue;
        }
        // License contrast as the desktop: wider access brighter.
        float blend = 0.5f;
        if (seg.license == QLatin1String("E")) blend = 0.2f;
        else if (seg.license == QLatin1String("E,G")) blend = 0.4f;
        else if (seg.license.contains(QLatin1Char('T'))) blend = 0.6f;
        const QColor fill(static_cast<int>(seg.color.red() * blend + bg.red() * (1 - blend)),
                          static_cast<int>(seg.color.green() * blend + bg.green() * (1 - blend)),
                          static_cast<int>(seg.color.blue() * blend + bg.blue() * (1 - blend)));
        painter->fillRect(QRectF(x1, y, x2 - x1, stripH), fill);
        painter->setPen(QColor(10, 10, 20, 200));
        painter->drawLine(QPointF(x1, y), QPointF(x1, y + stripH));
        if (fm.horizontalAdvance(seg.label) + 6 < x2 - x1) {
            painter->setPen(Qt::white);
            painter->drawText(QRectF(x1, y, x2 - x1, stripH), Qt::AlignCenter, seg.label);
        }
    }
}

void SpectrumItem::drawFrequencyAxis(QPainter* painter, qreal w, qreal specH) const
{
    // Frequency scale at the top of the waterfall, as AetherSDR's: ticks on
    // a round step (1/2/5 x 10^n kHz) giving labels about 110 px apart.
    if (!m_engine || m_engine->panBandwidthMhz() <= 0.0 || w <= 0) {
        return;
    }
    const double span = m_engine->panBandwidthMhz();
    const double low = m_engine->panCenterMhz() - span / 2.0;
    const double wantedMhz = span * 110.0 / w;
    double step = 1e-6;  // 1 Hz
    for (const double decade : {1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1, 1.0}) {
        bool found = false;
        for (const double m : {1.0, 2.0, 5.0}) {
            if (decade * m >= wantedMhz) {
                step = decade * m;
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    const qreal axisH = std::clamp(height() * 0.03, 16.0, 22.0);
    painter->fillRect(QRectF(0, specH, w, axisH), QColor(11, 15, 20, 215));
    QFont font = painter->font();
    font.setPixelSize(static_cast<int>(axisH * 0.62));
    painter->setFont(font);
    // Decimals: enough to tell neighbouring labels apart.
    const int decimals = step >= 0.1 ? 1 : step >= 0.01 ? 2 : step >= 0.001 ? 3 : step >= 0.0001 ? 4 : 5;
    const double first = std::ceil(low / step) * step;
    for (double f = first; f <= low + span; f += step) {
        const qreal x = (f - low) / span * w;
        painter->setPen(QColor(230, 237, 243, 150));
        painter->drawLine(QPointF(x, specH), QPointF(x, specH + axisH * 0.3));
        const QString label = QString::number(f, 'f', decimals);
        const QRectF box(x - 50, specH + axisH * 0.25, 100, axisH * 0.75);
        if (box.left() < 0 || box.right() > w) {
            // Only labels that fit whole; keep the tick.
            const qreal textW = painter->fontMetrics().horizontalAdvance(label);
            if (x - textW / 2 < 2 || x + textW / 2 > w - 2) {
                continue;
            }
        }
        painter->setPen(QColor(200, 210, 220));
        painter->drawText(box, Qt::AlignHCenter | Qt::AlignVCenter, label);
    }
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

    if (!m_waterfall.isNull()) {
        painter->drawImage(QPointF(0, specH), m_waterfall);
    }
    drawFrequencyAxis(painter, w, specH);
    drawBandPlan(painter, w, specH);
    // In 3D the spectrum area belongs to Spectrum3DItem (GPU), as on the
    // desktop: the stacked trace replaces the trace, the waterfall stays.
    if (m_engine && m_engine->spectrum3D()) {
        return;
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
