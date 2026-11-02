#include "renderer.h"
#include "shaders.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bootanim {

static const float kDeg2Rad = 0.0174532924f;  // 0x3c8efa35, as in the binary

Renderer::Renderer(const BootAnimModel& model, WindowSize size, const Env& env)
    : cfg_(model), size_(size), env_(env) {
    startTime_ = Clock::now();  // steady_clock::now() in the ctor: load time counts against the animation
}

Renderer::~Renderer() {
    if (program_) glDeleteProgram(program_);
}

// ---------------------------------------------------------------- loading ---

bool Renderer::loadModel() {
    std::string path = mapPath(env_, cfg_.path);
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    bool ok = false;
    auto ends = [&](const char* ext) {
        size_t n = std::strlen(ext);
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    if (ends(".glb")) ok = loader.LoadBinaryFromFile(&model_, &err, &warn, path);
    else if (ends(".gltf")) ok = loader.LoadASCIIFromFile(&model_, &err, &warn, path);
    else { ALOGE("Unknown file extension: %s", path.c_str()); return false; }
    if (!warn.empty()) ALOGW("Warning loading %s: %s", path.c_str(), warn.c_str());
    if (!ok) ALOGE("Failed to load %s: %s", path.c_str(), err.c_str());
    return ok;
}

bool Renderer::init() {
    if (!loadModel()) return false;
    for (const auto& e : model_.extensionsUsed)
        if (e == "KHR_draco_mesh_compression") { ALOGE("Model uses Draco compression"); return false; }
    if (!setupGl()) return false;
    if (!bindModel()) return false;
    return true;
}

bool Renderer::setupGl() {
    program_ = createProgram(kGltfVert, kGltfFrag);
    if (!program_) return false;
    glUseProgram(program_);
    locModel_ = glGetUniformLocation(program_, "uModelMat");
    locView_ = glGetUniformLocation(program_, "uViewMat");
    locProj_ = glGetUniformLocation(program_, "uProjMat");
    locCorrect_ = glGetUniformLocation(program_, "uCorrectForDepth");
    locWeight_ = glGetUniformLocation(program_, "uWeight");
    locUseVC_ = glGetUniformLocation(program_, "uUseVertexColor");
    locUseTex_ = glGetUniformLocation(program_, "uUseBaseTexture");
    locBaseTex_ = glGetUniformLocation(program_, "uBaseTexture");
    locBaseColor_ = glGetUniformLocation(program_, "uBaseColor");

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return !checkGlError("setupGl");
}

bool Renderer::boundsAndSafetyChecks() {
    const int nAcc = (int)model_.accessors.size();
    const int nBv = (int)model_.bufferViews.size();
    auto accOk = [&](int a, const char* what, size_t mi, size_t pi) {
        if (a < 0 || a >= nAcc) {
            ALOGE("mesh[%zu].primitive[%zu]: accessor %d (%s) is out of bounds", mi, pi, a, what);
            return false;
        }
        int bv = model_.accessors[a].bufferView;
        if (bv < 0 || bv >= nBv) {
            ALOGE("accessor[%d].bufferView: %d is out of bounds", a, bv);
            return false;
        }
        return true;
    };
    for (size_t mi = 0; mi < model_.meshes.size(); ++mi) {
        const auto& mesh = model_.meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            const auto& p = mesh.primitives[pi];
            for (const auto& kv : p.attributes) if (!accOk(kv.second, kv.first.c_str(), mi, pi)) return false;
            // the device always checks "INDICES": non-indexed primitives are rejected
            if (!accOk(p.indices, "INDICES", mi, pi)) return false;
            for (size_t ti = 0; ti < p.targets.size(); ++ti)
                for (const auto& kv : p.targets[ti]) {
                    std::string nm = "TARGET" + std::to_string(ti) + "_" + kv.first;
                    if (!accOk(kv.second, nm.c_str(), mi, pi)) return false;
                }
        }
    }
    return true;
}

