#version 440
// The reflected range: the voice's ridge range folded about the item's
// centre line. A point's height is the voice delayed by its distance from
// the centre, on a slow carrier wave, so speech ripples outward from the
// middle. Ridge 0 is now, a bright line through the middle; each ridge
// behind it is the voice a moment earlier, stacked outward, dimmer and
// bluer, and hidden where a newer ridge passes it.

layout(location = 0) in vec2 pos;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 color;       // the front line
    vec4 farColor;    // the oldest ridges
    vec4 glowColor;   // the glow around every line
    vec4 history[24]; // loudness, newest first, 60 samples a second
    vec2 size;        // the item, px
    float lines;      // ridges behind the front line
    float row;        // samples between ridges
    float frac;       // 0 to 1 of the way to the next ridge
    float sub;        // samples since the newest one
    float time;       // seconds of carrier motion
    float rest;       // the voice's height in silence
    float lineWidth;  // px
    float glowWidth;  // px
    float px;         // one device pixel, px
    float alpha;
    float ink;        // 1 on a light sheet: ink, with a faint glow; 0: light
};

const float kSamples = 96.0;
const float kRate = 60.0;
const int kMostLines = 6;
// Samples of delay from the centre to the ends.
const float kSpread = 26.0;
// The band the ridges live in, centred in the item, px: the front ridge's
// peaks reach kAmp of half of it, and the stack kRise.
const float kBand = 28.0;
const float kAmp = 0.62;
const float kRise = 0.96;

float sampleAt(int i)
{
    vec4 four = history[i / 4];
    return four[i - 4 * (i / 4)];
}

// The voice age samples ago, between samples.
float level(float age)
{
    float at = clamp(age, 0.0, kSamples - 1.0);
    int i = int(at);
    return mix(sampleAt(i), sampleAt(min(i + 1, int(kSamples) - 1)), at - float(i));
}

// The range's height at distance r from the centre, d ridges back; env is
// the voice there.
float terrain(float r, float d, out float env)
{
    env = level(d * row + r * kSpread - sub);
    float t = time - d * row / kRate;
    float wave = 0.5 + 0.35 * cos(r * 21.0 - t * 7.0) + 0.15 * cos(r * 37.0 - t * 11.0 + 1.3);
    float bell = exp(-r * r * 2.2) * (1.0 - smoothstep(0.6, 1.0, r));
    return (rest + env) * bell * (0.4 + 0.6 * wave);
}

void main()
{
    float u = pos.x / size.x * 2.0 - 1.0;
    float y = abs(pos.y - size.y * 0.5);
    float halfBand = kBand * 0.5;
    float r = sqrt(u * u + 0.0016);
    float edge = 1.0 - smoothstep(0.86, 1.0, abs(u));   // nothing reaches the ends
    float dx = dFdx(pos.x);

    vec3 col = vec3(0.0);
    float cov = 0.0;
    float occ = -1e4;   // how far out the newer ridges reach here, px
    // A fixed count with the unused ridges off, so every backend unrolls it
    // and dFdx stays in uniform control flow.
    for (int i = 0; i <= kMostLines; ++i) {
        float on = step(float(i), lines);
        float d = i == 0 ? 0.0 : float(i) - 1.0 + frac;
        float env;
        float h = terrain(r, d, env);
        float c = (kRise * d / lines + kAmp * h / (1.0 + d * 0.04)) * halfBand;
        float s = dFdx(c) / dx;
        float slope = sqrt(1.0 + s * s);
        float dist = abs(y - c) / slope;
        float depth = clamp(d / (lines + 1.0), 0.0, 1.0);
        float fade = pow(1.0 - depth, 1.5) * (1.0 - smoothstep(0.82, 1.0, r)) * (i == 0 ? 1.0 : smoothstep(0.0, 1.0, d));
        float shown = smoothstep(occ - px, occ + 0.5 * px, y);
        float lit = on * fade * edge * (0.55 + 0.75 * env) / sqrt(slope) * shown;
        float core = exp(-dist * dist / (lineWidth * lineWidth));
        float glow = exp(-dist / glowWidth) * (0.18 + 0.5 * env) * mix(1.0, 0.35, ink);
        vec3 tint = mix(color.rgb, farColor.rgb, sqrt(depth));
        col += (tint * core + glowColor.rgb * glow) * lit;
        // As light the glow adds more than it covers; as ink it covers.
        cov += (core + glow * mix(0.6, 1.0, ink)) * lit;
        occ = max(occ, c);
    }
    // Dither against banding in the glow.
    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    const float gain = 0.9;
    float a = clamp(cov * gain * mix(0.8, 1.0, ink), 0.0, 1.0);
    col = min(col * gain + n / 255.0, vec3(mix(1.0, a, ink)));
    fragColor = vec4(col, a) * alpha * qt_Opacity;
}
