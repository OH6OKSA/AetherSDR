#include "Spectrum3DItem.h"

#include "MobileEngine.h"

#include <QDateTime>
#include <QFile>
#include <QLoggingCategory>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace AetherSDR::mobile {

Q_LOGGING_CATEGORY(lcSpectrum3D, "aether.mobile.spectrum3d")

namespace {

// Same perspective as the desktop's DssRenderer.
constexpr float kBackWidthFrac = 0.60f;
constexpr float kDepthSpanFrac = 0.58f;
constexpr float kFrontMaxRidgeFrac = 0.46f;
constexpr float kHaze = 0.16f;
constexpr float kZCurve = 0.70f;
constexpr float kColorSpanDb = 45.0f;

// std140 layout of the shaders' uniform block.
struct Uniforms {
    float clipCorr[16];
    float levels[4];
    float shape[4];
    float grid[4];
    float colour[4];
    float bgFill[4];
    float rowFrames[Spectrum3DItem::kRows][4];
};

QShader loadShader(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

// The desktop's default waterfall colormap (theme "waterfall.colormap.default").
QRgb paletteAt(float v)
{
    struct Stop { float at; int r; int g; int b; };
    static constexpr Stop kStops[] = {
        {0.00f, 0x00, 0x00, 0x00}, {0.15f, 0x00, 0x00, 0x80}, {0.30f, 0x00, 0x40, 0xff},
        {0.45f, 0x00, 0xc8, 0xff}, {0.60f, 0x00, 0xdc, 0x00}, {0.80f, 0xff, 0xff, 0x00},
        {1.00f, 0xff, 0x00, 0x00},
    };
    v = std::clamp(v, 0.0f, 1.0f);
    for (size_t i = 1; i < std::size(kStops); ++i) {
        if (v <= kStops[i].at) {
            const Stop& a = kStops[i - 1];
            const Stop& b = kStops[i];
            const float t = (v - a.at) / (b.at - a.at);
            return qRgba(static_cast<int>(a.r + (b.r - a.r) * t), static_cast<int>(a.g + (b.g - a.g) * t),
                         static_cast<int>(a.b + (b.b - a.b) * t), 255);
        }
    }
    return qRgba(0xff, 0x00, 0x00, 255);
}

} // namespace

class Spectrum3DRenderer : public QQuickRhiItemRenderer {
public:
    void initialize(QRhiCommandBuffer* cb) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    QRhi* m_rhi{nullptr};
    std::unique_ptr<QRhiBuffer> m_vbuf;
    std::unique_ptr<QRhiBuffer> m_ibuf;
    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiTexture> m_heightTex;
    std::unique_ptr<QRhiTexture> m_paletteTex;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    // Ridge outlines (line list over the ridge vertices).
    std::unique_ptr<QRhiBuffer> m_lineIbuf;
    std::unique_ptr<QRhiGraphicsPipeline> m_linePipeline;
    int m_lineIndexCount{0};
    QRhiRenderPassDescriptor* m_rpDesc{nullptr};
    int m_indexCount{0};
    bool m_staticUploaded{false};