void Renderer::bindBufferViews() {
    buffers_.assign(model_.bufferViews.size(), 0);
    if (!buffers_.empty()) glGenBuffers((GLsizei)buffers_.size(), buffers_.data());
    for (size_t i = 0; i < model_.bufferViews.size(); ++i) {
        const auto& bv = model_.bufferViews[i];
        if (bv.target == 0) continue;  // device skips bufferViews without a target
        const auto& buf = model_.buffers[bv.buffer];
        if (bv.byteOffset + bv.byteLength > buf.data.size()) {
            ALOGE("bufferViews[%zu] offset+length out of bounds", i);
            continue;
        }
        glBindBuffer(bv.target, buffers_[i]);
        glBufferData(bv.target, (GLsizeiptr)bv.byteLength, buf.data.data() + bv.byteOffset, GL_STATIC_DRAW);
    }
}

bool Renderer::bindModel() {
    if (!boundsAndSafetyChecks()) { ALOGE("Bounds and safety checks failed"); return false; }
    bindBufferViews();
    if (model_.scenes.empty()) { ALOGE("No scenes in model"); return false; }
    sceneIdx_ = model_.defaultScene;
    if (sceneIdx_ < 0 || sceneIdx_ >= (int)model_.scenes.size()) {
        ALOGW("Invalid default scene index: %d, defaulting to 0", sceneIdx_);
        sceneIdx_ = 0;
    }
    for (int n : model_.scenes[sceneIdx_].nodes) {
        if (n < 0 || n >= (int)model_.nodes.size()) { ALOGE("Invalid node index: %d", n); return false; }
        bindModelNodes(model_.nodes[n]);
    }
    textures_.assign(model_.textures.size(), 0);
    if (!textures_.empty()) glGenTextures((GLsizei)textures_.size(), textures_.data());
    for (size_t i = 0; i < model_.textures.size(); ++i) bindTexture((int)i);
    if (!model_.animations.empty()) {
        animIdx_ = 0; hasAnim_ = true;
        getAnimationMaxTime(model_.animations[0]);
    }
    if (!cameraBound_) ALOGW("Model has no camera: the device leaves the view/projection uninitialised");
    return true;
}

void Renderer::bindModelNodes(const tinygltf::Node& node) {
    if (node.mesh >= 0 && node.mesh < (int)model_.meshes.size()) bindMesh(node.mesh);
    for (int c : node.children) bindModelNodes(model_.nodes[c]);
    if (node.camera >= 0 && node.camera < (int)model_.cameras.size() && !cameraBound_) bindCamera(node);
}

void Renderer::bindCamera(const tinygltf::Node& node) {
    const auto& cam = model_.cameras[node.camera];
    // camera pose comes straight from the camera node's own TRS (no parent, no animation)
    if (node.translation.size() == 3)
        camT_ = {(float)node.translation[0], (float)node.translation[1], (float)node.translation[2]};
    if (node.rotation.size() == 4)
        camQ_ = {(float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]};
    correctForDepth_ = 0.f;
    if (cam.extras.Has("depthCorrection")) {
        const auto& v = cam.extras.Get("depthCorrection");
        double d = v.IsInt() ? (double)v.Get<int>() : v.IsReal() ? v.Get<double>() : 0.0;
        if (d > 0.0) correctForDepth_ = (float)(1.0 / d);
    }
    if (cam.type == "perspective") {
        camYfov_ = cam.perspective.yfov;
        camNear_ = cam.perspective.znear;
        camFar_ = cam.perspective.zfar;
        camPerspective_ = true;
    } else {
        ALOGW("Orthographic cameras are not reproduced by this preview");
        camPerspective_ = true;
    }
    cameraBound_ = true;
}

