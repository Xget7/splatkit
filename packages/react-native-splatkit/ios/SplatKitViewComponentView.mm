#import "SplatKitViewComponentView.h"
#import "SplatKitRNView.h"
#include <react/renderer/components/SplatKitSpec/ComponentDescriptors.h>
#include <react/renderer/components/SplatKitSpec/EventEmitters.h>
#include <react/renderer/components/SplatKitSpec/Props.h>

using namespace facebook::react;

@interface SplatKitViewComponentView ()
@property(nonatomic, strong) SplatKitRNView *splatView;
@end

@implementation SplatKitViewComponentView {
  // Fabric mounts with updateProps before updateEventEmitter, so world and policy work waits for
  // finalizeUpdates; otherwise the capability and policy events of the first engine are dropped.
  BOOL _worldChanged;
  BOOL _policyChanged;
  BOOL _colliderChanged;
}

+ (ComponentDescriptorProvider)componentDescriptorProvider {
  return concreteComponentDescriptorProvider<SplatKitViewComponentDescriptor>();
}
+ (std::vector<ComponentDescriptorProvider>)supplementalComponentDescriptorProviders { return {}; }

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    // Until the first updateProps the view answers from these, and the base class reads the
    // concrete type to tell a configured subclass from a plain view: a debug build asserts on
    // the ViewProps its own constructor leaves behind.
    static const auto defaultProps = std::make_shared<const SplatKitViewProps>();
    _props = defaultProps;
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
  _splatView.colliderEvent = ^(NSDictionary *event) {
    if (!emitter) return;
    SplatKitViewEventEmitter::OnColliderEvent value;
    value.requestId = [event[@"requestId"] UTF8String];
    value.phase = [event[@"phase"] isEqualToString:@"ready"]
        ? SplatKitViewEventEmitter::OnColliderEventPhase::Ready
        : SplatKitViewEventEmitter::OnColliderEventPhase::Failed;
    value.errorCode = [event[@"errorCode"] UTF8String];
    value.message = [event[@"message"] UTF8String];
    emitter->onColliderEvent(value);
  };
  _splatView.cameraPoseEvent = ^(NSDictionary *event) {
    if (!emitter) return;
    SplatKitViewEventEmitter::OnCameraPose value;
    value.x = [event[@"x"] doubleValue]; value.y = [event[@"y"] doubleValue];
    value.z = [event[@"z"] doubleValue]; value.yaw = [event[@"yaw"] doubleValue];
    value.pitch = [event[@"pitch"] doubleValue];
    emitter->onCameraPose(value);
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
    value.policyRasterMask = [event[@"policyRasterMask"] intValue];
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
  _splatView.linearBlending = next.linearBlending;
  _splatView.cullMarginDegrees = next.cullMarginDegrees;
  _splatView.touchLookEnabled = next.touchLookEnabled;
  _splatView.lookSensitivity = next.lookSensitivity;
  _splatView.cameraPoseInterval = next.cameraPoseInterval;
  if (!previous || next.motionEnabled != previous->motionEnabled) {
    _splatView.motionEnabled = next.motionEnabled;
  }
  // A zero eye height is an absent character prop; the native default stands.
  if (next.character.eyeHeight > 0 &&
      (!previous || next.character.eyeHeight != previous->character.eyeHeight ||
       next.character.bodyRadius != previous->character.bodyRadius ||
       next.character.stepHeight != previous->character.stepHeight)) {
    SKCharacterSettings character{};
    character.eyeHeight = static_cast<float>(next.character.eyeHeight);
    character.bodyRadius = static_cast<float>(next.character.bodyRadius);
    character.stepHeight = static_cast<float>(next.character.stepHeight);
    [_splatView setCharacter:character];
  }
  // Codegen gives an absent policy revision 0; like Android, 0 or less means no policy.
  if (!previous || next.policy.revision != previous->policy.revision) {
    // Value initialized: the props carry no lodSplatLimit, and an indeterminate one starves
    // the selector to that many splats. Zero is the engine's own default, meaning no limit.
    SKRenderPolicy policy{};
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
    _policyChanged = YES;
  }
  if (!previous || next.world.requestId != previous->world.requestId) _worldChanged = YES;
  if (!previous || next.collider.requestId != previous->collider.requestId) _colliderChanged = YES;
}

- (void)finalizeUpdates:(RNComponentViewUpdateMask)updateMask {
  [super finalizeUpdates:updateMask];
  const BOOL worldChanged = _worldChanged, policyChanged = _policyChanged;
  const BOOL colliderChanged = _colliderChanged;
  _worldChanged = NO;
  _policyChanged = NO;
  _colliderChanged = NO;
  // Before the world: a load rebuilds walk mode on the engine it creates.
  if (colliderChanged) {
    const auto &collider = std::static_pointer_cast<const SplatKitViewProps>(_props)->collider;
    [_splatView loadCollider:[NSString stringWithUTF8String:collider.filePath.c_str()]
                   requestId:[NSString stringWithUTF8String:collider.requestId.c_str()]];
  }
  if (worldChanged) {
    const auto &world = std::static_pointer_cast<const SplatKitViewProps>(_props)->world;
    if (world.requestId.empty()) {
      // An empty request ID means no world; release the current one like Android's null world.
      [_splatView dispose];
    } else {
      // The engine this load creates applies the stored policy, so it is applied once.
      [_splatView loadWorld:[NSString stringWithUTF8String:world.filePath.c_str()]
                   requestId:[NSString stringWithUTF8String:world.requestId.c_str()]
               maxShDegree:world.maxShDegree lodCapacity:world.lodCapacitySplats
         residencyCapacity:world.residencyCapacitySplats];
      return;
    }
  }
  if (policyChanged) [_splatView applyStoredPolicy];
}

- (void)handleCommand:(const NSString *)commandName args:(const NSArray *)args {
  if ([commandName isEqualToString:@"setWalkVelocity"] && args.count == 2) {
    [_splatView setWalkVelocityForward:[args[0] floatValue] right:[args[1] floatValue]];
  } else if ([commandName isEqualToString:@"look"] && args.count == 2) {
    [_splatView lookWithDeltaYaw:[args[0] floatValue] deltaPitch:[args[1] floatValue]];
  } else if ([commandName isEqualToString:@"setCameraPose"] && args.count == 5) {
    SKCameraPose pose;
    pose.x = [args[0] floatValue]; pose.y = [args[1] floatValue]; pose.z = [args[2] floatValue];
    pose.yaw = [args[3] floatValue]; pose.pitch = [args[4] floatValue];
    [_splatView setPose:pose];
  }
}

- (void)prepareForRecycle {
  [super prepareForRecycle];
  _worldChanged = NO;
  _policyChanged = NO;
  _colliderChanged = NO;
  _splatView.worldEvent = nil;
  _splatView.statsEvent = nil;
  _splatView.policyEvent = nil;
  _splatView.capabilitiesEvent = nil;
  _splatView.colliderEvent = nil;
  _splatView.cameraPoseEvent = nil;
  // A recycled view serves another component next; keep no engine, world or policy.
  [_splatView recycle];
}
- (void)invalidate {
  _splatView.worldEvent = nil;
  _splatView.statsEvent = nil;
  _splatView.policyEvent = nil;
  _splatView.capabilitiesEvent = nil;
  _splatView.colliderEvent = nil;
  _splatView.cameraPoseEvent = nil;
  [_splatView dispose];
  [super invalidate];
}
@end