    // Ring of rows: the newest at m_head, older ages at m_head + age.
    int m_head{0};
    int m_valid{0};
    std::array<double, Spectrum3DItem::kRows> m_slotCenter{};
    std::array<double, Spectrum3DItem::kRows> m_slotBandwidth{};
    QVector<Spectrum3DItem::PendingRow> m_rows;
    bool m_clear{false};
    double m_viewCenter{0.0};
    double m_viewBandwidth{0.0};
    float m_floor{-120.0f};
    float m_range{60.0f};
    int m_gain{50};
};

Spectrum3DItem::Spectrum3DItem(QQuickItem* parent)
    : QQuickRhiItem(parent)
{
    setAlphaBlending(false);
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
        connect(m_engine, &MobileEngine::connectionStateChanged, this, [this] {
            m_clear = true;
            update();
        });
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

void Spectrum3DItem::onSpectrum(const QVector<float>& binsDbm)
{
    if (!isVisible() || binsDbm.isEmpty() || !m_engine || m_engine->panBandwidthMhz() <= 0.0) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_nextRowMs) {
        return;
    }
    m_nextRowMs = now + kRowIntervalMs;
    // Resample to the texture width, keeping peaks (max over each column).
    PendingRow row;
    row.dbm.resize(kTexCols);
    const qsizetype n = binsDbm.size();
    for (int c = 0; c < kTexCols; ++c) {
        const qsizetype a = static_cast<qsizetype>(c) * n / kTexCols;
        const qsizetype b = std::max(a + 1, static_cast<qsizetype>(c + 1) * n / kTexCols);
        float peak = binsDbm.at(std::min(a, n - 1));
        for (qsizetype i = a + 1; i < b && i < n; ++i) {
            peak = std::max(peak, binsDbm.at(i));
        }
        row.dbm[c] = peak;
    }
    row.centerMhz = m_engine->panCenterMhz();
    row.bandwidthMhz = m_engine->panBandwidthMhz();
    if (m_pending.size() > 8) {
        m_pending.removeFirst();
    }
    m_pending.append(row);
    update();
}

QQuickRhiItemRenderer* Spectrum3DItem::createRenderer()
{
    return new Spectrum3DRenderer;
}

void Spectrum3DRenderer::synchronize(QQuickRhiItem* rhiItem)
{
    auto* item = static_cast<Spectrum3DItem*>(rhiItem);
    m_rows += item->m_pending;
    item->m_pending.clear();
    if (item->m_clear) {
        m_clear = true;
        item->m_clear = false;
    }
    if (item->m_engine) {
        m_viewCenter = item->m_engine->panCenterMhz();
        m_viewBandwidth = item->m_engine->panBandwidthMhz();
    }
    m_floor = static_cast<float>(item->m_floorDbm);
    m_range = static_cast<float>(item->m_rangeDb);
    m_gain = item->m_colorGain;
}

void Spectrum3DRenderer::initialize(QRhiCommandBuffer*)
{
    if (m_rhi != rhi()) {
        // New graphics context: everything made with the old one goes.
        m_pipeline.reset();
        m_linePipeline.reset();
        m_lineIbuf.reset();
        m_srb.reset();
        m_sampler.reset();
        m_paletteTex.reset();
        m_heightTex.reset();
        m_ubuf.reset();
        m_ibuf.reset();
        m_vbuf.reset();
        m_rhi = rhi();
        m_staticUploaded = false;
        m_valid = 0;
        qCInfo(lcSpectrum3D) << "GPU 3D view on" << m_rhi->backendName();
    }
    if (m_pipeline && m_rpDesc == renderTarget()->renderPassDescriptor()) {
        return;
    }
    m_rpDesc = renderTarget()->renderPassDescriptor();

    constexpr int rows = Spectrum3DItem::kRows;
    constexpr int cols = Spectrum3DItem::kMeshCols;
    if (!m_vbuf) {
        // Per row: a top and a base vertex per column (u, age, edge).
        m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                      rows * cols * 2 * 3 * sizeof(float)));
        m_vbuf->create();
        // Back rows first, so nearer curtains cover them.
        m_indexCount = rows * (cols - 1) * 6;
        m_ibuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                      m_indexCount * sizeof(quint32)));
        m_ibuf->create();
        m_lineIndexCount = rows * (cols - 1) * 2;
        m_lineIbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                          m_lineIndexCount * sizeof(quint32)));
        m_lineIbuf->create();
        m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
        m_ubuf->create();
        m_heightTex.reset(m_rhi->newTexture(QRhiTexture::R32F, QSize(Spectrum3DItem::kTexCols, rows)));
        m_heightTex->create();
        m_paletteTex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(256, 1)));
        m_paletteTex->create();
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::Repeat));
        m_sampler->create();
        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::VertexStage, m_heightTex.get(), m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage, m_paletteTex.get(), m_sampler.get()),
        });
        m_srb->create();
    }

    const QShader vs = loadShader(QStringLiteral(":/shaders/spectrum3d.vert.qsb"));
    const QShader fs = loadShader(QStringLiteral(":/shaders/spectrum3d.frag.qsb"));
    if (!vs.isValid() || !fs.isValid()) {
        qCWarning(lcSpectrum3D) << "3D shaders missing from the resources";
    }
    m_pipeline.reset(m_rhi->newGraphicsPipeline());
    m_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });
    QRhiVertexInputLayout layout;
    layout.setBindings({{3 * sizeof(float)}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}});
    m_pipeline->setVertexInputLayout(layout);
    m_pipeline->setShaderResourceBindings(m_srb.get());
    m_pipeline->setRenderPassDescriptor(m_rpDesc);
    m_pipeline->setCullMode(QRhiGraphicsPipeline::None);
    if (!m_pipeline->create()) {
        qCWarning(lcSpectrum3D) << "3D pipeline could not be created";
        m_pipeline.reset();
    }

    m_linePipeline.reset(m_rhi->newGraphicsPipeline());
    m_linePipeline->setTopology(QRhiGraphicsPipeline::Lines);
    m_linePipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/spectrum3d_line.frag.qsb"))},
    });
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_linePipeline->setTargetBlends({blend});
    m_linePipeline->setVertexInputLayout(layout);
    m_linePipeline->setShaderResourceBindings(m_srb.get());
    m_linePipeline->setRenderPassDescriptor(m_rpDesc);
    if (!m_linePipeline->create()) {
        qCWarning(lcSpectrum3D) << "3D outline pipeline could not be created";
        m_linePipeline.reset();
    }
    qCInfo(lcSpectrum3D) << "3D pipelines ready" << (m_pipeline != nullptr) << (m_linePipeline != nullptr);
}

