// Shader sources extracted byte-for-byte from vr_bootanimation (.rodata).
#pragma once
namespace bootanim {
inline const char* const kGltfVert = R"GLSL(
#version 300 es

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inColor;

layout (location = 4) in vec3 inPosition1;
layout (location = 5) in vec3 inNormal1;

uniform mat4 uModelMat;
uniform mat4 uViewMat;
uniform mat4 uProjMat;
uniform float uCorrectForDepth;
uniform float uWeight;

out vec3 fragColor;
out vec3 fragNormal;
out vec2 fragUV;

void main() {
  vec3 pos = inPosition + inPosition1 * uWeight;
  fragNormal = inNormal + inNormal1 * uWeight;
  fragUV = inUV;
  fragColor = inColor;
  gl_Position =  uModelMat * vec4(pos, 1.0);
  // optionally compensate for vertical perspective distortion
  gl_Position.y *= (1.0 - gl_Position.z * uCorrectForDepth);
  gl_Position = uProjMat * uViewMat * gl_Position;
})GLSL";

inline const char* const kGltfFrag = R"GLSL(
#version 300 es
// #define SHADING

precision mediump float;

in vec3 fragColor;
in vec3 fragNormal;
in vec2 fragUV;

uniform bool uUseVertexColor;
uniform bool uUseBaseTexture;
uniform sampler2D uBaseTexture;
uniform vec4 uBaseColor;

out vec4 outColor;

#ifdef SHADING
float lambert(vec3 N, vec3 L)
{
  vec3 nrmN = normalize(N);
  vec3 nrmL = normalize(L);
  float result = dot(nrmN, nrmL);
  return max(result, 0.0);
}
#endif

void main() {
  vec4 color = uBaseColor;
  if (uUseVertexColor) {
    color *= vec4(fragColor.rgb, 1.0f);
  }
  if (uUseBaseTexture) {
    color *= texture(uBaseTexture, fragUV);
  }

#ifdef SHADING
  vec3 lightPosition = vec3(-10.0f, 0.0f, 20.0f);
  color.rgb = color.rgb * max(lambert(fragNormal, lightPosition), 0.3);
#endif
  outColor = color;
})GLSL";

inline const char* const kMetaVert = R"GLSL(
#version 300 es

layout (location = 0) in vec3 vPosition;
layout (location = 3) in vec2 vUV;

uniform bool eyeID;
uniform vec2 screenRotation;
uniform vec2 eyeCenter;
uniform float aspect;
uniform vec2 logoRotation;

out vec3 v_vertexColor;

uniform sampler2D colorMap;

// 3D scene parameters
const float scale = 0.000936f;
const float ipd = 0.01f;
const float dist = 0.5f;

// rotates x,y around z axis
vec3 rotate(vec3 pos, vec2 sincos) {
  float s = sincos.s;
  float c = sincos.t;

  return vec3(
    pos.x * c - pos.y * s,
    pos.x * s + pos.y * c,
    pos.z
  );
}

void main() {
    vec2 center = eyeCenter * 2.0f - (1.0f, 1.0f);

    // initial scaling of the object
    vec3 pos = vPosition * scale;

    // spin the logo around its vertical axis
    pos = rotate(pos.xzy, logoRotation).xzy;

    // apply IPD
    if (eyeID) {
      pos.x -= ipd;
    } else {
      pos.x += ipd;
    }

    // perspective: we are only applying it to x axis
    // to reduce distortion and this is ok since we are headlocked
    // to a horizontal view of the logo
    pos.x *= dist / (pos.z + dist);

    // screen space rotation
    pos = rotate(pos, screenRotation);

    // correct for screen aspect ratio
    pos.y *= aspect;

    // move to the center of the eye
    pos += vec3(center, 0.0f);
    gl_Position = vec4(pos, 1.0f); // neutral "w"

    // the mesh is very dense so we can just read out
    // the color here, and let frag interpolate
    v_vertexColor = texture(colorMap, vUV).rgb;

})GLSL";

inline const char* const kMetaFrag = R"GLSL(
#version 300 es

precision mediump float;

uniform float colorScale;

in vec3 v_vertexColor;
out vec4 outColor;

void main() {
   outColor = vec4(v_vertexColor.xyz, 1.0f) * colorScale;
})GLSL";

}  // namespace bootanim