void Renderer::bindMesh(int meshIdx) {
    const auto& mesh = model_.meshes[meshIdx];
    for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
        const auto& prim = mesh.primitives[pi];
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        vaos_[(meshIdx << 8) | (int)(pi & 0xff)] = vao;

        auto bindAttr = [&](GLint loc, int accIdx) {
            if (loc < 0) return;
            const auto& acc = model_.accessors[accIdx];
            const auto& bv = model_.bufferViews[acc.bufferView];
            glBindBuffer(GL_ARRAY_BUFFER, buffers_[acc.bufferView]);
            glEnableVertexAttribArray(loc);
            int comps = tinygltf::GetNumComponentsInType(acc.type);
            glVertexAttribPointer(loc, comps, acc.componentType, acc.normalized ? GL_TRUE : GL_FALSE,
                                  acc.ByteStride(bv), (const void*)(uintptr_t)acc.byteOffset);
        };
        for (const auto& kv : prim.attributes) {
            const char* name = nullptr;
            if (kv.first == "NORMAL") name = "inNormal";
            else if (kv.first == "POSITION") name = "inPosition";
            else if (kv.first == "COLOR_0") name = "inColor";
            else if (kv.first == "TEXCOORD_0") name = "inUV";
            if (!name) { ALOGE("Unknown vertex attrib (or missing from shader): %s", kv.first.c_str()); continue; }
            bindAttr(glGetAttribLocation(program_, name), kv.second);
        }
        // Only morph target 0 is ever bound (the shader has a single inPosition1/inNormal1).
        if (!prim.targets.empty()) {
            for (const auto& kv : prim.targets[0]) {
                const char* name = kv.first == "POSITION" ? "inPosition1" : kv.first == "NORMAL" ? "inNormal1" : nullptr;
                if (!name) { ALOGE("target vaa missing: %s", kv.first.c_str()); continue; }
                bindAttr(glGetAttribLocation(program_, name), kv.second);
            }
        }
    }
    glBindVertexArray(0);
}

void Renderer::bindTexture(int idx) {
    const auto& tex = model_.textures[idx];
    if (tex.source < 0 || tex.source >= (int)model_.images.size()) { ALOGE("Invalid texture source: %d", tex.source); return; }
    const auto& img = model_.images[tex.source];
    GLint minF = GL_LINEAR_MIPMAP_NEAREST, magF = GL_LINEAR_MIPMAP_NEAREST, wrapS = GL_REPEAT, wrapT = GL_REPEAT;
    if (tex.sampler >= 0 && tex.sampler < (int)model_.samplers.size()) {
        const auto& s = model_.samplers[tex.sampler];
        if (s.minFilter > 0) minF = s.minFilter;
        if (s.magFilter > 0) magF = s.magFilter;
        wrapS = s.wrapS; wrapT = s.wrapT;
    }
    glBindTexture(GL_TEXTURE_2D, textures_[idx]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLfloat)minF);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magF > GL_LINEAR ? GL_LINEAR : (GLfloat)magF);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    if (img.component != 4 || (img.bits != 8 && img.bits != 16)) {
        ALOGE("Invalid image component/bits: %d/%d", img.component, img.bits); return;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA,
                 img.bits == 16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, img.image.data());
    glGenerateMipmap(GL_TEXTURE_2D);
}

// ------------------------------------------------------------- animation ---

static bool interpOk(const std::string& s) { return s == "STEP" || s == "LINEAR"; }

void Renderer::getAnimationMaxTime(const tinygltf::Animation& anim) {
    animMax_ = 0.f;
    for (const auto& ch : anim.channels) {
        if (ch.sampler < 0 || ch.sampler >= (int)anim.samplers.size()) { ALOGE("Input sampler (%d) out of bounds", ch.sampler); continue; }
        const auto& s = anim.samplers[ch.sampler];
        if (s.input < 0 || s.input >= (int)model_.accessors.size()) { ALOGE("Input sampler (%d) out of bounds", s.input); continue; }
        if (s.output < 0 || s.output >= (int)model_.accessors.size()) { ALOGE("Output sampler (%d) out of bounds", s.output); continue; }
        if (!interpOk(s.interpolation)) { ALOGE("Unsupported animation interpolation: %s", s.interpolation.c_str()); continue; }
        const auto& in = model_.accessors[s.input];
        if (in.componentType != GL_FLOAT) { ALOGE("Unsupported animation input component type: %d", in.componentType); continue; }
        if (!in.maxValues.empty()) animMax_ = std::max(animMax_, (float)in.maxValues[0]);
    }
}