void Spectrum3DRenderer::render(QRhiCommandBuffer* cb)
{
    if (!m_pipeline) {
        cb->beginPass(renderTarget(), QColor(QStringLiteral("#0b0f14")), {1.0f, 0});
        cb->endPass();
        return;
    }
    constexpr int rows = Spectrum3DItem::kRows;
    constexpr int cols = Spectrum3DItem::kMeshCols;
    constexpr int texCols = Spectrum3DItem::kTexCols;
    QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();

    if (!m_staticUploaded) {
        QVector<float> verts;
        verts.reserve(rows * cols * 2 * 3);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const float uu = static_cast<float>(c) / (cols - 1);
                verts << uu << static_cast<float>(r) << 1.0f;  // ridge
                verts << uu << static_cast<float>(r) << 0.0f;  // base
            }
        }
        u->uploadStaticBuffer(m_vbuf.get(), verts.constData());
        QVector<quint32> idx;
        idx.reserve(m_indexCount);
        for (int r = rows - 1; r >= 0; --r) {
            const quint32 base = static_cast<quint32>(r * cols * 2);
            for (int c = 0; c < cols - 1; ++c) {
                const quint32 t0 = base + c * 2, b0 = t0 + 1, t1 = t0 + 2, b1 = t0 + 3;
                idx << t0 << b0 << t1 << t1 << b0 << b1;
            }
        }
        u->uploadStaticBuffer(m_ibuf.get(), idx.constData());
        // Outlines: ridge-to-ridge segments, rows back to front like the fill.
        QVector<quint32> lineIdx;
        lineIdx.reserve(m_lineIndexCount);
        for (int r = rows - 1; r >= 0; --r) {
            const quint32 base = static_cast<quint32>(r * cols * 2);
            for (int c = 0; c < cols - 1; ++c) {
                lineIdx << base + c * 2 << base + (c + 1) * 2;
            }
        }
        u->uploadStaticBuffer(m_lineIbuf.get(), lineIdx.constData());
        QImage pal(256, 1, QImage::Format_RGBA8888);
        for (int i = 0; i < 256; ++i) {
            const QRgb c = paletteAt(i / 255.0f);
            pal.setPixelColor(i, 0, QColor(qRed(c), qGreen(c), qBlue(c)));
        }
        u->uploadTexture(m_paletteTex.get(), pal);
        // Start from the floor everywhere.
        QVector<float> blank(texCols * rows, -200.0f);
        QRhiTextureSubresourceUploadDescription sub(blank.constData(), blank.size() * sizeof(float));
        u->uploadTexture(m_heightTex.get(), QRhiTextureUploadDescription({0, 0, sub}));
        m_staticUploaded = true;
    }

    if (m_clear) {
        m_valid = 0;
        m_rows.clear();
        m_clear = false;
    }
    // New rows: each takes the slot in front of the newest.
    for (const Spectrum3DItem::PendingRow& row : std::as_const(m_rows)) {
        m_head = (m_head - 1 + rows) % rows;
        m_slotCenter[m_head] = row.centerMhz;
        m_slotBandwidth[m_head] = row.bandwidthMhz;
        m_valid = std::min(m_valid + 1, rows);
        QRhiTextureSubresourceUploadDescription sub(row.dbm.constData(), texCols * sizeof(float));
        sub.setDestinationTopLeft(QPoint(0, m_head));
        sub.setSourceSize(QSize(texCols, 1));
        u->uploadTexture(m_heightTex.get(), QRhiTextureUploadDescription({0, 0, sub}));
    }
    m_rows.clear();

    Uniforms un{};
    const QMatrix4x4 corr = m_rhi->clipSpaceCorrMatrix();
    std::memcpy(un.clipCorr, corr.constData(), sizeof(un.clipCorr));
    un.levels[0] = m_floor;
    un.levels[1] = std::max(m_range, 10.0f);
    un.levels[2] = kZCurve;
    un.levels[3] = kHaze;
    un.shape[0] = kBackWidthFrac;
    un.shape[1] = kDepthSpanFrac;
    un.shape[2] = kFrontMaxRidgeFrac;
    un.shape[3] = (m_head + 0.5f) / rows;
    un.grid[0] = texCols;
    un.grid[1] = rows;
    un.grid[2] = m_valid;
    un.grid[3] = rows;
    un.colour[0] = std::min(un.levels[1], kColorSpanDb);
    un.colour[1] = std::pow(4.0f, (50.0f - m_gain) / 50.0f);
    un.colour[2] = static_cast<float>(m_viewBandwidth > 0.0 ? m_viewBandwidth : 0.2);
    const QColor bg(QStringLiteral("#0b0f14"));
    un.bgFill[0] = bg.redF();
    un.bgFill[1] = bg.greenF();
    un.bgFill[2] = bg.blueF();
    un.bgFill[3] = 1.0f;
    for (int age = 0; age < rows; ++age) {
        const int slot = (m_head + age) % rows;
        un.rowFrames[age][0] = static_cast<float>(m_slotCenter[slot] - m_viewCenter);
        un.rowFrames[age][1] = static_cast<float>(m_slotBandwidth[slot]);
    }
    u->updateDynamicBuffer(m_ubuf.get(), 0, sizeof(Uniforms), &un);

    const QSize size = renderTarget()->pixelSize();
    cb->beginPass(renderTarget(), bg, {1.0f, 0}, u);
    const QRhiViewport viewport(0, 0, static_cast<float>(size.width()), static_cast<float>(size.height()));
    const QRhiCommandBuffer::VertexInput vin(m_vbuf.get(), 0);
    // Row by row, back to front: the curtain, then its outline, so nearer
    // curtains cover the outlines behind them.
    const int fillPerRow = (cols - 1) * 6;
    const int linePerRow = (cols - 1) * 2;
    for (int i = 0; i < rows; ++i) {
        cb->setGraphicsPipeline(m_pipeline.get());
        cb->setViewport(viewport);
        cb->setShaderResources();
        cb->setVertexInput(0, 1, &vin, m_ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(fillPerRow, 1, i * fillPerRow);
        if (m_linePipeline) {
            cb->setGraphicsPipeline(m_linePipeline.get());
            cb->setViewport(viewport);
            cb->setShaderResources();
            cb->setVertexInput(0, 1, &vin, m_lineIbuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(linePerRow, 1, i * linePerRow);
        }
    }
    cb->endPass();
}

} // namespace AetherSDR::mobile
