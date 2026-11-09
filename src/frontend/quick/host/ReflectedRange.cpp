#include "ReflectedRange.h"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <vector>

namespace speecher {

namespace {

// A stalled frame resumes the voice rather than leaping it on.
constexpr qreal kLongestStep = 0.05;
// How quickly a pause closes the range, stills it and dims it (s).
constexpr qreal kDrain = 0.25;
constexpr qreal kStill = 0.5;
constexpr qreal kDim = 0.25;
// Reduced motion: how slowly the one level follows the voice (s), and how
// high it goes.
constexpr qreal kCalm = 0.9;
constexpr qreal kCalmHeight = 0.6;
// The resting height of the voice and how it breathes (s a breath).
constexpr qreal kRest = 0.05;
constexpr qreal kBreath = 3.4;

// Eases value toward target, most of the way in about 3 tau, at any frame rate.
qreal follow(qreal value, qreal target, qreal tau, qreal dt)
{
    return value + (target - value) * (1 - std::exp(-dt / tau));
}

// The shader's uniform block (shaders/reflectedrange.frag), std140, after
// the matrix and opacity the scene graph sets.
struct Frame {
    float color[4];
    float farColor[4];
    float glowColor[4];
    float history[RangeVoice::kSamples];
    float size[2];
    float lines;
    float row;
    float frac;
    float sub;
    float time;
    float rest;
    float lineWidth;
    float glowWidth;
    float px;
    float alpha;
    float ink;
};
constexpr int kMatrixAt = 0;
constexpr int kOpacityAt = 64;
constexpr int kFrameAt = 80;
static_assert(offsetof(Frame, history) == 48 && offsetof(Frame, size) == 432 && sizeof(Frame) == 484);

class RangeMaterial : public QSGMaterial {
public:
    RangeMaterial() { setFlag(Blending); }
    QSGMaterialType *type() const override
    {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override { return this == other ? 0 : this < other ? -1 : 1; }

    Frame frame{};
};

class RangeShader : public QSGMaterialShader {
public:
    RangeShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/speecher/shaders/reflectedrange.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/speecher/shaders/reflectedrange.frag.qsb"));
    }
    bool updateUniformData(RenderState &state, QSGMaterial *material, QSGMaterial *) override
    {
        QByteArray *buffer = state.uniformData();
        Q_ASSERT(buffer->size() >= kFrameAt + int(sizeof(Frame)));
        if (state.isMatrixDirty()) {
            const QMatrix4x4 matrix = state.combinedMatrix();
            std::memcpy(buffer->data() + kMatrixAt, matrix.constData(), 64);
        }
        if (state.isOpacityDirty()) {
            const float opacity = state.opacity();
            std::memcpy(buffer->data() + kOpacityAt, &opacity, sizeof opacity);
        }
        std::memcpy(buffer->data() + kFrameAt, &static_cast<RangeMaterial *>(material)->frame, sizeof(Frame));
        return true;
    }
};

QSGMaterialShader *RangeMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new RangeShader;
}

void setColor(float (&out)[4], const QColor &color)
{
    out[0] = color.redF();
    out[1] = color.greenF();
    out[2] = color.blueF();
    out[3] = 1;
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(std::lerp(a.redF(), b.redF(), t), std::lerp(a.greenF(), b.greenF(), t),
                            std::lerp(a.blueF(), b.blueF(), t));
}

float smoothstep(float from, float to, float x)
{
    const float t = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// shaders/reflectedrange.frag on the CPU, for the software renderer, which
// runs no shaders: the same frame into an image of pixels device pixels.
// A ridge's height depends only on x, so each is worked out once a column.
QImage rasterise(const Frame &frame, QSize pixels)
{
    // As in the shader.
    constexpr float kSpread = 26, kBand = 28, kAmp = 0.62f, kRise = 0.96f, kGain = 0.9f;
    const auto level = [&frame](float age) {
        const float at = std::clamp(age, 0.0f, float(RangeVoice::kSamples - 1));
        const int i = int(at);
        return std::lerp(frame.history[i], frame.history[std::min(i + 1, RangeVoice::kSamples - 1)], at - i);
    };
    const int lines = int(frame.lines);
    const float ratio = 1 / frame.px;
    struct Ridge {
        float c, env, lit;
        float tint[3];
    };
    std::vector<Ridge> ridges(size_t(pixels.width()) * (lines + 1));
    for (int x = 0; x < pixels.width(); ++x) {
        const float u = (x + 0.5f) / ratio / frame.size[0] * 2 - 1;
        const float r = std::sqrt(u * u + 0.0016f);
        const float edge = 1 - smoothstep(0.86f, 1, std::abs(u));
        for (int i = 0; i <= lines; ++i) {
            const float d = i == 0 ? 0 : i - 1 + frame.frac;
            Ridge &ridge = ridges[size_t(x) * (lines + 1) + i];
            ridge.env = level(d * frame.row + r * kSpread - frame.sub);
            const float t = frame.time - d * frame.row / float(RangeVoice::kRate);
            const float wave = 0.5f + 0.35f * std::cos(r * 21 - t * 7) + 0.15f * std::cos(r * 37 - t * 11 + 1.3f);
            const float bell = std::exp(-r * r * 2.2f) * (1 - smoothstep(0.6f, 1, r));
            const float h = (frame.rest + ridge.env) * bell * (0.4f + 0.6f * wave);
            ridge.c = (kRise * d / frame.lines + kAmp * h / (1 + d * 0.04f)) * kBand / 2;
            const float depth = std::clamp(d / (frame.lines + 1), 0.0f, 1.0f);
            const float fade = std::pow(1 - depth, 1.5f) * (1 - smoothstep(0.82f, 1, r)) * (i == 0 ? 1 : smoothstep(0, 1, d));
            ridge.lit = fade * edge * (0.55f + 0.75f * ridge.env);
            for (int k = 0; k < 3; ++k) {
                ridge.tint[k] = std::lerp(frame.color[k], frame.farColor[k], std::sqrt(depth));
            }
        }
    }

    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    for (int py = 0; py < pixels.height(); ++py) {
        const float y = std::abs((py + 0.5f) / ratio - frame.size[1] / 2);
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(py));
        for (int x = 0; x < pixels.width(); ++x) {
            const Ridge *column = &ridges[size_t(x) * (lines + 1)];
            const Ridge *next = &ridges[size_t(std::min(x + 1, pixels.width() - 1)) * (lines + 1)];
            float col[3] = {0, 0, 0}, cov = 0, occ = -1e4f;
            for (int i = 0; i <= lines; ++i) {
                const Ridge &ridge = column[i];
                const float s = (next[i].c - ridge.c) * ratio;
                const float slope = std::sqrt(1 + s * s);
                const float dist = std::abs(y - ridge.c) / slope;
                const float shown = smoothstep(occ - frame.px, occ + 0.5f * frame.px, y);
                const float lit = ridge.lit / std::sqrt(slope) * shown;
                const float core = std::exp(-dist * dist / (frame.lineWidth * frame.lineWidth));
                const float glow = std::exp(-dist / frame.glowWidth) * (0.18f + 0.5f * ridge.env) * std::lerp(1.0f, 0.35f, frame.ink);
                for (int k = 0; k < 3; ++k) {
                    col[k] += (ridge.tint[k] * core + frame.glowColor[k] * glow) * lit;
                }
                cov += (core + glow * std::lerp(0.6f, 1.0f, frame.ink)) * lit;
                occ = std::max(occ, ridge.c);
            }
            // Premultiplied, so no channel may pass the alpha: brighter light
            // is scaled down whole, keeping its hue, so the glow adds a
            // little less than on the GPU.
            const float a = std::clamp(cov * kGain * std::lerp(0.8f, 1.0f, frame.ink), 0.0f, 1.0f) * frame.alpha;
            const float brightest = std::max({col[0], col[1], col[2]}) * kGain * frame.alpha;
            const float scale = kGain * frame.alpha * (brightest > a ? a / brightest : 1);
            const auto channel = [&](float v) { return int(255 * v * scale + 0.5f); };
            line[x] = qRgba(channel(col[0]), channel(col[1]), channel(col[2]), int(255 * a + 0.5f));
        }
    }
    return image;
}

} // namespace

