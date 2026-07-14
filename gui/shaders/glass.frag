#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemSize;
    vec2 cardSize;
    float pad;
    float radiusPx;
    float refractPx;
    vec4 tintColor;
    vec4 rimColor;
    vec4 shadowColor;
    float specularStrength;
    float blurRadius;
} ubuf;

layout(binding = 1) uniform sampler2D source;

float roundedBoxSDF(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec3 boostSaturation(vec3 c, float amt) {
    float l = luminance(c);
    return mix(vec3(l), c, amt);
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 texel = 1.0 / ubuf.itemSize;
    vec2 pxPos = uv * ubuf.itemSize;
    vec2 canvasCenter = ubuf.itemSize * 0.5;
    vec2 p = pxPos - canvasCenter;
    vec2 cardHalf = max(ubuf.cardSize * 0.5, vec2(1.0));

    float d = roundedBoxSDF(p, cardHalf, ubuf.radiusPx);
    float outerMask = 1.0 - smoothstep(ubuf.pad - 2.0, ubuf.pad, d);
    if (outerMask <= 0.001) {
        fragColor = vec4(0.0);
        return;
    }

    float br = max(ubuf.blurRadius, 0.0);
    float brDiag = br * 0.67;
    vec3 s0 = texture(source, uv).rgb;
    vec3 s1 = texture(source, clamp(uv + texel * vec2( br,  0.0), vec2(0.001), vec2(0.999))).rgb;
    vec3 s2 = texture(source, clamp(uv + texel * vec2(-br,  0.0), vec2(0.001), vec2(0.999))).rgb;
    vec3 s3 = texture(source, clamp(uv + texel * vec2( 0.0,  br), vec2(0.001), vec2(0.999))).rgb;
    vec3 s4 = texture(source, clamp(uv + texel * vec2( 0.0, -br), vec2(0.001), vec2(0.999))).rgb;
    vec3 s5 = texture(source, clamp(uv + texel * vec2( brDiag,  brDiag), vec2(0.001), vec2(0.999))).rgb;
    vec3 s6 = texture(source, clamp(uv + texel * vec2(-brDiag,  brDiag), vec2(0.001), vec2(0.999))).rgb;
    vec3 s7 = texture(source, clamp(uv + texel * vec2( brDiag, -brDiag), vec2(0.001), vec2(0.999))).rgb;
    vec3 s8 = texture(source, clamp(uv + texel * vec2(-brDiag, -brDiag), vec2(0.001), vec2(0.999))).rgb;
    vec3 blurred = s0 * 0.24 + (s1 + s2 + s3 + s4) * 0.12 + (s5 + s6 + s7 + s8) * 0.07;

    float l0 = luminance(s0);
    float lAvg = (l0 + luminance(s1) + luminance(s2) + luminance(s3) + luminance(s4)) / 5.0;
    float variance = (abs(l0 - lAvg) + abs(luminance(s1) - lAvg) + abs(luminance(s2) - lAvg) + abs(luminance(s3) - lAvg) + abs(luminance(s4) - lAvg)) / 5.0;
    float complexity = clamp(variance * 6.0, 0.0, 1.0);

    float t = clamp(d / ubuf.pad, 0.0, 1.0);
    float shadowAlpha = (1.0 - t) * mix(0.08, 0.34, complexity);
    vec4 shadowResult = vec4(ubuf.shadowColor.rgb, shadowAlpha);

    vec2 dirFromCenter = p / cardHalf;
    float distIn = clamp(-d / ubuf.refractPx, 0.0, 1.0);
    float distFromEdge = 1.0 - distIn;
    float lensCurve = distFromEdge * distFromEdge * (3.0 - 2.0 * distFromEdge);
    vec2 baseOffset = lensCurve * dirFromCenter * ubuf.refractPx * 0.85;

    float chromaSpread = 0.4;
    vec2 offR = baseOffset * (1.0 + chromaSpread);
    vec2 offB = baseOffset * (1.0 - chromaSpread);
    vec2 uvR = clamp(uv + offR * texel, vec2(0.001), vec2(0.999));
    vec2 uvG = clamp(uv + baseOffset * texel, vec2(0.001), vec2(0.999));
    vec2 uvB = clamp(uv + offB * texel, vec2(0.001), vec2(0.999));
    vec3 lensed = vec3(
        texture(source, uvR).r,
        texture(source, uvG).g,
        texture(source, uvB).b
    );
    vec3 bg = mix(blurred, lensed, lensCurve);

    bg = boostSaturation(bg, 1.25);
    bg *= 1.06;

    vec3 col = mix(bg, ubuf.tintColor.rgb, ubuf.tintColor.a);

    float ring = exp(-abs(d) * 0.5);
    col += ring * ubuf.rimColor.rgb * ubuf.rimColor.a * ubuf.specularStrength;

    float glassAlpha = 1.0 - smoothstep(-1.5, 0.0, d);
    vec4 glassResult = vec4(clamp(col, 0.0, 1.0), glassAlpha);

    float insideMask = 1.0 - smoothstep(-1.5, 1.5, d);
    vec4 result = mix(shadowResult, glassResult, insideMask);

    fragColor = result * outerMask * ubuf.qt_Opacity;
}
