#import "SplatKitViewComponentView.h"
#import "SplatKitRNView.h"
#include <react/renderer/components/SplatKitSpec/ComponentDescriptors.h>
#include <react/renderer/components/SplatKitSpec/EventEmitters.h>
#include <react/renderer/components/SplatKitSpec/Props.h>

using namespace facebook::react;

@interface SplatKitViewComponentView ()
@property(nonatomic, strong) SplatKitRNView *splatView;
@end

@implementation SplatKitViewComponentView

+ (ComponentDescriptorProvider)componentDescriptorProvider {
  return concreteComponentDescriptorProvider<SplatKitViewComponentDescriptor>();
}
+ (std::vector<ComponentDescriptorProvider>)supplementalComponentDescriptorProviders { return {}; }

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _splatView = [[SplatKitRNView alloc] initWithFrame:self.bounds];
    [self addSubview:_splatView];
    _splatView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  }
  return self;
}

- (void)updateEventEmitter:(const EventEmitter::Shared &)eventEmitter {
  [super updateEventEmitter:eventEmitter];
  auto emitter = std::dynamic_pointer_cast<const SplatKitViewEventEmitter>(eventEmitter);
  __weak SplatKitViewComponentView *weakSelf = self;
  _splatView.worldEvent = ^(NSDictionary *event) {
    auto strong = weakSelf;
    if (!strong || !emitter) return;
    SplatKitViewEventEmitter::OnWorldEvent value;
    value.requestId = [event[@"requestId"] UTF8String];
    NSString *phase = event[@"phase"];
    value.phase = [phase isEqualToString:@"uploaded"]
        ? SplatKitViewEventEmitter::OnWorldEventPhase::Uploaded
        : [phase isEqualToString:@"frameReady"]
            ? SplatKitViewEventEmitter::OnWorldEventPhase::FrameReady
            : SplatKitViewEventEmitter::OnWorldEventPhase::Failed;
    value.loadedSplats = [event[@"loadedSplats"] doubleValue];
    value.errorCode = [event[@"errorCode"] UTF8String]; value.message = [event[@"message"] UTF8String];
    emitter->onWorldEvent(value);
  };
  _splatView.statsEvent = ^(NSDictionary *event) {
    if (!emitter) return;
    SplatKitViewEventEmitter::OnStats value;
    value.requestId = [event[@"requestId"] UTF8String]; value.loadedSplats = [event[@"loadedSplats"] doubleValue];
    value.drawnSplats = [event[@"drawnSplats"] doubleValue]; value.frameMillis = [event[@"frameMillis"] doubleValue];
    value.frameTimingAvailable = [event[@"frameTimingAvailable"] boolValue]; value.gpuMillis = [event[@"gpuMillis"] doubleValue];
    value.gpuTimingAvailable = [event[@"gpuTimingAvailable"] boolValue]; value.sortMillis = [event[@"sortMillis"] doubleValue];
    value.sortTimingAvailable = [event[@"sortTimingAvailable"] boolValue]; emitter->onStats(value);
  };
  _splatView.policyEvent = ^(NSDictionary *event) {
    if (!emitter) return;
    SplatKitViewEventEmitter::OnPolicyEvent value;
    value.revision = [event[@"revision"] intValue];
    NSString *phase = event[@"phase"];
    value.phase = [phase isEqualToString:@"applied"] ? SplatKitViewEventEmitter::OnPolicyEventPhase::Applied
        : [phase isEqualToString:@"warning"] ? SplatKitViewEventEmitter::OnPolicyEventPhase::Warning
                                             : SplatKitViewEventEmitter::OnPolicyEventPhase::Rejected;
    value.errorCode = [event[@"errorCode"] UTF8String];
    value.message = [event[@"message"] UTF8String];
    value.raster = [event[@"raster"] intValue]; value.tileSize = [event[@"tileSize"] intValue];
    value.lodErrorPixels = [event[@"lodErrorPixels"] doubleValue];
    value.alphaThreshold = [event[@"alphaThreshold"] doubleValue];
    value.subpixelThreshold = [event[@"subpixelThreshold"] doubleValue];
    value.enableFrustumCulling = [event[@"enableFrustumCulling"] boolValue];
    value.enableHiZOcclusion = [event[@"enableHiZOcclusion"] boolValue];
    value.enableEarlyTermination = [event[@"enableEarlyTermination"] boolValue];
    value.sortDepth = [event[@"sortDepth"] intValue];
    emitter->onPolicyEvent(value);
  };
  _splatView.capabilitiesEvent = ^(NSDictionary *event) {
    if (!emitter) return;
    SplatKitViewEventEmitter::OnCapabilities value;
    value.maxLodCapacitySplats = [event[@"maxLodCapacitySplats"] intValue];
    value.minResidencyCapacitySplats = [event[@"minResidencyCapacitySplats"] intValue];
    value.maxResidencyCapacitySplats = [event[@"maxResidencyCapacitySplats"] intValue];
    value.supportsComputeTiles = [event[@"supportsComputeTiles"] boolValue];
    value.supportsHiZOcclusion = [event[@"supportsHiZOcclusion"] boolValue];
    value.supportsSubgroups = [event[@"supportsSubgroups"] boolValue];
    value.maxTextureDimension = [event[@"maxTextureDimension"] intValue];
    value.policyRaster = [event[@"policyRaster"] boolValue];
    value.policyTileSize = [event[@"policyTileSize"] boolValue];
    value.policyLodErrorPixels = [event[@"policyLodErrorPixels"] boolValue];
    value.policyAlphaThreshold = [event[@"policyAlphaThreshold"] boolValue];
    value.policySubpixelThreshold = [event[@"policySubpixelThreshold"] boolValue];
    value.policyEnableFrustumCulling = [event[@"policyEnableFrustumCulling"] boolValue];
    value.policyEnableHiZOcclusion = [event[@"policyEnableHiZOcclusion"] boolValue];
    value.policyEnableEarlyTermination = [event[@"policyEnableEarlyTermination"] boolValue];
    value.policySortDepth = [event[@"policySortDepth"] boolValue];
    emitter->onCapabilities(value);
  };
}

