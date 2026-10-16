#include "Spectrum3DItem.h"

#include "MobileEngine.h"
#include "SpectrumPalette.h"
#include "gui/DssSupplementalCoverage.h"

#include <QDateTime>
#include <QFile>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <span>

namespace AetherSDR::mobile {

Q_LOGGING_CATEGORY(lcSpectrum3D, "aether.mobile.spectrum3d")

namespace {

using Row = std::array<float, Spectrum3DItem::kCols>;

// Temporal blend (share of the new row): the desktop's kTemporalAlpha, and
// the light setting's.
constexpr float kTemporalAlpha = 0.60f;
constexpr float kLightTemporalAlpha = 0.80f;
// Without waterfall lines (the radio pauses them while transmitting) rows
// come from the trace at this interval.
constexpr qint64 kFftRowFallbackAfterMs = 400;
constexpr qint64 kFftRowIntervalMs = 100;
// Row-span ease per frame (SpectrumWidget's kRowSpanAlpha).
constexpr float kRowSpanAlpha = 0.12f;
// Uniform block of dss_mesh.{vert,frag}: 28 scalars through bgFill, eight
// shadow bands, eight shadow styles, shadow metadata, a frame per ring age.
constexpr int kScalarFields = 28;
constexpr int kShadowBandsOffset = kScalarFields;
constexpr int kShadowStylesOffset = kShadowBandsOffset + Spectrum3DItem::kShadowSlots * 4;
constexpr int kShadowMetaOffset = kShadowStylesOffset + Spectrum3DItem::kShadowSlots * 4;
constexpr int kRowFramesOffset = kShadowMetaOffset + 4;
constexpr int kUboFloats = kRowFramesOffset + Spectrum3DItem::kRows * 4;
static_assert(kUboFloats == 96 + Spectrum3DItem::kRows * 4, "dss_mesh uniform block layout");
static_assert(Spectrum3DItem::kRows == 104, "dss_mesh declares rowFrames[104]");
// Two triangles per column, two crossfade layers (fill and ribbon alike).
constexpr int kVerticesPerRow = (Spectrum3DItem::kMeshCols - 1) * 6 * 2;
const QColor kBackground(QStringLiteral("#0b0f14"));

QShader loadShader(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

float median3(float a, float b, float c)
{
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

bool sameFrame(double c1, double b1, double c2, double b2)
{
    const auto near = [](double x, double y) {
        return std::abs(x - y) <= std::max({1.0, std::abs(x), std::abs(y)}) * 1.0e-9;
    };
    return near(c1, c2) && near(b1, b2);
}

// Peak of each column's bins (DssRenderer's resampledRawRow).
Row resampled(const QVector<float>& bins, float fallback)
{
    Row row;
    const int n = static_cast<int>(bins.size());
    if (n <= 0) {
        row.fill(fallback);
        return row;
    }
    const double step = static_cast<double>(n) / Spectrum3DItem::kCols;
    for (int c = 0; c < Spectrum3DItem::kCols; ++c) {
        const int i0 = std::clamp(static_cast<int>(std::floor(c * step)), 0, n - 1);
        const int i1 = std::clamp(static_cast<int>(std::ceil((c + 1) * step)), i0 + 1, n);
        float peak = std::isfinite(bins[i0]) ? bins[i0] : fallback;
        for (int i = i0 + 1; i < i1; ++i) {
            if (std::isfinite(bins[i])) {
                peak = std::max(peak, bins[i]);
            }
        }
        row[c] = peak;
    }
    return row;
}

// DssRenderer's smoothDssRow (level 2): median of the last three raw rows
// (impulses gone), a [1/4 1/2 1/4] pass across columns, then a blend with the
// previous row when it shows the same frequencies. Level 1 keeps only a light
// blend, so narrow dips (gaps in a voice's spectrum) stay visible; level 0
// draws the rows raw, as live as the 2D trace.
Row smoothRow(const Row& raw, Row& prev1, Row& prev2, int& histCount, const Row* previous, int level)
{
    if (level <= 0) {
        histCount = 0;
        return raw;
    }
    if (level == 1) {
        histCount = 0;
        Row blended = raw;
        if (previous) {
            for (int c = 0; c < Spectrum3DItem::kCols; ++c) {
                blended[c] = kLightTemporalAlpha * raw[c] + (1.0f - kLightTemporalAlpha) * (*previous)[c];
            }
        }
        return blended;
    }
    Row row = raw;
    if (histCount >= 2) {
        for (int c = 0; c < Spectrum3DItem::kCols; ++c) {
            row[c] = median3(raw[c], prev1[c], prev2[c]);
        }
    }
    prev2 = prev1;
    prev1 = raw;
    histCount = std::min(histCount + 1, 2);
    Row smoothed = row;
    for (int c = 0; c < Spectrum3DItem::kCols; ++c) {
        smoothed[c] = 0.25f * row[std::max(0, c - 1)] + 0.5f * row[c]
            + 0.25f * row[std::min(Spectrum3DItem::kCols - 1, c + 1)];
    }
    if (previous) {
        for (int c = 0; c < Spectrum3DItem::kCols; ++c) {
            smoothed[c] = kTemporalAlpha * smoothed[c] + (1.0f - kTemporalAlpha) * (*previous)[c];
        }
    }
    return smoothed;
}

} // namespace

class Spectrum3DRenderer : public QQuickRhiItemRenderer {
public:
    void initialize(QRhiCommandBuffer* cb) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    void writeTextureRow(const Spectrum3DItem* item, int ring);

    QRhi* m_rhi{nullptr};
    std::unique_ptr<QRhiBuffer> m_fillVbuf;
    std::unique_ptr<QRhiBuffer> m_lineVbuf;
    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiTexture> m_heightTex;
    std::unique_ptr<QRhiTexture> m_paletteTex;
    std::unique_ptr<QRhiSampler> m_heightSampler;
    std::unique_ptr<QRhiSampler> m_paletteSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_fillPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_linePipeline;
    QRhiRenderPassDescriptor* m_rpDesc{nullptr};
    bool m_meshUploaded{false};
    bool m_halfFloat{true};

    // Texture rows (RGBA: FFT dBm, coverage, tile dBm, tile coverage) and
    // the ring rows still to upload.
    QVector<qfloat16> m_texHalf;
    QVector<float> m_texFloat;
    QVector<int> m_dirtyRings;
    bool m_uploadAll{true};
    quint64 m_uploadedGeneration{~0ull};
    int m_validRows{0};
    std::array<float, kUboFloats> m_ubo{};
    int m_paletteGain{-1};
    bool m_paletteDirty{true};
};

Spectrum3DItem::Spectrum3DItem(QQuickItem* parent)
    : QQuickRhiItem(parent)
{
    setAlphaBlending(false);
    m_rowClock.start();
    // Frames while the surface glides between rows or the row span eases.
    m_frameTimer.setInterval(16);
    connect(&m_frameTimer, &QTimer::timeout, this, &Spectrum3DItem::tick);
}

void Spectrum3DItem::setEngine(MobileEngine* engine)
{
    if (engine == m_engine) {
        return;
    }
    if (m_engine) {
        disconnect(m_engine, nullptr, this, nullptr);
    }
    m_engine = engine;
    if (m_engine) {
        connect(m_engine, &MobileEngine::spectrumFrame, this, &Spectrum3DItem::onSpectrum);
        connect(m_engine, &MobileEngine::waterfallRow, this, &Spectrum3DItem::onWaterfallRow);
        connect(m_engine, &MobileEngine::connectionStateChanged, this, &Spectrum3DItem::clearHistory);
        connect(m_engine, &MobileEngine::slicesChanged, this, [this] { update(); });
        connect(m_engine, &MobileEngine::filterChanged, this, [this] { update(); });
    }
    emit engineChanged();
}

void Spectrum3DItem::setFloorDbm(double v)
{
    if (std::abs(v - m_floorDbm) >= 0.25) {
        m_floorDbm = v;
        emit levelsChanged();
        update();
    }
}

void Spectrum3DItem::setRangeDb(double v)
{
    if (std::abs(v - m_rangeDb) >= 0.25) {
        m_rangeDb = v;
        emit levelsChanged();
        update();
    }
}

void Spectrum3DItem::setColorGain(int v)
{
    if (v != m_colorGain) {
        m_colorGain = v;
        emit levelsChanged();
        update();
    }
}

void Spectrum3DItem::clearHistory()
{
    m_count = 0;
    m_rawHistCount = 0;
    m_supplementalHistCount = 0;
    m_fft.clear();
    ++m_rowGeneration;
    update();
}

void Spectrum3DItem::onSpectrum(const QVector<float>& binsDbm)
{
    if (binsDbm.isEmpty() || !m_engine || m_engine->panBandwidthMhz() <= 0.0) {
        return;
    }
    m_fft = binsDbm;
    m_fftCenterMhz = m_engine->panCenterMhz();
    m_fftBandwidthMhz = m_engine->panBandwidthMhz();
    // No waterfall lines (transmitting, or a backend without them): rows from
    // the trace, so the surface keeps moving.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastWaterfallRowMs > kFftRowFallbackAfterMs && now >= m_nextFftRowMs) {
        m_nextFftRowMs = now + kFftRowIntervalMs;
        pushRow({}, 0.0, 0.0);
    }
}

void Spectrum3DItem::onWaterfallRow(const QVector<float>& bins, double lowMhz, double highMhz)
{
    // One 3D row per waterfall line, as the desktop: the depth then follows
    // the waterfall's line rate.
    m_lastWaterfallRowMs = QDateTime::currentMSecsSinceEpoch();
    pushRow(bins, lowMhz, highMhz);
}

void Spectrum3DItem::pushRow(const QVector<float>& tile, double tileLowMhz, double tileHighMhz)
{
    // Rows are kept while hidden too, so switching to 3D shows history at once.
    if (m_fft.isEmpty() || m_fftBandwidthMhz <= 0.0) {
        return;
    }
    const Row raw = resampled(m_fft, -200.0f);
    const bool continues = m_count > 0
        && sameFrame(m_rowCenterMhz[m_head], m_rowBandwidthMhz[m_head], m_fftCenterMhz, m_fftBandwidthMhz);
    if (m_count > 0 && !continues) {
        // Neighbouring columns no longer show the same frequencies; blending
        // would smear a signal along the retune.
        m_rawHistCount = 0;
    }
    const int level = m_engine ? m_engine->spectrum3DSmoothing() : 1;
    const Row smoothed =
        smoothRow(raw, m_rawPrev1, m_rawPrev2, m_rawHistCount, continues ? &m_rows[m_head] : nullptr, level);
    const int previousRing = m_head;
    m_head = (m_head - 1 + kRows) % kRows;
    m_rows[m_head] = smoothed;
    m_rowCenterMhz[m_head] = m_fftCenterMhz;
    m_rowBandwidthMhz[m_head] = m_fftBandwidthMhz;

    // The tile is wider than the pan: calibrated to the trace's dBm, it fills
    // the near rows' widened ends where the trace has no data.
    std::vector<float> calibrated;
    if (!tile.isEmpty() && tileHighMhz > tileLowMhz) {
        calibrated = AetherSDR::buildDssSupplementalCoverage(
            std::span<const float>(tile.constData(), static_cast<std::size_t>(tile.size())), tileLowMhz,
            tileHighMhz, std::span<const float>(m_fft.constData(), static_cast<std::size_t>(m_fft.size())),
            m_fftCenterMhz, m_fftBandwidthMhz, static_cast<float>(m_floorDbm - 10.0),
            static_cast<float>(m_floorDbm + m_rangeDb + 10.0));
    }
    if (!calibrated.empty()) {
        const double center = (tileLowMhz + tileHighMhz) / 2.0;
        const double bandwidth = tileHighMhz - tileLowMhz;
        const bool tileContinues = m_count > 0
            && sameFrame(m_supplementalCenterMhz[previousRing], m_supplementalBandwidthMhz[previousRing], center,
                         bandwidth);
        if (!tileContinues) {
            m_supplementalHistCount = 0;
        }
        const QVector<float> asVector(calibrated.begin(), calibrated.end());
        m_supplemental[m_head] = smoothRow(resampled(asVector, -200.0f), m_supplementalPrev1, m_supplementalPrev2,
                                           m_supplementalHistCount,
                                           tileContinues ? &m_supplemental[previousRing] : nullptr, level);
        m_supplementalCoverage[m_head].fill(1);
        m_supplementalCenterMhz[m_head] = center;
        m_supplementalBandwidthMhz[m_head] = bandwidth;
    } else {
        m_supplemental[m_head].fill(-200.0f);
        m_supplementalCoverage[m_head].fill(0);
        m_supplementalCenterMhz[m_head] = 0.0;
        m_supplementalBandwidthMhz[m_head] = 0.0;
        m_supplementalHistCount = 0;
    }
    m_count = std::min(m_count + 1, kRows);
    ++m_rowGeneration;

    // Pace the glide by the measured line interval.
    const float interval = static_cast<float>(m_rowClock.restart());
    if (interval > 20.0f && interval < 2000.0f) {
        m_msPerRow += (interval - m_msPerRow) * 0.2f;
    }
    if (isVisible()) {
        update();
        if (!m_frameTimer.isActive()) {
            m_frameTimer.start();
        }
    }
}

float Spectrum3DItem::scrollProgressRows() const
{
    return std::clamp(static_cast<float>(m_rowClock.elapsed()) / std::max(m_msPerRow, 1.0f), 0.0f, 1.0f);
}

void Spectrum3DItem::tick()
{
    if (!isVisible()) {
        m_frameTimer.stop();
        return;
    }
    update();
    // Stop once the glide is done and the row span settled; the next row or
    // level change starts it again.
    if (scrollProgressRows() >= 1.0f && m_frameTimer.isActive()) {
        bool spanSettled = true;
        if (m_engine && m_count > 0) {
            const double target = m_supplementalBandwidthMhz[m_head] > m_engine->panBandwidthMhz()
                ? std::min(m_supplementalBandwidthMhz[m_head] / m_engine->panBandwidthMhz(),
                           static_cast<double>(kMaxRowSpanFactor))
                : 1.0;
            spanSettled = std::abs(target - m_rowSpanFactor) < 0.002;
        }
        if (spanSettled) {
            m_frameTimer.stop();
        }
    }
}

QQuickRhiItemRenderer* Spectrum3DItem::createRenderer()
{
    return new Spectrum3DRenderer;
}

void Spectrum3DRenderer::writeTextureRow(const Spectrum3DItem* item, int ring)
{
    constexpr int cols = Spectrum3DItem::kCols;
    const auto& row = item->m_rows[ring];
    const auto& tile = item->m_supplemental[ring];
    const auto& tileCoverage = item->m_supplementalCoverage[ring];
    if (m_halfFloat) {
        qfloat16* dst = m_texHalf.data() + ring * cols * 4;
        for (int c = 0; c < cols; ++c) {
            dst[c * 4] = qfloat16(row[c]);
            dst[c * 4 + 1] = qfloat16(1.0f);
            dst[c * 4 + 2] = qfloat16(tile[c]);
            dst[c * 4 + 3] = qfloat16(tileCoverage[c] ? 1.0f : 0.0f);
        }
    } else {
        float* dst = m_texFloat.data() + ring * cols * 4;
        for (int c = 0; c < cols; ++c) {
            dst[c * 4] = row[c];
            dst[c * 4 + 1] = 1.0f;
            dst[c * 4 + 2] = tile[c];
            dst[c * 4 + 3] = tileCoverage[c] ? 1.0f : 0.0f;
        }
    }
}

void Spectrum3DRenderer::synchronize(QQuickRhiItem* rhiItem)
{
    auto* item = static_cast<Spectrum3DItem*>(rhiItem);
    constexpr int rows = Spectrum3DItem::kRows;
    constexpr int cols = Spectrum3DItem::kCols;
    if (m_texHalf.size() != rows * cols * 4) {
        m_texHalf = QVector<qfloat16>(rows * cols * 4, qfloat16(-200.0f));
        m_texFloat = QVector<float>(rows * cols * 4, -200.0f);
        m_uploadAll = true;
    }

    // New rows since the last frame (normally one).
    const quint64 generation = item->m_rowGeneration;
    if (generation != m_uploadedGeneration) {
        const quint64 newRows = m_uploadedGeneration == ~0ull || generation < m_uploadedGeneration
            ? static_cast<quint64>(rows)
            : generation - m_uploadedGeneration;
        if (newRows >= static_cast<quint64>(std::min(item->m_count, rows)) || newRows > 8) {
            for (int ring = 0; ring < rows; ++ring) {
                writeTextureRow(item, ring);
            }
            m_uploadAll = true;
            m_dirtyRings.clear();
        } else {
            for (quint64 n = 0; n < newRows; ++n) {
                const int ring = (item->m_head + static_cast<int>(n)) % rows;
                writeTextureRow(item, ring);
                if (!m_dirtyRings.contains(ring)) {
                    m_dirtyRings.append(ring);
                }
            }
        }
        m_uploadedGeneration = generation;
    }
    m_validRows = item->m_count;

    // The near rows widen over the side wedges as far as the tile reaches,
    // eased so a zoom or stream switch cannot snap the silhouette.
    const double targetBandwidth = item->m_engine ? item->m_engine->panBandwidthMhz() : 0.0;
    const double targetCenter = item->m_engine ? item->m_engine->panCenterMhz() : 0.0;
    float spanTarget = 1.0f;
    if (targetBandwidth > 0.0) {
        for (int age = 0; age < std::min(item->m_count, Spectrum3DItem::kVisibleRows); ++age) {
            const double tileBandwidth = item->m_supplementalBandwidthMhz[(item->m_head + age) % rows];
            if (tileBandwidth > targetBandwidth) {
                spanTarget = std::min(static_cast<float>(tileBandwidth / targetBandwidth),
                                      Spectrum3DItem::kMaxRowSpanFactor);
                break;
            }
        }
    }
    item->m_rowSpanFactor += kRowSpanAlpha * (spanTarget - item->m_rowSpanFactor);
    if (std::abs(spanTarget - item->m_rowSpanFactor) < 0.002f) {
        item->m_rowSpanFactor = spanTarget;
    }
    const float rowSpan = item->m_rowSpanFactor;

    const float floorDbm = static_cast<float>(item->m_floorDbm);
    const float rangeDb = std::max(static_cast<float>(item->m_rangeDb), 10.0f);
    const QSizeF px = QSizeF(item->width(), item->height())
        * (item->window() ? item->window()->effectiveDevicePixelRatio() : 1.0);
    const float scalars[] = {
        (static_cast<float>(item->m_head) + 0.5f) / rows,
        floorDbm,
        rangeDb,
        Spectrum3DItem::kZCurve,
        Spectrum3DItem::kBackWidthFrac,
        Spectrum3DItem::kDepthSpanFrac,
        Spectrum3DItem::kFrontMaxRidgeFrac,
        Spectrum3DItem::kHaze,
        static_cast<float>(cols),
        static_cast<float>(targetBandwidth > 0.0 ? targetBandwidth : 0.1),
        0.0f,  // targetCenterOffsetMhz: frames are already relative to the view
        1.0f,  // rowFrequencyFrames
        item->scrollProgressRows(),
        static_cast<float>(rows),
        1.0f,  // scrollDistanceRows: one row per waterfall line
        std::min(rangeDb, Spectrum3DItem::kColorSpanDb),
        static_cast<float>(item->m_count),
        static_cast<float>(Spectrum3DItem::kVisibleRows),
        static_cast<float>(px.width()),
        static_cast<float>(px.height()),
        rowSpan,
        static_cast<float>(Spectrum3DItem::kMeshCols),
        0.0f, 0.0f,  // std140 hole before bgFill
        static_cast<float>(kBackground.redF()),
        static_cast<float>(kBackground.greenF()),
        static_cast<float>(kBackground.blueF()),
        1.0f,
    };
    static_assert(std::size(scalars) == kScalarFields, "dss_mesh scalar prefix");
    m_ubo.fill(0.0f);
    std::copy(std::begin(scalars), std::end(scalars), m_ubo.begin());

    // The slices' passbands darkened into the surface, with a cue line at the
    // carrier (the desktop's 3D slice shadow): it follows the perspective,
    // which a flat overlay cannot.
    int shadows = 0;
    if (item->m_engine && targetBandwidth > 0.0) {
        const double start = targetCenter - targetBandwidth / 2.0;
        const float margin = (rowSpan - 1.0f) * 0.5f;
        const auto unit = [&](double mhz) { return static_cast<float>((mhz - start) / targetBandwidth); };
        const QVariantList slices = item->m_engine->slices();
        QString activePan;
        for (const QVariant& v : slices) {
            const QVariantMap s = v.toMap();
            if (s.value(QStringLiteral("active")).toBool()) {
                activePan = s.value(QStringLiteral("panId")).toString();
            }
        }
        // Others first, the active slice last (drawn over them).
        for (int pass = 0; pass < 2; ++pass) {
            for (const QVariant& v : slices) {
                const QVariantMap s = v.toMap();
                const bool active = s.value(QStringLiteral("active")).toBool();
                if (active != (pass == 1) || s.value(QStringLiteral("panId")).toString() != activePan
                    || shadows >= Spectrum3DItem::kShadowSlots) {
                    continue;
                }
                const double freq = s.value(QStringLiteral("frequency")).toDouble();
                float low = unit(freq + s.value(QStringLiteral("filterLow")).toDouble() / 1e6);
                float high = unit(freq + s.value(QStringLiteral("filterHigh")).toDouble() / 1e6);
                if (low > high) {
                    std::swap(low, high);
                }
                const float cue = unit(freq);
                if (high < -margin || low > 1.0f + margin) {
                    continue;
                }
                const int band = kShadowBandsOffset + shadows * 4;
                m_ubo[band] = std::clamp(low, -margin, 1.0f + margin);
                m_ubo[band + 1] = std::clamp(high, -margin, 1.0f + margin);
                m_ubo[band + 2] = std::clamp(cue, -margin, 1.0f + margin);
                m_ubo[band + 3] = active ? 0.42f : 0.17f;
                const QColor colour = active ? QColor(0x3f, 0xb6, 0xff) : QColor(0x9a, 0xa6, 0xb2);
                const int style = kShadowStylesOffset + shadows * 4;
                m_ubo[style] = static_cast<float>(colour.redF());
                m_ubo[style + 1] = static_cast<float>(colour.greenF());
                m_ubo[style + 2] = static_cast<float>(colour.blueF());
                m_ubo[style + 3] = (cue >= -margin && cue <= 1.0f + margin) ? (active ? 0.36f : 0.11f) : 0.0f;
                ++shadows;
            }
        }
    }
    m_ubo[kShadowMetaOffset] = static_cast<float>(shadows);
    m_ubo[kShadowMetaOffset + 1] = 1.0f;
    m_ubo[kShadowMetaOffset + 2] = static_cast<float>(item->width());

    for (int age = 0; age < rows; ++age) {
        const int ring = (item->m_head + age) % rows;
        double center = item->m_rowCenterMhz[ring];
        double bandwidth = item->m_rowBandwidthMhz[ring];
        if (!(center > 0.0) || !(bandwidth > 0.0)) {
            center = targetCenter;
            bandwidth = targetBandwidth;
        }
        const int frame = kRowFramesOffset + age * 4;
        m_ubo[frame] = static_cast<float>(center - targetCenter);
        m_ubo[frame + 1] = static_cast<float>(bandwidth);
        if (item->m_supplementalCenterMhz[ring] > 0.0 && item->m_supplementalBandwidthMhz[ring] > 0.0) {
            m_ubo[frame + 2] = static_cast<float>(item->m_supplementalCenterMhz[ring] - targetCenter);
            m_ubo[frame + 3] = static_cast<float>(item->m_supplementalBandwidthMhz[ring]);
        }
    }

    if (item->m_colorGain != m_paletteGain) {
        m_paletteGain = item->m_colorGain;
        m_paletteDirty = true;
    }
}

void Spectrum3DRenderer::initialize(QRhiCommandBuffer*)
{
    if (m_rhi != rhi()) {
        // New graphics context: everything made with the old one goes.
        m_fillPipeline.reset();
        m_linePipeline.reset();
        m_srb.reset();
        m_heightSampler.reset();
        m_paletteSampler.reset();
        m_paletteTex.reset();
        m_heightTex.reset();
        m_ubuf.reset();
        m_lineVbuf.reset();
        m_fillVbuf.reset();
        m_rhi = rhi();
        m_meshUploaded = false;
        m_uploadAll = true;
        m_paletteDirty = true;
        qCInfo(lcSpectrum3D) << "GPU 3D view on" << m_rhi->backendName();
    }
    if (m_fillPipeline && m_rpDesc == renderTarget()->renderPassDescriptor()) {
        return;
    }
    m_rpDesc = renderTarget()->renderPassDescriptor();

    if (!m_fillVbuf) {
        constexpr int vertices = Spectrum3DItem::kVisibleRows * kVerticesPerRow;
        m_fillVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vertices * 3 * sizeof(float)));
        m_lineVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vertices * 3 * sizeof(float)));
        m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUboFloats * sizeof(float)));
        m_fillVbuf->create();
        m_lineVbuf->create();
        m_ubuf->create();
        // Half floats when the GPU takes them (every Apple GPU), else floats.
        m_halfFloat = m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F);
        m_heightTex.reset(m_rhi->newTexture(m_halfFloat ? QRhiTexture::RGBA16F : QRhiTexture::RGBA32F,
                                            QSize(Spectrum3DItem::kCols, Spectrum3DItem::kRows)));
        m_heightTex->create();
        m_paletteTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(256, 1)));
        m_paletteTex->create();
        // Nearest is enough for heights: the mesh is never sparser than the
        // texture. The palette is linear for a smooth floor-to-peak gradient.
        m_heightSampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_heightSampler->create();
        m_paletteSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_paletteSampler->create();
        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubuf.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage, m_heightTex.get(),
                                                      m_heightSampler.get()),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_paletteTex.get(),
                                                      m_paletteSampler.get()),
        });
        m_srb->create();
    }

    const QShader vs = loadShader(QStringLiteral(":/shaders/dss_mesh.vert.qsb"));
    const QShader fs = loadShader(QStringLiteral(":/shaders/dss_mesh.frag.qsb"));
    if (!vs.isValid() || !fs.isValid()) {
        qCWarning(lcSpectrum3D) << "3D shaders missing from the resources";
    }
    QRhiVertexInputLayout layout;
    layout.setBindings({{3 * sizeof(float)}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}});
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    const auto makePipeline = [&]() {
        std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
        p->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
        p->setVertexInputLayout(layout);
        p->setTopology(QRhiGraphicsPipeline::Triangles);
        p->setShaderResourceBindings(m_srb.get());
        p->setRenderPassDescriptor(m_rpDesc);
        p->setTargetBlends({blend});
        p->setCullMode(QRhiGraphicsPipeline::None);
        if (!p->create()) {
            p.reset();
        }
        return p;
    };
    m_fillPipeline = makePipeline();
    // OpenGL draws the ribbons with the fill pipeline (the desktop found a
    // separate identical program misbehaves there); Metal gets its own.
    m_linePipeline = m_rhi->backend() == QRhi::OpenGLES2 ? nullptr : makePipeline();
    qCInfo(lcSpectrum3D) << "3D pipelines ready" << (m_fillPipeline != nullptr) << (m_linePipeline != nullptr)
                         << (m_halfFloat ? "RGBA16F" : "RGBA32F");
}

