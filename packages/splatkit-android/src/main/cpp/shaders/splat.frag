#version 450

layout(location = 0) in vec2 relativePosition;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 outColor;

const float kBoundsRadiusSquared = 9.0;

void main() {
  float r2 = dot(relativePosition, relativePosition);
  if (r2 > kBoundsRadiusSquared) discard;
  // The Gaussian falloff evaluated at this pixel, scaled by the splat's opacity.
  float alpha = exp(-0.5 * r2) * color.a;
  if (alpha < 1.0 / 255.0) discard;
  outColor = vec4(color.rgb, alpha);
}
