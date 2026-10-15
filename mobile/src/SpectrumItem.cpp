#include "SpectrumItem.h"

#include <QFontMetricsF>

#include "MobileEngine.h"
#include "SpectrumPalette.h"
#include "gui/NoiseFloorEstimator.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace AetherSDR::mobile {

Q_LOGGING_CATEGORY(lcSpectrum, "aether.mobile.spectrum")

namespace {

// How often the spectrum's levels go to the log.
constexpr qint64 kLevelLogIntervalMs = 30000;

// Auto trace scale: the noise floor this far up from the bottom (the
// desktop's default noise floor position, 75 % down from the top).
constexpr float kNoiseTargetFraction = 0.75f;
// Trace averaging, the desktop's SMOOTH_ALPHA (share of the new frame).
constexpr float kTraceAverage = 0.35f;
// Row interval for the TX waterfall drawn from trace frames.
constexpr qint64 kTxRowIntervalMs = 100;
// Waterfall rows dropped after a transmission while the AGC settles.
constexpr qint64 kPostTxSettleMs = 400;

} // namespace

SpectrumItem::SpectrumItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setFillColor(QColor(QStringLiteral("#0b0f14")));
    for (int i = 0; i < 256; ++i) {
        m_palette[i] = spectrumPaletteAt(i / 255.0f);
    }
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
        connect(m_engine, &MobileEngine::bandPlanChanged, this, [this] {
            emit bandPlanHeightChanged();
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
    if (newGeometry.height() != oldGeometry.height()) {
        emit bandPlanHeightChanged();
    }
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
    QVector<float> raw = binsDbm;
    for (float& v : raw) {
        if (!std::isfinite(v)) {
            v = -200.0f;
        }
        v = std::clamp(v, -200.0f, 50.0f);
    }
    if (raw.isEmpty()) {
        update();
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool transmitting = m_engine && m_engine->radioTransmitting();
    // A retune or zoom moves every bin to a new frequency: start the
    // averaging and the noise floor afresh, as the desktop does.
    const double center = m_engine ? m_engine->panCenterMhz() : 0.0;
    const double bandwidth = m_engine ? m_engine->panBandwidthMhz() : 0.0;
    const bool frameChanged = !qFuzzyCompare(1.0 + center, 1.0 + m_frameCenterMhz)
        || !qFuzzyCompare(1.0 + bandwidth, 1.0 + m_frameBandwidthMhz);
    if (frameChanged) {
        m_frameCenterMhz = center;
        m_frameBandwidthMhz = bandwidth;
        m_noise.reset();
    }
    // Trace averaging (the desktop's SMOOTH_ALPHA): steadies the trace,
    // restarted on a new frame, a new width and after a transmission.
    if (m_spectrum.size() != raw.size() || frameChanged || (m_wasTransmitting && !transmitting)) {
        m_spectrum = raw;
    } else {
        for (qsizetype i = 0; i < raw.size(); ++i) {
            m_spectrum[i] = kTraceAverage * raw.at(i) + (1.0f - kTraceAverage) * m_spectrum.at(i);
        }
    }
    if (m_wasTransmitting && !transmitting) {
        m_txEndMs = nowMs;
    }
    m_wasTransmitting = transmitting;

    // Noise floor as the desktop tracks it. Without the RF gain, so a
    // preamp or attenuator step shows on the trace, waterfall and 3D (as
    // on a fixed SmartSDR scale) instead of being levelled away.
    const float gain = m_engine ? static_cast<float>(m_engine->rfGain()) : 0.0f;
    if (!transmitting) {
        m_noise.observe(estimateNoiseFloorDbm(std::span<const float>(raw.constData(), raw.size())) - gain, nowMs);
    }
    const float noise = m_noise.valid() ? m_noise.baseline() : m_autoNoise;
    // The 3D floor's anchor (SpectrumWidget::dssFloorDbm): follows the
    // baseline slowly, jumps when it is far off, in half-dB steps.
    if (!m_anchorSeeded || std::abs(noise - m_anchor) > 18.0f) {
        m_anchor = noise;
        m_anchorSeeded = true;
    } else {
        m_anchor += (noise - m_anchor) * 0.04f;
    }
    m_autoNoise = std::round(m_anchor * 2.0f) / 2.0f;

    // Trace scale. Auto as the desktop: the span stays (Range), the noise
    // floor sits a quarter of it above the bottom, and the level eases
    // there (a changing span made signal heights jump). Manual: reference
    // level at the top, range below it.
    const float range = m_engine ? static_cast<float>(m_engine->traceRangeDb()) : 90.0f;
    if (m_engine && !m_engine->traceAutoRange()) {
        m_traceTop = static_cast<float>(m_engine->traceRefDbm());
        m_traceSeeded = true;
    } else {
        const float target = noise + kNoiseTargetFraction * range;
        const float elapsedSec = m_lastTraceMs > 0 ? (nowMs - m_lastTraceMs) / 1000.0f : 1.0f;
        m_traceTop = m_traceSeeded ? easeLevelToward(m_traceTop, target, elapsedSec) : target;
        m_traceSeeded = true;
    }
    m_traceBottom = m_traceTop - range;
    m_lastTraceMs = nowMs;

    // Levels in the log now and then: bins pinned at the lowest value
    // mean the noise sits below the radio's scale and draws flat.
    if (m_engine && nowMs >= m_nextLevelLogMs) {
        m_nextLevelLogMs = nowMs + kLevelLogIntervalMs;
        QVector<float> sorted = raw;
        std::sort(sorted.begin(), sorted.end());
        const auto atLowest = std::upper_bound(sorted.cbegin(), sorted.cend(), sorted.first()) - sorted.cbegin();
        qCInfo(lcSpectrum).noquote()
            << QStringLiteral("spectrum levels: %1 bins on %2x%3, %4, lowest %5 dBm (%6 % of bins), "
                              "noise %7 dBm, peak %8 dBm, RF gain %9 dB, trace %10..%11 dBm")
                   .arg(sorted.size())
                   .arg(static_cast<int>(width()))
                   .arg(static_cast<int>(height() * m_spectrumFraction))
                   .arg(m_engine->panScaleInfo())
                   .arg(sorted.first(), 0, 'f', 1)
                   .arg(qRound(100.0 * atLowest / sorted.size()))
                   .arg(noise + gain, 0, 'f', 1)
                   .arg(sorted.last(), 0, 'f', 1)
                   .arg(m_engine->rfGain())
                   .arg(m_traceBottom, 0, 'f', 0)
                   .arg(m_traceTop, 0, 'f', 0);
    }

    // Tell the scale strip and the 3D view when a level moved.
    if (std::abs(m_traceTop - m_reportedTop) >= 0.5f || std::abs(m_traceBottom - m_reportedBottom) >= 0.5f
        || std::abs(m_autoNoise - m_reportedNoise) >= 0.5f) {
        m_reportedTop = m_traceTop;
        m_reportedBottom = m_traceBottom;
        m_reportedNoise = m_autoNoise;
        emit traceScaleChanged();
    }
    // Own transmission in the waterfall: the radio pauses waterfall rows
    // during TX, so draw them from the trace on the RX colour scale.
    if (transmitting && !m_waterfall.isNull() && nowMs >= m_nextTxRowMs) {
        m_nextTxRowMs = nowMs + kTxRowIntervalMs;
        followView();
        pushRow(m_spectrum, m_viewLowMhz, m_viewHighMhz, waterfallParams(noise));
        return;
    }
    update();
}

WaterfallLevelMap::Params SpectrumItem::waterfallParams(float autoBlack) const
{
    // The desktop's waterfall law: black at the tracked noise floor (Black
    // level biases it, 50 = none) or, with Auto off, a fixed black point;
    // the colour range's width set by Colour gain alone, so the same signal
    // looks the same on any band and a strong carrier does not darken the
    // rest.
    WaterfallLevelMap::Params p;
    p.autoBlack = !m_engine || m_engine->wfAutoLevel();
    p.autoBlackThresh = autoBlack;
    p.autoBlackOffset = m_engine ? m_engine->wfBlackLevel() : 50;
    p.blackLevel = m_engine ? m_engine->wfBlackLevel() : 50;
    p.colorGain = m_engine ? m_engine->wfColorGain() : 50;
    return p;
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
    // While transmitting the TX rows come from the trace (onSpectrum); for
    // 400 ms after it the radio's AGC settles and its rows are dropped, as
    // the desktop's kPostTxAgcSettleMs.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (bins.isEmpty() || m_waterfall.isNull() || (m_engine && m_engine->radioTransmitting())
        || nowMs - m_txEndMs < kPostTxSettleMs) {
        return;
    }
    followView();
    // Black point: the tile's noise floor (mean of the bins under the mean),
    // smoothed across rows; held while transmitting. Without the RF gain, so
    // a gain step brightens or darkens the picture as on the desktop.
    const float gain = m_engine ? static_cast<float>(m_engine->rfGain()) : 0.0f;
    const float estimate = estimateNoiseFloorDbm(std::span<const float>(bins.constData(), bins.size()));
    if (estimate > -500.0f) {
        m_wfBlack = m_wfBlackSeeded ? 0.95f * m_wfBlack + 0.05f * (estimate - gain) : estimate - gain;
        m_wfBlackSeeded = true;
    }
    WaterfallLevelMap::Params p = waterfallParams(m_wfBlack);
    // Rows carry the RF gain; the black point does not.
    pushRow(bins, lowMhz, highMhz, p);
}

void SpectrumItem::pushRow(const QVector<float>& bins, double lowMhz, double highMhz,
                           const WaterfallLevelMap::Params& levels)
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
        row[x] = m_palette[std::clamp(static_cast<int>(WaterfallLevelMap::level(bins.at(i), levels) * 255.0f + 0.5f), 0, 255)];
    }
    update();
}

double SpectrumItem::bandPlanHeight() const
{
    if (!m_engine || m_engine->bandPlan().isEmpty()) {
        return 0.0;
    }
    return std::clamp<qreal>(height() * 0.025, 14.0, 20.0);
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
    const qreal stripH = bandPlanHeight();
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
        // The desktop's gradient fill: the trace colour at the top, darker
        // and fainter toward the bottom (fill alpha 0.70).
        QLinearGradient gradient(0, 0, 0, specH);
        QColor bottom = QColor(63, 182, 255).darker(300);
        bottom.setAlpha(42);
        gradient.setColorAt(0.0, QColor(63, 182, 255, 140));
        gradient.setColorAt(1.0, bottom);
        painter->fillPath(fill, gradient);
        painter->setPen(QPen(QColor(63, 182, 255), 1.5));
        painter->drawPath(path);
    }

    // Centre line marks the pan centre; the slice marker is drawn in QML.
    painter->setPen(QColor(255, 255, 255, 60));
    painter->drawLine(QPointF(0, specH), QPointF(w, specH));
}

} // namespace AetherSDR::mobile
