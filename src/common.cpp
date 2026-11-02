#include "common.h"
#include <chrono>
#include <cstring>

namespace bootanim {

static double g_virtual = 0.0;

double Clock::now() { return g_virtual; }
void Clock::set(double t) { g_virtual = t; }
double Clock::real() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string mapPath(const Env& env, const std::string& p) {
    static const std::string kBootanim = "/system_ext/etc/bootanim/";
    static const std::string kSysExt = "/system_ext/";
    auto join = [](const std::string& dir, const std::string& rest) {
        return dir.empty() || dir.back() == '/' ? dir + rest : dir + "/" + rest;
    };
    if (!env.bootanimDir.empty() && p.compare(0, kBootanim.size(), kBootanim) == 0)
        return join(env.bootanimDir, p.substr(kBootanim.size()));
    if (!env.systemExtDir.empty() && p.compare(0, kSysExt.size(), kSysExt) == 0)
        return join(env.systemExtDir, p.substr(kSysExt.size()));
    return p;
}

static GLuint loadShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    if (!sh) { checkGlError("glCreateShader"); return 0; }
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 1 ? len : 1, '\0');
        glGetShaderInfoLog(sh, len, nullptr, &log[0]);
        ALOGE("Could not compile shader (type: %d):\n%s", (int)type, log.c_str());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint createProgram(const char* vs, const char* fs) {
    GLuint v = loadShader(GL_VERTEX_SHADER, vs);
    if (!v) { ALOGE("Vertex shader compilation failed"); return 0; }
    GLuint f = loadShader(GL_FRAGMENT_SHADER, fs);
    if (!f) { ALOGE("Fragment shader compilation failed"); return 0; }
    GLuint prog = glCreateProgram();
    if (!prog) { checkGlError("glCreateProgram"); return 0; }
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 1 ? len : 1, '\0');
        glGetProgramInfoLog(prog, len, nullptr, &log[0]);
        ALOGE("Could not link program:\n%s", log.c_str());
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

bool checkGlError(const char* op) {
    bool any = false;
    for (GLint e = glGetError(); e; e = glGetError()) {
        ALOGE("%s failed with error 0x%x", op, (unsigned)e);
        any = true;
    }
    return any;
}

}  // namespace bootanim
