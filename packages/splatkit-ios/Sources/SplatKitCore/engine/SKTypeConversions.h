#pragma once

#import "SplatKit/SKSplatEngine.h"

#include "splatkit/camera/Lens.h"
#include "splatkit/rendering/RenderPolicy.h"

namespace splatkit {

// Bridge between the C++ policy types and their C mirror imported by Swift.
SKRenderPolicy fromCppPolicy(const RenderPolicy& p);
// False, with `out` untouched, when `p.sortDepth` is neither 16 nor 32.
bool toCppPolicy(SKRenderPolicy p, RenderPolicy* out);
SKRenderPolicySupport fromCppSupport(const RenderPolicySupport& s);
LensSettings lensSettings(const SKLensSettings& s);

}  // namespace splatkit