const unsigned char* Renderer::accessorElem(const tinygltf::Accessor& acc, int idx) const {
    const auto& bv = model_.bufferViews[acc.bufferView];
    const auto& buf = model_.buffers[bv.buffer];
    size_t off = bv.byteOffset + acc.byteOffset + (size_t)acc.ByteStride(bv) * (size_t)std::max(idx, 0);
    if (off >= buf.data.size()) return nullptr;
    return buf.data.data() + off;
}

Vec3 Renderer::readVec3Lerp(const tinygltf::Accessor& acc, float f) const {
    int i0 = (int)f;
    int i1 = std::min(i0 + 1, (int)acc.count - 1);
    float t = f - std::floor(f);
    const float* a = (const float*)accessorElem(acc, i0);
    const float* b = (const float*)accessorElem(acc, i1);
    if (!a || !b) return {};
    float u = 1.f - t;
    return {u * a[0] + t * b[0], u * a[1] + t * b[1], u * a[2] + t * b[2]};
}

Quat Renderer::readQuatLerp(const tinygltf::Accessor& acc, float f) const {
    int i0 = (int)f;
    int i1 = std::min(i0 + 1, (int)acc.count - 1);
    float t = f - std::floor(f);
    const float* a = (const float*)accessorElem(acc, i0);
    const float* b = (const float*)accessorElem(acc, i1);
    if (!a || !b) return {};
    return slerp({a[0], a[1], a[2], a[3]}, {b[0], b[1], b[2], b[3]}, t);
}

Renderer::NodeXform Renderer::staticXform(const tinygltf::Node& n) const {
    NodeXform x;
    x.t = {0, 0, 0}; x.r = {0, 0, 0, 1}; x.s = {1, 1, 1}; x.weight = 0.f;
    if (n.translation.size() == 3) x.t = {(float)n.translation[0], (float)n.translation[1], (float)n.translation[2]};
    if (n.rotation.size() == 4) x.r = {(float)n.rotation[0], (float)n.rotation[1], (float)n.rotation[2], (float)n.rotation[3]};
    if (n.scale.size() == 3) x.s = {(float)n.scale[0], (float)n.scale[1], (float)n.scale[2]};
    if (!n.weights.empty()) x.weight = (float)n.weights[0];
    return x;
}

