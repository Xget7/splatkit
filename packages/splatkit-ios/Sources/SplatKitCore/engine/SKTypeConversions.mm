#include "engine/SKTypeConversions.h"

namespace splatkit {

SKRenderPolicy fromCppPolicy(const RenderPolicy& p) {
  SKRenderPolicy out{};
  out.raster = static_cast<uint32_t>(p.raster);
  out.tileSize = p.tileSize;
  out.lodErrorPixels = p.lodErrorPixels;
  out.alphaThreshold = p.alphaThreshold;
  out.subpixelThreshold = p.subpixelThreshold;
  out.enableFrustumCulling = p.enableFrustumCulling;
  out.enableHiZOcclusion = p.enableHiZOcclusion;
  out.enableEarlyTermination = p.enableEarlyTermination;
  out.sortDepth = static_cast<uint32_t>(p.sortDepth);
  out.lodSplatLimit = p.lodSplatLimit;
  return out;
}

bool toCppPolicy(SKRenderPolicy p, RenderPolicy* out) {
  if (p.sortDepth != 16 && p.sortDepth != 32) return false;
  out->raster = static_cast<RasterStrategy>(p.raster);
  out->tileSize = p.tileSize;
  out->lodErrorPixels = p.lodErrorPixels;
  out->alphaThreshold = p.alphaThreshold;
  out->subpixelThreshold = p.subpixelThreshold;
  out->enableFrustumCulling = p.enableFrustumCulling == YES;
  out->enableHiZOcclusion = p.enableHiZOcclusion == YES;
  out->enableEarlyTermination = p.enableEarlyTermination == YES;
  out->sortDepth = p.sortDepth == 16 ? SortKeyBits::low16 : SortKeyBits::full32;
  out->lodSplatLimit = p.lodSplatLimit;
  return true;
}

SKRenderPolicySupport fromCppSupport(const RenderPolicySupport& s) {
  SKRenderPolicySupport out{};
  out.raster = s.raster;
  out.tileSize = s.tileSize;
  out.lodErrorPixels = s.lodErrorPixels;
  out.alphaThreshold = s.alphaThreshold;
  out.subpixelThreshold = s.subpixelThreshold;
  out.enableFrustumCulling = s.enableFrustumCulling;
  out.enableHiZOcclusion = s.enableHiZOcclusion;
  out.enableEarlyTermination = s.enableEarlyTermination;
  out.sortDepth = s.sortDepth;
  out.rasterMask = s.rasterMask;
  out.tileSizeMask = s.tileSizeMask;
  out.minLodErrorPixels = s.minLodErrorPixels;
  out.maxLodErrorPixels = s.maxLodErrorPixels;
  out.minSubpixelThreshold = s.minSubpixelThreshold;
  out.maxSubpixelThreshold = s.maxSubpixelThreshold;
  out.lodSplatLimit = s.lodSplatLimit;
  return out;
}

LensSettings lensSettings(const SKLensSettings& s) {
  LensSettings out;
  if (s.hasFieldOfView) out.fieldOfViewDegrees = s.fieldOfViewDegrees;
  if (s.hasDepthRange) out.depthRange = DepthRange{s.nearPlane, s.farPlane};
  return out;
}

}  // namespace splatkit
