#include "ShaderProgram.h"
#include "RuntimePaths.h"
#include <QFile>
#include <QDebug>
#include <glm/gtc/type_ptr.hpp>

bool ShaderProgram::load(const QString& vertPath, const QString& fragPath) {
    initializeOpenGLFunctions();
    GLuint vert = compileShader(GL_VERTEX_SHADER,   vertPath);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragPath);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }

    m_id = glCreateProgram();
    glAttachShader(m_id, vert);
    glAttachShader(m_id, frag);
    glLinkProgram(m_id);

    GLint ok = 0;
    glGetProgramiv(m_id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(m_id, 512, nullptr, log);
        qWarning() << "Shader link error:" << log;
        glDeleteShader(vert);
        glDeleteShader(frag);
        destroy();
        return false;
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return true;
}

void ShaderProgram::destroy() {
    if (m_id)
        glDeleteProgram(m_id);
    m_id = 0;
}

void ShaderProgram::bind()    { glUseProgram(m_id); }
void ShaderProgram::release() { glUseProgram(0); }

void ShaderProgram::setMat4(const char* n, const glm::mat4& m) {
    glUniformMatrix4fv(glGetUniformLocation(m_id, n), 1, GL_FALSE, glm::value_ptr(m));
}
void ShaderProgram::setVec2(const char* n, const glm::vec2& v) {
    glUniform2fv(glGetUniformLocation(m_id, n), 1, glm::value_ptr(v));
}
void ShaderProgram::setVec3(const char* n, const glm::vec3& v) {
    glUniform3fv(glGetUniformLocation(m_id, n), 1, glm::value_ptr(v));
}
void ShaderProgram::setVec4(const char* n, const glm::vec4& v) {
    glUniform4fv(glGetUniformLocation(m_id, n), 1, glm::value_ptr(v));
}
void ShaderProgram::setFloat(const char* n, float v) {
    glUniform1f(glGetUniformLocation(m_id, n), v);
}
void ShaderProgram::setInt(const char* n, int v) {
    glUniform1i(glGetUniformLocation(m_id, n), v);
}

GLuint ShaderProgram::compileShader(GLenum type, const QString& path) {
    const QString resolvedPath = RuntimePaths::resolve(path);
    QFile f(resolvedPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open shader:" << resolvedPath;
        return 0;
    }
    QByteArray src = f.readAll();
    const char* src_ptr = src.constData();
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src_ptr, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
        qWarning() << "Shader compile error" << resolvedPath << ":" << log;
        return 0;
    }
    return s;
}