void RangeVoice::step(qreal dt, bool listening, bool calm)
{
    if (!listening && settled()) {
        return;
    }
    m_dim = follow(m_dim, listening ? 0 : 1, kDim, dt);
    m_pace = calm ? 0 : follow(m_pace, listening ? 1 : 0, kStill, dt);
    m_time += dt * m_pace;
    // Reduced motion keeps sampling, to ease its one level, while the clock holds.
    m_sub += dt * kRate * (calm ? 1 : m_pace);
    while (m_sub >= 1) {
        m_sub -= 1;
        tick(listening ? m_level : 0, calm);
    }
    if (!listening) {
        const float keep = float(std::exp(-dt / kDrain));
        for (float &sample : m_history) {
            sample *= keep;
        }
        m_amp *= keep;
        m_velocity *= keep;
    }
}

// One sample: auto level, square root, then the spring.
void RangeVoice::tick(qreal loudness, bool calm)
{
    constexpr qreal h = 1 / kRate;
    m_floor = loudness < m_floor ? loudness : m_floor + (loudness - m_floor) * h / 4;
    m_peak = std::max({loudness, m_peak * std::exp(-h / 3), m_floor + 0.15});
    const qreal target = std::sqrt(std::clamp((loudness - m_floor) / (m_peak - m_floor), 0.0, 1.0));
    if (calm) {
        m_amp = follow(m_amp, target * kCalmHeight, kCalm, h);
        m_velocity = 0;
        m_history.fill(float(m_amp));
    } else {
        // Underdamped: 0.6 of critical damping.
        constexpr qreal stiffness = 240;
        const qreal damping = 2 * std::sqrt(stiffness) * 0.6;
        m_velocity += (stiffness * (target - m_amp) - damping * m_velocity) * h;
        m_amp += m_velocity * h;
        std::copy_backward(m_history.begin(), m_history.end() - 1, m_history.end());
        m_history[0] = float(std::max(0.0, m_amp));
    }
    ++m_count;
}

