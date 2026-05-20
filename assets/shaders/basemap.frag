#version 330 core
in  vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uBasemap;
uniform float     uOpacity;

void main() {
    vec4 c = texture(uBasemap, vTexCoord);
    fragColor = vec4(c.rgb, c.a * uOpacity);
}