void Renderer::processAnimation(const tinygltf::Animation& anim) {
    // t = max(elapsed - 0.25 s, 0): the animation starts 250 ms after Renderer construction
    float tAnim = std::max(elapsed_ - 0.25f, 0.f);

    for (const auto& ch : anim.channels) {
        if (ch.target_node < 0 || ch.target_node >= (int)model_.nodes.size()) continue;
        // NOTE (bug-compatible): every channel first RESETS the node's entry to its static
        // TRS/weight, so when one node has several channels only the last one survives.
        NodeXform& xf = anim_[ch.target_node];
        xf = staticXform(model_.nodes[ch.target_node]);

        if (ch.sampler < 0 || ch.sampler >= (int)anim.samplers.size()) { ALOGE("Input sampler (%d) out of bounds", ch.sampler); continue; }
        const auto& s = anim.samplers[ch.sampler];
        if (s.input < 0 || s.input >= (int)model_.accessors.size()) { ALOGE("Input sampler (%d) out of bounds", s.input); continue; }
        if (s.output < 0 || s.output >= (int)model_.accessors.size()) { ALOGE("Output sampler (%d) out of bounds", s.output); continue; }
        if (!interpOk(s.interpolation)) { ALOGE("Unsupported animation interpolation: %s", s.interpolation.c_str()); continue; }
        const auto& in = model_.accessors[s.input];
        const auto& out = model_.accessors[s.output];
        if (in.componentType != GL_FLOAT) { ALOGE("Unsupported animation input component type: %d", in.componentType); continue; }
        if (out.componentType != GL_FLOAT) { ALOGE("Unsupported animation output component type: %d", out.componentType); continue; }

        // loop: t_wrapped = t - floor(t/dur)*dur, dur = input accessor max[0]
        float dur = in.maxValues.empty() ? 0.f : (float)in.maxValues[0];
        float q = tAnim / dur;
        float tw = (q - std::floor(q)) * dur;

        // keyframe search -> fractional index f
        float f = 0.f;
        if (in.count >= 2) {
            const float* p0 = (const float*)accessorElem(in, 0);
            float prev = p0 ? *p0 : 0.f;
            for (size_t i = 1; i < in.count; ++i) {
                const float* pc = (const float*)accessorElem(in, (int)i);
                float cur = pc ? *pc : 0.f;
                if (cur >= tw) { f = (float)(i - 1) + (tw - prev) / (cur - prev); break; }
                prev = cur;
            }
        }
        if (s.interpolation == "STEP") f = std::floor(f);

        const bool vec3 = out.type == TINYGLTF_TYPE_VEC3, vec4 = out.type == TINYGLTF_TYPE_VEC4, scal = out.type == TINYGLTF_TYPE_SCALAR;
        if (scal && ch.target_path == "weights") {
            int i0 = (int)f;
            const float* a = (const float*)accessorElem(out, i0);
            const float* b = (const float*)accessorElem(out, std::min(i0 + 1, (int)out.count - 1));
            float t = f - std::floor(f);
            if (a && b) xf.weight = (1.f - t) * (*a) + t * (*b);
        } else if (vec3 && ch.target_path == "translation") {
            xf.t = readVec3Lerp(out, f);
        } else if (vec3 && ch.target_path == "scale") {
            xf.s = readVec3Lerp(out, f);
        } else if (vec4 && ch.target_path == "rotation") {
            xf.r = readQuatLerp(out, f);
        } else {
            ALOGE("Unsupported animation target type (%d) or path (%s)", out.type, ch.target_path.c_str());
        }
    }
}

bool Renderer::checkTransition() {
    // Renderer::checkTransition @0x28b10
    float t = (float)(Clock::now() - startTime_);
    if (hasAnim_ && animMax_ > 0.f) {
        loopsDone_ = (int)std::floor(t / animMax_);
        if (cfg_.minLoops >= 1 && cfg_.minLoops > loopsDone_) return false;
    }
    return cfg_.minSeconds >= 1 ? cfg_.minSeconds <= (int)t : true;
}

// ----------------------------------------------------------------- draw ---

void Renderer::drawMesh(int meshIdx) {
    const auto& mesh = model_.meshes[meshIdx];
    for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
        const auto& prim = mesh.primitives[pi];
        float color[4] = {1, 1, 1, 1};
        const bool hasVC = prim.attributes.find("COLOR_0") != prim.attributes.end();
        bool useTex = false;
        if (prim.material < 0) {
            if (!hasVC) { color[0] = color[1] = color[2] = 0.5f; }  // no material, no COLOR_0 -> grey
        }
        if (prim.material >= 0 && prim.material < (int)model_.materials.size()) {
            const auto& mat = model_.materials[prim.material];
            int ti = mat.pbrMetallicRoughness.baseColorTexture.index;
            if (ti >= 0 && ti < (int)textures_.size()) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, textures_[ti]);
                useTex = true;
            }
            const auto& bc = mat.pbrMetallicRoughness.baseColorFactor;   // emissive etc. are never read
            if (bc.size() >= 4) for (int i = 0; i < 4; ++i) color[i] = (float)bc[i];
        }
        glUniform1ui(locUseTex_, useTex ? 1 : 0);
        glUniform1ui(locUseVC_, hasVC ? 1 : 0);
        glUniform4fv(locBaseColor_, 1, color);

        auto it = vaos_.find((meshIdx << 8) | (int)(pi & 0xff));
        if (it == vaos_.end()) continue;
        glBindVertexArray(it->second);
        const auto& ia = model_.accessors[prim.indices];
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers_[ia.bufferView]);
        glDrawElements(prim.mode, (GLsizei)ia.count, ia.componentType, (const void*)(uintptr_t)ia.byteOffset);
    }
}

