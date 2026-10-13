#version 440

// Ridge outline of each history row: the ridge colour lifted toward white,
// fading with depth (the desktop's bright ridge lines).

layout(location = 0) in float vLut;
layout(location = 1) in float vDepth;
layout(location = 2) in float vValid;

layout(std140, binding = 0) uniform U {
    mat4 clipCorr;
    vec4 levels;
    vec4 shape;
    vec4 grid;
    vec4 colour;
    vec4 bgFill;
    vec4 rowFrames[96];
};

layout(binding = 2) uniform sampler2D paletteTex;

layout(location = 0) out vec4 fragColor;

void main()
{
    if (vValid < 0.5) {
        discard;
    }
    vec3 c = texture(paletteTex, vec2(clamp(vLut, 0.0, 1.0), 0.5)).rgb;
    c = mix(c, vec3(0.85, 0.9, 1.0), 0.45);
    float a = (1.0 - vDepth * 0.75) * 0.8;
    fragColor = vec4(c * a, a);
}
