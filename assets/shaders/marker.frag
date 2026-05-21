#version 330 core
in  vec3 vColor;
out vec4 FragColor;

void main() {
    // Discard fragments outside the unit circle to make a round point
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float dist = dot(coord, coord);
    if (dist > 1.0) discard;

    // Bright filled circle with a thin white ring near the edge
    float edge = smoothstep(0.7, 0.85, dist);
    vec3  col  = mix(vColor, vec3(1.0), edge);
    float alpha = 1.0 - smoothstep(0.90, 1.0, dist);
    FragColor = vec4(col, alpha);
}