void Renderer::drawModelNodes(int nodeIdx, const Mat4& parent) {
    const auto& node = model_.nodes[nodeIdx];
    NodeXform xf = staticXform(node);
    auto it = anim_.find(nodeIdx);
    if (it != anim_.end()) xf = it->second;

    Mat4 model = parent * trs(xf.t, xf.r, xf.s);
    glUniformMatrix4fv(locModel_, 1, GL_FALSE, model.m);
    glUniform1f(locWeight_, xf.weight);
    if (node.mesh >= 0 && node.mesh < (int)model_.meshes.size()) drawMesh(node.mesh);
    for (int c : node.children) drawModelNodes(c, model);
}

void Renderer::draw() {
    elapsed_ = (float)(Clock::now() - startTime_);

    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    if (exitFlag_) return;   // "screen saving" mode: black

    if (hasAnim_) processAnimation(model_.animations[animIdx_]);

    const uint32_t W = size_.width, H = size_.height;
    const int eyeW = (int)(W * 0.5f);
    const float cx[2] = {env_.eyeCenterL, env_.eyeCenterR};
    const float cy = 0.5f;

    // projection from the window size (the camera's own aspectRatio is ignored)
    const float aspect = (W * 0.5f) / (float)H;
    Mat4 proj = cameraBound_ ? perspective(camYfov_, aspect, camNear_, camFar_)
                             : perspective(0.785398163, aspect, 0.1, 100.0);

    for (int eye = 0; eye < 2; ++eye) {
        // exactly as compiled: centre is truncated to whole pixels first, then half-sizes are subtracted
        float cxp = (float)(int)(cx[eye] * (float)W);
        float cyp = (float)(int)(cy * (float)H);
        int vx = (int)(cxp - (float)eyeW * 0.5f);
        int vy = (int)(cyp - (float)H * 0.5f);
        glViewport(vx, vy, eyeW, (GLsizei)H);
        glScissor(eye * eyeW, 0, eyeW, (GLsizei)H);

        // eye 0 sits at -0.25, eye 1 at +0.25 along the camera's local X
        const float e = eye == 0 ? -1.f : 1.f;
        Vec3 eyePos = camT_ + rotate(camQ_, Vec3{e * 0.25f, 0, 0});
        Vec3 fwd = rotate(camQ_, Vec3{0, 0, -1});
        Vec3 up = rotate(camQ_, Vec3{0, 1, 0});
        Mat4 view = lookAt(eyePos, eyePos + fwd, up);

        float angle = e * (float)(-env_.screenRotationDeg) * kDeg2Rad;
        Mat4 p = rotate(proj, angle, Vec3{0, 0, 1});

        glUniformMatrix4fv(locView_, 1, GL_FALSE, view.m);
        glUniformMatrix4fv(locProj_, 1, GL_FALSE, p.m);
        glUniform1f(locCorrect_, correctForDepth_);
        glUniform1i(locBaseTex_, 0);

        for (int n : model_.scenes[sceneIdx_].nodes) drawModelNodes(n, Mat4::identity());
        // (firmware-update progress bar intentionally omitted)
    }
}

}  // namespace bootanim
