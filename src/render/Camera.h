#pragma once
#include <glm/glm.hpp>

class Camera {
public:
    glm::vec3 target{0.0f};  // orbit target point
    float azimuth   = 45.0f;  // degrees around Y
    float elevation = 30.0f;  // degrees above horizon
    float radius    = 500.0f; // distance from target

    glm::mat4 view()       const;
    glm::mat4 projection(float aspect) const;
    glm::mat4 mvp(float aspect)        const;
    // The camera position in rendered coordinates, where heights carry the
    // viewport's vertical exaggeration.
    glm::vec3 eye() const;

    void orbit(float dAz, float dEl);
    void pan(float dx, float dy);
    void zoom(float delta);
};
