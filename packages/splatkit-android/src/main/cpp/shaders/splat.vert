#version 450

// One instance per splat, four vertices per instance (triangle strip quad).
// Ported from MetalSplatter's SplatProcessing.metal, which follows the reference
// rasterizer of Kerbl et al. 2023 and Zwicker's EWA projection.

layout(set = 0, binding = 0) uniform Camera {
  mat4 view;
  mat4 proj;
  vec2 focal;         // pixels: screenSize * proj[0][0] / 2, screenSize * proj[1][1] / 2
  vec2 tanHalfFov;    // 1 / proj[0][0], 1 / proj[1][1]
  vec2 screenSize;    // pixels
  uint outputLinear;  // 1 when the swapchain is sRGB and expects linear values
  uint pad;
} cam;

struct Splat {
  vec4 positionAlpha;  // xyz, alpha
  vec4 covA;           // xx, xy, xz, yy
  vec2 covB;           // yz, zz
  uint rgba8;          // packed colour
  uint unused;
};

layout(std430, set = 0, binding = 1) readonly buffer Splats { Splat splats[]; };
layout(std430, set = 0, binding = 2) readonly buffer Order { uint order[]; };

layout(location = 0) out vec2 relativePosition;  // in units of sigma
layout(location = 1) out vec4 color;

const float kBoundsRadius = 3.0;  // draw out to 3 sigma; beyond that nothing is visible
const vec2 kCorners[4] = vec2[](vec2(-1, -1), vec2(-1, 1), vec2(1, -1), vec2(1, 1));

// Projects the 3D covariance to screen space: Sigma' = J W Sigma W^T J^T.
vec3 projectCovariance(vec3 viewPos, vec4 covA, vec2 covB) {
  float invZ = 1.0 / viewPos.z;
  float invZ2 = invZ * invZ;

  // Clamp the projected center so the Jacobian stays finite at the frustum edges.
  vec2 lim = 1.3 * cam.tanHalfFov;
  viewPos.x = clamp(viewPos.x * invZ, -lim.x, lim.x) * viewPos.z;
  viewPos.y = clamp(viewPos.y * invZ, -lim.y, lim.y) * viewPos.z;

  mat3 J = mat3(
    cam.focal.x * invZ, 0.0, 0.0,
    0.0, cam.focal.y * invZ, 0.0,
    -cam.focal.x * viewPos.x * invZ2, -cam.focal.y * viewPos.y * invZ2, 0.0);
  mat3 W = mat3(cam.view);
  mat3 T = J * W;
  mat3 Vrk = mat3(
    covA.x, covA.y, covA.z,
    covA.y, covA.w, covB.x,
    covA.z, covB.x, covB.y);
  mat3 cov = T * Vrk * transpose(T);
  // Low pass filter: every splat is at least about a pixel wide, so none flicker.
  return vec3(cov[0][0] + 0.3, cov[0][1], cov[1][1] + 0.3);
}

// Eigen decomposition of the symmetric 2x2 (a, b; b, d): the ellipse axes in pixels.
void ellipseAxes(vec3 cov2D, out vec2 axis1, out vec2 axis2) {
  float a = cov2D.x, b = cov2D.y, d = cov2D.z;
  float det = a * d - b * b;
  float mean = 0.5 * (a + d);
  float dist = max(0.1, sqrt(max(mean * mean - det, 0.0)));
  float lambda1 = mean + dist;
  float lambda2 = mean - dist;
  vec2 e1 = (b == 0.0) ? ((a > d) ? vec2(1, 0) : vec2(0, 1)) : normalize(vec2(b, d - lambda2));
  vec2 e2 = vec2(e1.y, -e1.x);
  axis1 = e1 * sqrt(lambda1);
  axis2 = e2 * sqrt(max(lambda2, 0.0));
}

void main() {
  uint index = order[gl_InstanceIndex];
  Splat s = splats[index];

  vec4 viewPos4 = cam.view * vec4(s.positionAlpha.xyz, 1.0);
  vec3 viewPos = viewPos4.xyz;
  // Behind the camera: emit a vertex outside clip space so the quad is discarded.
  if (viewPos.z >= 0.0) {
    gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
    return;
  }

  vec4 clip = cam.proj * viewPos4;
  float bounds = 1.2 * clip.w;
  if (clip.z < 0.0 || clip.z > clip.w ||
      clip.x < -bounds || clip.x > bounds || clip.y < -bounds || clip.y > bounds) {
    gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
    return;
  }

  vec3 cov2D = projectCovariance(viewPos, s.covA, s.covB);
  vec2 axis1, axis2;
  ellipseAxes(cov2D, axis1, axis2);

  // Draw only out to where this splat's contribution drops below 1/255, which is
  // where the fragment stage would discard anyway: exp(-r^2 / 2) * alpha = 1 / 255.
  // Faint splats, the majority, get a much smaller quad; opaque ones keep 3 sigma.
  float alpha = s.positionAlpha.w;
  float radius = min(kBoundsRadius, sqrt(2.0 * log(max(alpha * 255.0, 1.0))));

  vec2 corner = kCorners[gl_VertexIndex];
  vec2 delta = (corner.x * axis1 + corner.y * axis2) * 2.0 * radius / cam.screenSize;
  gl_Position = vec4(clip.xy + delta * clip.w, clip.z, clip.w);
  relativePosition = radius * corner;

  vec4 rgba = unpackUnorm4x8(s.rgba8);
  vec3 rgb = rgba.rgb;
  if (cam.outputLinear == 1u) rgb = pow(rgb, vec3(2.2));
  color = vec4(rgb, alpha);
}
