#include "splat/formats/GlbDecoder.h"

#include <cstring>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "splat/math/Mat4.h"

namespace splat {
namespace {

using json = nlohmann::json;

constexpr uint32_t kGlbMagic = 0x46546C67;  // "glTF"
constexpr uint32_t kChunkJson = 0x4E4F534A;
constexpr uint32_t kChunkBin = 0x004E4942;

uint32_t readU32(const std::uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

struct Accessor {
  const std::uint8_t* data = nullptr;
  std::size_t count = 0;
  std::size_t stride = 0;
  int componentType = 0;
  std::string type;
};

// Resolves accessor -> bufferView -> BIN chunk. Returns false when anything is out of bounds.
bool resolve(const json& doc, int accessorIndex, const std::uint8_t* bin, std::size_t binSize,
             Accessor& out) {
  const json& accessors = doc["accessors"];
  if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
  const json& a = accessors[static_cast<std::size_t>(accessorIndex)];
  if (!a.contains("bufferView")) return false;
  const int viewIndex = a["bufferView"].get<int>();
  const json& views = doc["bufferViews"];
  if (viewIndex < 0 || viewIndex >= static_cast<int>(views.size())) return false;
  const json& view = views[static_cast<std::size_t>(viewIndex)];
  if (view.value("buffer", 0) != 0) return false;  // only the embedded BIN chunk

  out.componentType = a["componentType"].get<int>();
  out.type = a["type"].get<std::string>();
  out.count = a["count"].get<std::size_t>();
  const int components = out.type == "SCALAR" ? 1 : out.type == "VEC2" ? 2 : out.type == "VEC3" ? 3 : 4;
  const std::size_t componentSize = out.componentType == 5126 ? 4 : out.componentType == 5125 ? 4 : out.componentType == 5123 ? 2 : 1;
  const std::size_t elementSize = static_cast<std::size_t>(components) * componentSize;
  out.stride = view.value("byteStride", 0u);
  if (out.stride == 0) out.stride = elementSize;

  const std::size_t offset = view.value("byteOffset", 0u) + a.value("byteOffset", 0u);
  const std::size_t needed = out.count == 0 ? 0 : offset + (out.count - 1) * out.stride + elementSize;
  if (needed > binSize) return false;
  out.data = bin + offset;
  return true;
}

Mat4 nodeTransform(const json& node) {
  if (node.contains("matrix")) {
    Mat4 m;
    for (std::size_t i = 0; i < 16; ++i) m.m[i] = node["matrix"][i].get<float>();
    return m;  // glTF matrices are column major, like ours
  }
  Mat4 t = Mat4::identity(), r = Mat4::identity(), s = Mat4::identity();
  if (node.contains("translation")) {
    const json& v = node["translation"];
    t = Mat4::translation({v[0].get<float>(), v[1].get<float>(), v[2].get<float>()});
  }
  if (node.contains("rotation")) {
    const json& q = node["rotation"];
    const float x = q[0].get<float>(), y = q[1].get<float>(), z = q[2].get<float>(), w = q[3].get<float>();
    r.at(0, 0) = 1 - 2 * (y * y + z * z); r.at(0, 1) = 2 * (x * y - w * z);     r.at(0, 2) = 2 * (x * z + w * y);
    r.at(1, 0) = 2 * (x * y + w * z);     r.at(1, 1) = 1 - 2 * (x * x + z * z); r.at(1, 2) = 2 * (y * z - w * x);
    r.at(2, 0) = 2 * (x * z - w * y);     r.at(2, 1) = 2 * (y * z + w * x);     r.at(2, 2) = 1 - 2 * (x * x + y * y);
  }
  if (node.contains("scale")) {
    const json& v = node["scale"];
    s.at(0, 0) = v[0].get<float>(); s.at(1, 1) = v[1].get<float>(); s.at(2, 2) = v[2].get<float>();
  }
  return t * r * s;
}

}  // namespace

Result<TriangleMesh> decodeGlb(const std::uint8_t* data, std::size_t size,
                               const GlbDecodeOptions& options) {
  if (size < 12 || readU32(data) != kGlbMagic) {
    return Error{ErrorCode::unsupportedFormat, "not a GLB file"};
  }
  if (readU32(data + 4) != 2) {
    return Error{ErrorCode::unsupportedFormat, "GLB version other than 2"};
  }
  const std::size_t total = std::min<std::size_t>(readU32(data + 8), size);

  const std::uint8_t* jsonChunk = nullptr;
  std::size_t jsonSize = 0;
  const std::uint8_t* bin = nullptr;
  std::size_t binSize = 0;
  std::size_t offset = 12;
  while (offset + 8 <= total) {
    const uint32_t chunkLength = readU32(data + offset);
    const uint32_t chunkType = readU32(data + offset + 4);
    offset += 8;
    if (offset + chunkLength > total) return Error{ErrorCode::corrupt, "GLB chunk exceeds file"};
    if (chunkType == kChunkJson) { jsonChunk = data + offset; jsonSize = chunkLength; }
    else if (chunkType == kChunkBin) { bin = data + offset; binSize = chunkLength; }
    offset += chunkLength;
  }
  if (jsonChunk == nullptr) return Error{ErrorCode::corrupt, "GLB without JSON chunk"};

  json doc = json::parse(jsonChunk, jsonChunk + jsonSize, nullptr, false);
  if (doc.is_discarded()) return Error{ErrorCode::corrupt, "GLB JSON does not parse"};
  if (doc.contains("extensionsRequired") && !doc["extensionsRequired"].empty()) {
    return Error{ErrorCode::unsupportedFormat, "GLB requires extensions"};
  }
  if (!doc.contains("meshes") || !doc.contains("accessors") || !doc.contains("bufferViews")) {
    return Error{ErrorCode::corrupt, "GLB without meshes"};
  }

  // Frame conversion: RDF -> RUB negates Y and Z, applied on top of every node transform.
  Mat4 frame = Mat4::identity();
  if (options.sourceFrame == CoordinateFrame::rdf) {
    frame.at(1, 1) = -1;
    frame.at(2, 2) = -1;
  }

  TriangleMesh mesh;
  bool corrupt = false;
  auto appendMesh = [&](int meshIndex, const Mat4& transform) {
    const json& meshes = doc["meshes"];
    if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes.size())) { corrupt = true; return; }
    for (const json& prim : meshes[static_cast<std::size_t>(meshIndex)].value("primitives", json::array())) {
      if (prim.value("mode", 4) != 4) continue;  // triangles only
      if (!prim.contains("attributes") || !prim["attributes"].contains("POSITION")) continue;
      Accessor pos;
      if (!resolve(doc, prim["attributes"]["POSITION"].get<int>(), bin, binSize, pos) ||
          pos.componentType != 5126 || pos.type != "VEC3") { corrupt = true; return; }

      const uint32_t base = static_cast<uint32_t>(mesh.vertexCount());
      for (std::size_t i = 0; i < pos.count; ++i) {
        float v[3];
        std::memcpy(v, pos.data + i * pos.stride, sizeof(v));
        const Vec3 p = transform.transformPoint({v[0], v[1], v[2]});
        mesh.positions.push_back(p.x);
        mesh.positions.push_back(p.y);
        mesh.positions.push_back(p.z);
      }
      if (prim.contains("indices")) {
        Accessor idx;
        if (!resolve(doc, prim["indices"].get<int>(), bin, binSize, idx) || idx.type != "SCALAR") { corrupt = true; return; }
        for (std::size_t i = 0; i < idx.count; ++i) {
          const std::uint8_t* p = idx.data + i * idx.stride;
          uint32_t value = 0;
          if (idx.componentType == 5125) std::memcpy(&value, p, 4);
          else if (idx.componentType == 5123) { uint16_t v16; std::memcpy(&v16, p, 2); value = v16; }
          else value = *p;
          if (value >= pos.count) { corrupt = true; return; }
          mesh.indices.push_back(base + value);
        }
      } else {
        for (uint32_t i = 0; i < pos.count; ++i) mesh.indices.push_back(base + i);
      }
    }
  };

  // Walk the scene graph so node transforms apply; fall back to every mesh when there is none.
  std::function<void(int, const Mat4&)> visit = [&](int nodeIndex, const Mat4& parent) {
    const json& nodes = doc["nodes"];
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size())) { corrupt = true; return; }
    const json& node = nodes[static_cast<std::size_t>(nodeIndex)];
    const Mat4 world = parent * nodeTransform(node);
    if (node.contains("mesh")) appendMesh(node["mesh"].get<int>(), world);
    for (const json& child : node.value("children", json::array())) visit(child.get<int>(), world);
  };
  if (doc.contains("scenes") && !doc["scenes"].empty()) {
    const std::size_t sceneIndex = doc.value("scene", 0u);
    for (const json& root : doc["scenes"][sceneIndex].value("nodes", json::array())) visit(root.get<int>(), frame);
  } else {
    for (int i = 0; i < static_cast<int>(doc["meshes"].size()); ++i) appendMesh(i, frame);
  }

  if (corrupt) return Error{ErrorCode::corrupt, "GLB geometry references are invalid"};
  if (mesh.triangleCount() == 0) return Error{ErrorCode::corrupt, "GLB has no triangles"};
  return mesh;
}

}  // namespace splat