void Spectrum3DRenderer::render(QRhiCommandBuffer* cb)
{
    if (!m_fillPipeline) {
        cb->beginPass(renderTarget(), kBackground, {1.0f, 0});
        cb->endPass();
        return;
    }
    constexpr int rows = Spectrum3DItem::kRows;
    constexpr int cols = Spectrum3DItem::kCols;
    QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();

    if (!m_meshUploaded) {
        // The static perspective grid (SpectrumWidget::initGpu on main): back
        // rows first so nearer curtains cover them; per column two triangles
        // for each crossfade layer, fill and ribbon tagged by edge.
        constexpr int meshRows = Spectrum3DItem::kVisibleRows;
        constexpr int meshCols = Spectrum3DItem::kMeshCols;
        QVector<float> fill;
        QVector<float> line;
        fill.reserve(meshRows * kVerticesPerRow * 3);
        line.reserve(meshRows * kVerticesPerRow * 3);
        const auto add = [](QVector<float>& v, float uu, float vv, float edge) { v << uu << vv << edge; };
        for (int rr = meshRows - 1; rr >= 0; --rr) {
            const float v = static_cast<float>(rr) / meshRows;
            for (int layer = 0; layer < 2; ++layer) {
                const float fillTag = layer == 0 ? 0.0f : 2.0f;
                const float floorTag = layer == 0 ? 1.0f : 3.0f;
                const float lowerTag = layer == 0 ? -10.0f : -20.0f;
                const float upperTag = layer == 0 ? -11.0f : -21.0f;
                for (int c = 0; c + 1 < meshCols; ++c) {
                    const float u0 = static_cast<float>(c) / (meshCols - 1);
                    const float u1 = static_cast<float>(c + 1) / (meshCols - 1);
                    add(fill, u0, v, fillTag);
                    add(fill, u0, v, floorTag);
                    add(fill, u1, v, fillTag);
                    add(fill, u1, v, fillTag);
                    add(fill, u0, v, floorTag);
                    add(fill, u1, v, floorTag);
                    add(line, u0, v, lowerTag);
                    add(line, u0, v, upperTag);
                    add(line, u1, v, lowerTag);
                    add(line, u1, v, lowerTag);
                    add(line, u0, v, upperTag);
                    add(line, u1, v, upperTag);
                }
            }
        }
        u->uploadStaticBuffer(m_fillVbuf.get(), fill.constData());
        u->uploadStaticBuffer(m_lineVbuf.get(), line.constData());
        m_meshUploaded = true;
    }

    if (m_paletteDirty) {
        // The waterfall's palette through the 3D Gain gamma: 100 lifts colour
        // to the noise floor, 0 keeps it for the strongest peaks.
        const float gamma = std::pow(4.0f, (50.0f - m_paletteGain) / 50.0f);
        QImage pal(256, 1, QImage::Format_RGBA8888);
        for (int i = 0; i < 256; ++i) {
            pal.setPixelColor(i, 0, QColor(spectrumPaletteAt(std::pow(i / 255.0f, gamma))));
        }
        u->uploadTexture(m_paletteTex.get(), pal);
        m_paletteDirty = false;
    }

    const int bytesPerTexel = m_halfFloat ? 4 * sizeof(qfloat16) : 4 * sizeof(float);
    const char* texBase = m_halfFloat ? reinterpret_cast<const char*>(m_texHalf.constData())
                                      : reinterpret_cast<const char*>(m_texFloat.constData());
    if (m_uploadAll) {
        QRhiTextureSubresourceUploadDescription all(texBase, rows * cols * bytesPerTexel);
        all.setSourceSize(QSize(cols, rows));
        u->uploadTexture(m_heightTex.get(), QRhiTextureUploadDescription({0, 0, all}));
        m_uploadAll = false;
        m_dirtyRings.clear();
    } else if (!m_dirtyRings.isEmpty()) {
        QList<QRhiTextureUploadEntry> entries;
        for (const int ring : std::as_const(m_dirtyRings)) {
            QRhiTextureSubresourceUploadDescription row(texBase + ring * cols * bytesPerTexel, cols * bytesPerTexel);
            row.setSourceSize(QSize(cols, 1));
            row.setDestinationTopLeft(QPoint(0, ring));
            entries.append(QRhiTextureUploadEntry(0, 0, row));
        }
        QRhiTextureUploadDescription desc;
        desc.setEntries(entries.cbegin(), entries.cend());
        u->uploadTexture(m_heightTex.get(), desc);
        m_dirtyRings.clear();
    }
    u->updateDynamicBuffer(m_ubuf.get(), 0, kUboFloats * sizeof(float), m_ubo.data());

    const QSize size = renderTarget()->pixelSize();
    cb->beginPass(renderTarget(), kBackground, {1.0f, 0}, u);
    const int drawnRows = std::min(m_validRows, Spectrum3DItem::kVisibleRows);
    if (drawnRows > 0) {
        const QRhiViewport viewport(0, 0, static_cast<float>(size.width()), static_cast<float>(size.height()));
        const int skipped = Spectrum3DItem::kVisibleRows - drawnRows;
        cb->setGraphicsPipeline(m_fillPipeline.get());
        cb->setShaderResources(m_srb.get());
        cb->setViewport(viewport);
        const QRhiCommandBuffer::VertexInput fillInput(m_fillVbuf.get(), 0);
        cb->setVertexInput(0, 1, &fillInput);
        cb->draw(drawnRows * kVerticesPerRow, 1, skipped * kVerticesPerRow, 0);
        // Ridge ribbons over the curtains.
        cb->setGraphicsPipeline(m_linePipeline ? m_linePipeline.get() : m_fillPipeline.get());
        cb->setShaderResources(m_srb.get());
        cb->setViewport(viewport);
        const QRhiCommandBuffer::VertexInput lineInput(m_lineVbuf.get(), 0);
        cb->setVertexInput(0, 1, &lineInput);
        cb->draw(drawnRows * kVerticesPerRow, 1, skipped * kVerticesPerRow, 0);
    }
    cb->endPass();
}

} // namespace AetherSDR::mobile
