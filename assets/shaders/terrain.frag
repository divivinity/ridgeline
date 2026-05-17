#version 330 core
in  vec2  vTexCoord;
in  float vHeight;
out vec4  fragColor;

void main() {
    // Simple height-based color: dark green -> khaki -> grey rock
    float h = clamp(vHeight / 80.0, 0.0, 1.0);
    vec3 low  = vec3(0.13, 0.22, 0.10);
    vec3 mid  = vec3(0.45, 0.38, 0.25);
    vec3 high = vec3(0.55, 0.55, 0.55);
    vec3 col  = mix(mix(low, mid, h * 2.0), high, max(0.0, h * 2.0 - 1.0));
    fragColor = vec4(col, 1.0);
}
