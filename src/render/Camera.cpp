#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

static constexpr float PI = 3.14159265f;

glm::vec3 camPos(const Camera& c) {
    float el = c.elevation * PI / 180.0f;
    float az = c.azimuth   * PI / 180.0f;
    return c.target + glm::vec3(
        c.radius * std::cos(el) * std::sin(az),
        c.radius * std::sin(el),
        c.radius * std::cos(el) * std::cos(az)
    );
}

glm::mat4 Camera::view() const {
    return glm::lookAt(camPos(*this), target, {0, 1, 0});
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspective(glm::radians(45.0f), aspect, 1.0f, 50000.0f);
}

glm::mat4 Camera::mvp(float aspect) const {
    return projection(aspect) * view();
}

void Camera::orbit(float dAz, float dEl) {
    azimuth   += dAz;
    elevation  = std::clamp(elevation + dEl, 5.0f, 89.0f);
}

void Camera::pan(float dx, float dy) {
    glm::vec3 right  = glm::normalize(glm::cross(
        camPos(*this) - target, {0, 1, 0}));
    glm::vec3 up     = {0, 1, 0};
    float scale = radius * 0.001f;
    target -= right * dx * scale;
    target += up    * dy * scale;
}

void Camera::zoom(float delta) {
    radius = std::clamp(radius - delta * radius * 0.1f, 10.0f, 20000.0f);
}
