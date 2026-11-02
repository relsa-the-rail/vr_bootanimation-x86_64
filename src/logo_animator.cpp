#include "logo_animator.h"
#include "shaders.h"
#include "stb_image.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bootanim {

static const double kPi = 3.141592653589793;

LogoAnimator::LogoAnimator(WindowSize size, const Env& env) : size_(size), env_(env) {
    startTime_ = Clock::now();
}

LogoAnimator::~LogoAnimator() {
    if (program_) glDeleteProgram(program_);
}

bool LogoAnimator::loadModel(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { ALOGW("Failed to open %s", path.c_str()); return false; }
    int32_t n = 0, m = 0;
    bool ok = std::fread(&n, 4, 1, f) == 1 && n >= 0;
    if (ok) { verts_.resize((size_t)n * 5); ok = std::fread(verts_.data(), 4, verts_.size(), f) == verts_.size(); }
    if (ok) ok = std::fread(&m, 4, 1, f) == 1 && m >= 0;
    if (ok) { indices_.resize((size_t)m); ok = std::fread(indices_.data(), 4, indices_.size(), f) == indices_.size(); }
    std::fclose(f);
    if (!ok) ALOGW("Short/invalid read in %s", path.c_str());
    return ok;
}

GLuint LogoAnimator::loadSplash(const std::string& path) {
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!px) { ALOGE("Could not load image '%s'", path.c_str()); return 0; }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(px);
    return tex;
}

bool LogoAnimator::init() {
    program_ = createProgram(kMetaVert, kMetaFrag);
    if (!program_) return false;
    glUseProgram(program_);
    if (!loadModel(mapPath(env_, "/system_ext/etc/bootanim/meta.bin"))) return false;

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts_.size() * 4), verts_.data(), GL_STATIC_DRAW);
    // (the device draws indices from client memory; an EBO gives the same result)
    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices_.size() * 4), indices_.data(), GL_STATIC_DRAW);

    locColorScale_ = glGetUniformLocation(program_, "colorScale");
    locEyeCenter_ = glGetUniformLocation(program_, "eyeCenter");
    locEyeID_ = glGetUniformLocation(program_, "eyeID");
    locLogoRot_ = glGetUniformLocation(program_, "logoRotation");
    locScreenRot_ = glGetUniformLocation(program_, "screenRotation");
    locAspect_ = glGetUniformLocation(program_, "aspect");
    locColorMap_ = glGetUniformLocation(program_, "colorMap");

    colorTex_ = loadSplash(mapPath(env_, "/system_ext/etc/bootanim/meta-colors.png"));
    if (!colorTex_) { ALOGW("Missing color map"); return false; }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glUniform1i(locColorMap_, 0);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (const void*)0);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 20, (const void*)12);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    return !checkGlError("LogoAnimator::init");
}

void LogoAnimator::draw() {
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, size_.width, size_.height);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);   // no per-eye viewport: the shader places each eye

    glUseProgram(program_);
    glBindVertexArray(vao_);
    if (aspectDirty_) { glUniform1f(locAspect_, (float)size_.width / (float)size_.height); aspectDirty_ = false; }

    float eyes[4] = {env_.eyeCenterL, 0.5f, env_.eyeCenterR, 0.5f};
    if (stopping_) { ALOGI("Stop at mFrame %d", frame_); stopped_ = true; }

    // t = max(elapsed - 0.5 s, 0); x = t * 2*pi / 3.3 ; y = x - sin x
    float elapsed = (float)(Clock::now() - startTime_);
    float t = std::fmax(elapsed - 0.5f, 0.f);
    float x = (float)((double)t * (2.0 * kPi) / 3.2999999523162842);
    float y = x - std::sin(x);
    float angle = (y + 2.f * (y - std::sin(y))) / 3.f * 0.5f;
    float logoRot[2] = {std::sin(angle), std::cos(angle)};   // (sin, cos)
    glUniform2fv(locLogoRot_, 1, logoRot);

    float colorScale = saving_ ? std::cos(y * 0.5f) : 1.0f;
    glUniform1f(locColorScale_, colorScale);

    const float rot = (float)(env_.screenRotationDeg * kPi / 180.0);
    for (int eye = 0; eye < 2; ++eye) {
        float a = eye == 0 ? rot : -rot;
        float sr[2] = {std::sin(a), std::cos(a)};
        glUniform2fv(locScreenRot_, 1, sr);
        glUniform2fv(locEyeCenter_, 1, eye == 0 ? &eyes[0] : &eyes[2]);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(locEyeID_, eye);
        glDrawElements(GL_TRIANGLES, (GLsizei)indices_.size(), GL_UNSIGNED_INT, (const void*)0);
    }
    checkGlError("glDrawElements");
    if (!stopped_) ++frame_;
}

}  // namespace bootanim
