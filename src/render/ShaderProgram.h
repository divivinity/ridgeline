#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QString>
#include <glm/glm.hpp>

class ShaderProgram : protected QOpenGLFunctions_3_3_Core {
public:
    bool   load(const QString& vertPath, const QString& fragPath);
    void   destroy();
    void   bind();
    void   release();
    GLuint id() const { return m_id; }

    void setMat4(const char* name, const glm::mat4& m);
    void setVec2(const char* name, const glm::vec2& v);
    void setVec3(const char* name, const glm::vec3& v);
    void setVec4(const char* name, const glm::vec4& v);
    void setFloat(const char* name, float v);
    void setInt(const char* name, int v);

private:
    GLuint compileShader(GLenum type, const QString& path);
    GLuint m_id = 0;
};
