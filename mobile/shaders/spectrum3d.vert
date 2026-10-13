#version 440

// Mobile 3D stacked trace (after the desktop's 3DSS, simplified): each
// history row is a curtain from the baseline up to its ridge on a receding
// perspective trapezoid. Heights come from a ring texture of dBm rows; each
// row's own frequency frame keeps old rows on their frequencies after a
// retune or zoom.

layout(location = 0) in vec3 inVert;  // x = u [0,1] across, y = age (0 = newest/front), z = 1 top / 0 base

layout(std140, binding = 0) uniform U {
    mat4 clipCorr;
    vec4 levels;    // floorDbm, rangeDb, zCurve, haze
    vec4 shape;     // backWidthFrac, depthSpanFrac, frontRidgeFrac, rowOffset
    vec4 grid;      // texCols, texRows, validRows, visibleRows
    vec4 colour;    // colorRangeDb, gamma, currentBandwidthMhz, 0
    vec4 bgFill;
    vec4 rowFrames[96];  // per age: centre offset from the current centre (MHz), bandwidth (MHz)
};

layout(binding = 1) uniform sampler2D heightTex;

layout(location = 0) out float vLut;
layout(location = 1) out float vDepth;
layout(location = 2) out float vValid;

void main()
{
    float age = inVert.y;
    float visibleRows = grid.w;
    float depth = age / max(visibleRows - 1.0, 1.0);

    vec4 frame = rowFrames[int(age)];
    float rowBw = frame.y > 0.0 ? frame.y : colour.z;
    // Where this screen column was in the row's own frame.
    float sourceU = 0.5 - frame.x / rowBw + (inVert.x - 0.5) * colour.z / rowBw;
    bool inside = sourceU >= 0.0 && sourceU <= 1.0 && age < grid.z;

    float texU = (clamp(sourceU, 0.0, 1.0) * (grid.x - 1.0) + 0.5) / grid.x;
    float texV = fract(shape.w + age / grid.y);
    float dbm = inside ? texture(heightTex, vec2(texU, texV)).r : levels.x;

    float strength = clamp((dbm - levels.x) / max(levels.y, 1.0), 0.0, 1.0);
    float height = pow(strength, max(levels.z, 0.05));

    float width = mix(1.0, shape.x, depth);
    float x = 0.5 + (inVert.x - 0.5) * width;
    float yBase = 1.0 - depth * shape.y;
    float y = inVert.z > 0.5 ? yBase - height * shape.z * width : yBase;

    float colourStrength = clamp((dbm - levels.x) / max(colour.x, 1.0), 0.0, 1.0);
    // Curtain base in the floor's dark blue, ridge by strength (desktop look).
    vLut = inVert.z > 0.5 ? max(pow(colourStrength, colour.y), 0.16) : 0.16;
    vDepth = depth;
    vValid = age < grid.z ? 1.0 : 0.0;
    gl_Position = clipCorr * vec4(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
}
