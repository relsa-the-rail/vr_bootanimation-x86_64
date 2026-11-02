// Port of the device's `Renderer` (glTF path of vr_bootanimation).
#pragma once
#include "common.h"
#include "vmath.h"
#include "tiny_gltf.h"
#include <unordered_map>

namespace bootanim {

class Renderer : public Animator {
public:
    Renderer(const BootAnimModel& model, WindowSize size, const Env& env);
    ~Renderer() override;

    bool init() override;
    void draw() override;
    void saveScreen() override { exitFlag_ = true; }   // vtable +0x20: sets [0x34]; draw() then only clears
    void windDown() override {}
    bool checkExit() override { return true; }
    int getType() override { return 1; }
    bool checkTransition() override;
    void setWindowSize(WindowSize s) override { size_ = s; }

private:
    struct NodeXform {                // NodeTransformation (key + 0x14..0x3f in the original)
        Vec3 t; Quat r; Vec3 s; float weight = 0.f;
    };

    bool loadModel();
    bool setupGl();
    bool bindModel();
    bool boundsAndSafetyChecks();
    void bindBufferViews();
    void bindModelNodes(const tinygltf::Node& node);
    void bindMesh(int meshIdx);
    void bindCamera(const tinygltf::Node& node);
    void bindTexture(int idx);
    void getAnimationMaxTime(const tinygltf::Animation& anim);
    void processAnimation(const tinygltf::Animation& anim);
    void drawModelNodes(int nodeIdx, const Mat4& parent);
    void drawMesh(int meshIdx);
    NodeXform staticXform(const tinygltf::Node& n) const;
    Quat readQuatLerp(const tinygltf::Accessor& acc, float f) const;
    Vec3 readVec3Lerp(const tinygltf::Accessor& acc, float f) const;
    const unsigned char* accessorElem(const tinygltf::Accessor& acc, int idx) const;

    BootAnimModel cfg_;
    WindowSize size_;
    Env env_;
    tinygltf::Model model_;

    GLuint program_ = 0;
    GLint locProj_ = -1, locView_ = -1, locModel_ = -1, locCorrect_ = -1, locWeight_ = -1;
    GLint locUseVC_ = -1, locUseTex_ = -1, locBaseTex_ = -1, locBaseColor_ = -1;

    std::vector<GLuint> buffers_;      // one per bufferView
    std::vector<GLuint> textures_;
    std::unordered_map<int, GLuint> vaos_;  // key = (mesh<<8)|primitive
    std::unordered_map<int, NodeXform> anim_;

    int sceneIdx_ = 0;
    // camera
    bool cameraBound_ = false;
    Vec3 camT_{0, 0, 0}; Quat camQ_{0, 0, 0, 1};
    double camYfov_ = 0.785398163, camNear_ = 0.1, camFar_ = 100.0;
    bool camPerspective_ = true;
    float correctForDepth_ = 0.f;

    // animation
    bool hasAnim_ = false;
    int animIdx_ = 0;
    float animMax_ = 0.f;
    double startTime_ = 0.0;
    float elapsed_ = 0.f;
    int loopsDone_ = 0;

    bool exitFlag_ = false;
};

}  // namespace bootanim
