#version 330 core
// Outlines of drawn vegetation areas, draped on the terrain mesh.
layout(location = 0) in vec3 aPos;     // unscaled terrain metres, on the drawn mesh
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aAlong;  // metres around the outline's perimeter
layout(location = 3) in float aDashed;
layout(location = 4) in vec2 aSlope;   // rise per metre east and north of the triangle under the line

uniform mat4 uMVP;          // the scene matrix, vertical exaggeration included
uniform vec3 uEye;          // camera position in unscaled terrain metres
uniform float uLiftM;       // true metres above the mesh, clearing the imagery drape
uniform float uPull;        // fraction of the eye distance pulled toward the eye
uniform vec2 uPixelOffset;  // non-zero only for the dark halo passes
uniform vec2 uViewportPx;
uniform float uVertScale;   // the vertical exaggeration inside uMVP
uniform float uPixelAngle;  // radians of view per framebuffer pixel

out vec4 vColor;
out float vAlong;
out float vDashed;

void main() {
    // Lift clears the basemap's own +0.15 m depth-writing drape up close. The
    // pull is a scaling about the eye, so each vertex stays on its own view ray
    // and nothing appears to move; it keeps the line in front of the surface at
    // distances where a fixed lift loses to depth-buffer precision.
    vec3 p = aPos + vec3(0.0, uLiftM, 0.0);

    // The imagery drape is drawn with glPolygonOffset(-1, -1), which moves it
    // toward the eye by about one pixel of its own depth slope: an eye distance
    // of d * pixelAngle * cot(grazing). A fixed lift and pull lose to that once
    // the ground is seen obliquely, so the pull also grows with the cotangent,
    // measured on the rendered, exaggerated triangle under the line. As a
    // fraction of d it does not depend on distance, which is why a scaling
    // about the eye is the right correction. Three pixels' worth covers the
    // drape's own pixel, the halo's one-pixel shift and slope-estimate slack;
    // the grazing angle is floored at about 3 degrees so an edge-on triangle
    // cannot pull the line through nearby ground in front of it.
    vec3 toVertex = vec3(p.x - uEye.x, (p.y - uEye.y) * uVertScale, p.z - uEye.z);
    float pull = uPull;
    if (dot(toVertex, toVertex) > 1.0e-6) {
        vec3 normal = normalize(vec3(-aSlope.x * uVertScale, 1.0, -aSlope.y * uVertScale));
        float sinGrazing = abs(dot(normalize(toVertex), normal));
        float cotGrazing = sqrt(max(1.0 - sinGrazing * sinGrazing, 0.0)) / max(sinGrazing, 0.05);
        pull += 3.0 * uPixelAngle * cotGrazing;
    }
    p = uEye + (p - uEye) * (1.0 - pull);
    vec4 clip = uMVP * vec4(p, 1.0);
    // A whole-pixel shift in screen space, for a halo that line width cannot
    // provide: widths above one pixel are not guaranteed in a core profile.
    clip.xy += uPixelOffset * 2.0 / uViewportPx * clip.w;

    vColor = aColor;
    vAlong = aAlong;
    vDashed = aDashed;
    gl_Position = clip;
}
