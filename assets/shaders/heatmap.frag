#version 330 core
in  vec2  vTexCoord;
out vec4  fragColor;

uniform sampler2D uHeatmap;
uniform float     uMinDbm;   // e.g. -120.0
uniform float     uMaxDbm;   // e.g.  -40.0
uniform int       uComparisonMode;
uniform vec2      uGridUvScale;
uniform vec2      uGridUvOffset;

vec3 heatColor(float t) {
    // Dead (red) -> marginal (yellow) -> good (green) -> strong (cyan/blue)
    vec3 c0 = vec3(0.8, 0.1, 0.1);  // dead
    vec3 c1 = vec3(0.9, 0.7, 0.0);  // marginal
    vec3 c2 = vec3(0.1, 0.8, 0.2);  // good
    vec3 c3 = vec3(0.0, 0.6, 1.0);  // strong
    if (t < 0.33) return mix(c0, c1, t / 0.33);
    if (t < 0.66) return mix(c1, c2, (t - 0.33) / 0.33);
    return mix(c2, c3, (t - 0.66) / 0.34);
}

void main() {
    vec2 gridUv = vTexCoord * uGridUvScale + uGridUvOffset;
    if (any(lessThan(gridUv, vec2(0.0))) ||
        any(greaterThan(gridUv, vec2(1.0)))) {
        fragColor = vec4(0.0);
        return;
    }
    float dbm = texture(uHeatmap, gridUv).r;
    if (uComparisonMode != 0) {
        if (dbm < -0.5) {
            fragColor = vec4(0.95, 0.16, 0.18, 0.82); // lost service
        } else if (dbm > 0.5) {
            fragColor = vec4(0.10, 0.88, 0.45, 0.82); // gained service
        } else {
            fragColor = vec4(0.0);                    // unchanged
        }
        return;
    }
    if (dbm < uMinDbm + 1.0) {
        fragColor = vec4(0.0); // transparent for dead zones
        return;
    }
    float t   = clamp((dbm - uMinDbm) / (uMaxDbm - uMinDbm), 0.0, 1.0);
    vec3  col = heatColor(t);
    fragColor = vec4(col, 0.65);  // semi-transparent overlay
}
