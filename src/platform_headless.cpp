#include "platform.h"
#include "egl_util.h"

namespace bootanim {

class HeadlessPlatform : public Platform {
public:
    ~HeadlessPlatform() override {
        if (dpy_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surf_ != EGL_NO_SURFACE) eglDestroySurface(dpy_, surf_);
            if (ctx_ != EGL_NO_CONTEXT) eglDestroyContext(dpy_, ctx_);
            eglTerminate(dpy_);
        }
    }
    bool init(const PlatformConfig& cfg) override {
        size_ = {cfg.width, cfg.height};
        auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
        if (getPlatformDisplay) dpy_ = getPlatformDisplay(0x31DD /*EGL_PLATFORM_SURFACELESS_MESA*/, EGL_DEFAULT_DISPLAY, nullptr);
        if (dpy_ == EGL_NO_DISPLAY) dpy_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy_ == EGL_NO_DISPLAY || !eglInitialize(dpy_, nullptr, nullptr)) { ALOGE("EGL init failed"); return false; }
        EGLConfig c;
        if (!chooseConfig(dpy_, EGL_PBUFFER_BIT, false, &c)) { ALOGE("Failed to find any suitable EGL config"); return false; }
        const EGLint pb[] = {EGL_WIDTH, (EGLint)cfg.width, EGL_HEIGHT, (EGLint)cfg.height, EGL_NONE};
        surf_ = eglCreatePbufferSurface(dpy_, c, pb);
        ctx_ = createES3Context(dpy_, c);
        if (surf_ == EGL_NO_SURFACE || ctx_ == EGL_NO_CONTEXT) { ALOGE("EGL surface/context creation failed (0x%x)", eglGetError()); return false; }
        if (eglMakeCurrent(dpy_, surf_, surf_, ctx_) != EGL_TRUE) return false;
        // Software EGL pbuffers cannot resolve MSAA reliably, so MSAA is done in our own FBO.
        if (cfg.msaa) {
            GLint maxS = 0; glGetIntegerv(GL_MAX_SAMPLES, &maxS);
            GLsizei samples = maxS >= 4 ? 4 : maxS;
            glGenFramebuffers(1, &fbo_);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            glGenRenderbuffers(2, rb_);
            glBindRenderbuffer(GL_RENDERBUFFER, rb_[0]);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, (GLsizei)cfg.width, (GLsizei)cfg.height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_[0]);
            glBindRenderbuffer(GL_RENDERBUFFER, rb_[1]);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, (GLsizei)cfg.width, (GLsizei)cfg.height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb_[1]);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                ALOGW("MSAA FBO incomplete, rendering without MSAA");
                glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &fbo_); fbo_ = 0;
            } else ALOGI("headless: offscreen %dx MSAA framebuffer", (int)samples);
        }
        return true;
    }
    GLuint drawTarget() const override { return fbo_; }
    void swapBuffers() override {}
    bool pump(int) override { return true; }
    WindowSize size() const override { return size_; }
    bool takeKey(Key&) override { return false; }
    const char* name() const override { return "headless-egl"; }
    bool interactive() const override { return false; }
private:
    GLuint fbo_ = 0, rb_[2] = {0, 0};
    EGLDisplay dpy_ = EGL_NO_DISPLAY; EGLSurface surf_ = EGL_NO_SURFACE; EGLContext ctx_ = EGL_NO_CONTEXT;
    WindowSize size_;
};

std::unique_ptr<Platform> createHeadlessPlatform() { return std::make_unique<HeadlessPlatform>(); }

}  // namespace bootanim
