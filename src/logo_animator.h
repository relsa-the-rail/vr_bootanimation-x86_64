// Port of android::LogoAnimator: the "meta.bin + meta-colors.png" path that vr_bootanimation
// uses when it is started WITHOUT -c / -m.
#pragma once
#include "common.h"

namespace bootanim {

class LogoAnimator : public Animator {
public:
    LogoAnimator(WindowSize size, const Env& env);
    ~LogoAnimator() override;
    bool init() override;
    void draw() override;
    void saveScreen() override { saving_ = true; }   // [0x38]: enables the colorScale fade
    void windDown() override { stopping_ = true; }   // [0x39]
    bool checkExit() override { return stopped_; }   // [0x3a]
    void setWindowSize(WindowSize s) override { size_ = s; aspectDirty_ = true; }

private:
    bool loadModel(const std::string& path);
    GLuint loadSplash(const std::string& path);

    WindowSize size_;
    Env env_;
    GLuint program_ = 0, vbo_ = 0, ebo_ = 0, vao_ = 0, colorTex_ = 0;
    GLint locColorScale_ = -1, locEyeCenter_ = -1, locEyeID_ = -1, locLogoRot_ = -1,
          locScreenRot_ = -1, locAspect_ = -1, locColorMap_ = -1;
    std::vector<float> verts_;        // x,y,z,u,v
    std::vector<uint32_t> indices_;
    double startTime_ = 0.0;
    int frame_ = 0;
    bool saving_ = false, stopping_ = false, stopped_ = false, aspectDirty_ = true;
};

}  // namespace bootanim