bool RangeVoice::settled() const
{
    return m_dim > 0.995 && m_pace < 0.002 && *std::ranges::max_element(m_history) < 0.002f;
}

ReflectedRange::ReflectedRange(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    // Still frames take new colours too.
    connect(this, &ReflectedRange::paletteChanged, this, &QQuickItem::update);
}

void ReflectedRange::itemChange(ItemChange change, const ItemChangeData &data)
{
    // Each showing starts from rest, not where the last one ended.
    if (change == ItemVisibleHasChanged && data.boolValue) {
        const bool wasMoving = moving();
        m_voice = RangeVoice();
        // Keeps hearing the level it has, which may not change again soon.
        m_voice.hear(m_level);
        if (moving() != wasMoving) {
            emit movingChanged();
        }
    }
    QQuickItem::itemChange(change, data);
}

void ReflectedRange::setLevel(qreal level)
{
    if (level == m_level) {
        return;
    }
    m_level = level;
    m_voice.hear(level);
    emit levelChanged();
}

void ReflectedRange::setListening(bool listening)
{
    if (listening == m_listening) {
        return;
    }
    const bool wasMoving = moving();
    m_listening = listening;
    emit listeningChanged();
    if (moving() != wasMoving) {
        emit movingChanged();
    }
}

void ReflectedRange::advance(qreal seconds)
{
    const bool wasMoving = moving();
    m_voice.step(std::min(seconds, kLongestStep), m_listening, m_movement <= 0);
    update();
    if (moving() != wasMoving) {
        emit movingChanged();
    }
}

QSGNode *ReflectedRange::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (width() <= 0 || height() <= 0) {
        delete old;
        return nullptr;
    }
    // As many ridges as the screen's pixels keep apart, each a little finer
    // on a sharp screen.
    const qreal ratio = window()->effectiveDevicePixelRatio();
    const bool sharp = ratio >= 1.75;
    const bool calm = m_movement <= 0;
    Frame frame{};
    const qreal dim = m_voice.dim();
    setColor(frame.color, mix(m_color, m_pausedColor, 0.55 * dim));
    setColor(frame.farColor, mix(m_farColor, m_pausedColor, 0.55 * dim));
    setColor(frame.glowColor, mix(m_glowColor, m_pausedColor, 0.55 * dim));
    std::ranges::copy(m_voice.history(), frame.history);
    frame.size[0] = float(width());
    frame.size[1] = float(height());
    frame.lines = sharp ? 6 : 4;
    frame.row = sharp ? 8 : 12;
    frame.frac = calm ? 0 : float(std::fmod(m_voice.clock(), frame.row) / frame.row);
    frame.sub = calm ? 0 : float(m_voice.sub());
    frame.time = float(m_voice.time());
    const qreal breath = calm ? 1 : 0.5 + 0.5 * std::sin(m_voice.time() * 2 * std::numbers::pi / kBreath);
    frame.rest = float(kRest * (0.75 + 0.25 * breath));
    frame.lineWidth = sharp ? 0.5f : 0.8f;
    frame.glowWidth = sharp ? 1.8f : 1.4f;
    frame.px = float(1 / ratio);
    frame.alpha = float(1 - 0.25 * dim);
    frame.ink = m_dark ? 0 : 1;

    if (QSGRendererInterface::isApiRhiBased(window()->rendererInterface()->graphicsApi())) {
        auto *node = static_cast<QSGGeometryNode *>(old);
        if (!node) {
            node = new QSGGeometryNode;
            node->setGeometry(new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4));
            node->setMaterial(new RangeMaterial);
            node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        }
        // The texture coordinates carry the item's own pixels to the shader.
        QSGGeometry::updateTexturedRectGeometry(node->geometry(), boundingRect(), boundingRect());
        static_cast<RangeMaterial *>(node->material())->frame = frame;
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        return node;
    }
    // The software renderer runs no shaders, so the CPU draws the same frame.
    auto *node = static_cast<QSGImageNode *>(old);
    if (!node) {
        node = window()->createImageNode();
        node->setOwnsTexture(true);
    }
    QImage image = rasterise(frame, (size() * ratio).toSize());
    image.setDevicePixelRatio(ratio);
    node->setTexture(window()->createTextureFromImage(image));
    node->setRect(boundingRect());
    return node;
}

} // namespace speecher