- (void)updateProps:(const Props::Shared &)props oldProps:(const Props::Shared &)oldProps {
  [super updateProps:props oldProps:oldProps];
  auto next = *std::static_pointer_cast<const SplatKitViewProps>(props);
  auto previous = oldProps ? std::static_pointer_cast<const SplatKitViewProps>(oldProps) : nullptr;
  _splatView.paused = next.paused; _splatView.renderScale = next.renderScale; _splatView.shDegree = next.shDegree;
  // Codegen gives an absent policy revision 0; like Android, 0 or less means no policy.
  const bool policyChanged = !previous || next.policy.revision != previous->policy.revision;
  if (policyChanged) {
    SKRenderPolicy policy;
    policy.raster = static_cast<uint32_t>(next.policy.raster);
    policy.tileSize = static_cast<uint32_t>(next.policy.tileSize);
    policy.lodErrorPixels = static_cast<float>(next.policy.lodErrorPixels);
    policy.alphaThreshold = static_cast<float>(next.policy.alphaThreshold);
    policy.subpixelThreshold = static_cast<float>(next.policy.subpixelThreshold);
    policy.enableFrustumCulling = next.policy.enableFrustumCulling;
    policy.enableHiZOcclusion = next.policy.enableHiZOcclusion;
    policy.enableEarlyTermination = next.policy.enableEarlyTermination;
    policy.sortDepth = static_cast<uint32_t>(next.policy.sortDepth);
    [_splatView setPolicy:policy revision:next.policy.revision];
  }
  if (!previous || next.world.requestId != previous->world.requestId) {
    if (next.world.requestId.empty()) {
      // An empty request ID means no world; release the current one like Android's null world.
      [_splatView dispose];
    } else {
      // The engine this load creates applies the stored policy, so it is applied once.
      [_splatView loadWorld:[NSString stringWithUTF8String:next.world.filePath.c_str()]
                   requestId:[NSString stringWithUTF8String:next.world.requestId.c_str()]
               maxShDegree:next.world.maxShDegree lodCapacity:next.world.lodCapacitySplats
         residencyCapacity:next.world.residencyCapacitySplats];
      return;
    }
  }
  if (policyChanged) [_splatView applyStoredPolicy];
}

- (void)prepareForRecycle {
  [super prepareForRecycle];
  _splatView.worldEvent = nil;
  _splatView.statsEvent = nil;
  _splatView.policyEvent = nil;
  _splatView.capabilitiesEvent = nil;
  // A recycled view serves another component next; keep no engine, world or policy.
  [_splatView recycle];
}
- (void)invalidate {
  _splatView.worldEvent = nil;
  _splatView.statsEvent = nil;
  _splatView.policyEvent = nil;
  _splatView.capabilitiesEvent = nil;
  [_splatView dispose];
  [super invalidate];
}
@end
