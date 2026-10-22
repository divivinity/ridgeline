#version 330 core
in vec4 vColor;
in float vAlong;
in float vDashed;

uniform float uDashPeriodM;  // derived from metres per pixel, so dashes keep their screen size
uniform int uHalo;

out vec4 fragColor;

void main() {
    // The core profile has no line stipple, so a clearing's dashes are cut here
    // from the distance around the outline.
    if (vDashed > 0.5 && uDashPeriodM > 0.0 && fract(vAlong / uDashPeriodM) > 0.55)
        discard;
    fragColor = (uHalo == 1) ? vec4(0.0, 0.0, 0.0, 0.6 * vColor.a) : vColor;
}
