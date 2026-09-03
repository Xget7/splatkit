#version 450

// Milestone check: a triangle with no vertex buffers. Positions come from the vertex index.
layout(location = 0) out vec3 fragColor;

const vec2 positions[3] = vec2[](vec2(0.0, -0.6), vec2(0.6, 0.6), vec2(-0.6, 0.6));
const vec3 colors[3] = vec3[](vec3(1.0, 0.3, 0.2), vec3(0.2, 1.0, 0.4), vec3(0.3, 0.4, 1.0));

void main() {
  gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
  fragColor = colors[gl_VertexIndex];
}
