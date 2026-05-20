#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

uniform mat4 uMVP;

out vec2 vTexCoord;

void main() {
    vec3 pos = aPos + vec3(0.0, 0.15, 0.0);
    vTexCoord = aTexCoord;
    gl_Position = uMVP * vec4(pos, 1.0);
}
