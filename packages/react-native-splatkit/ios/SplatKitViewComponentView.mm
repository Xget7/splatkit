#import "SplatKitViewComponentView.h"
#import "SplatKitRNView.h"
#include <react/renderer/components/SplatKitSpec/ComponentDescriptors.h>
#include <react/renderer/components/SplatKitSpec/EventEmitters.h>
#include <react/renderer/components/SplatKitSpec/Props.h>

using namespace facebook::react;

@interface SplatKitViewComponentView ()
@property(nonatomic, strong) SplatKitRNView *splatView;
@end

/// The device's capabilities from one throwaway engine, since the view has no engine until a
/// world loads. Absent on a device without Metal, where no policy can apply anyway.
static const SKDeviceCapabilities *DeviceCapabilities() {
  static SKDeviceCapabilities capabilities;
  static const SKDeviceCapabilities *available = nullptr;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    SKSplatEngine *probe = [SKSplatEngine create];
    if (probe == nil) return;
    capabilities = probe.deviceCapabilities;
    available = &capabilities;
  });
  return available;
}

@implementation SplatKitViewComponentView {
  // Fabric mounts with updateProps before updateEventEmitter, so world and policy work waits for
  // finalizeUpdates; otherwise the capability and policy events of the first engine are dropped.
  BOOL _worldChanged;
  BOOL _policyChanged;
  BOOL _colliderChanged;
  BOOL _cameraChanged;
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
  _splatView.cameraEvent = ^(NSDictionary *event) {
    if (!emitter) return;
    SplatKitViewEventEmitter::OnCameraEvent value;
    value.revision = [event[@"revision"] intValue];
    value.mode = [event[@"mode"] intValue];
    value.hasAnchor = [event[@"hasAnchor"] boolValue];
    value.phase = [event[@"phase"] isEqualToString:@"applied"]
        ? SplatKitViewEventEmitter::OnCameraEventPhase::Applied
        : SplatKitViewEventEmitter::OnCameraEventPhase::Rejected;
    value.errorCode = [event[@"errorCode"] UTF8String];
    value.message = [event[@"message"] UTF8String];
    value.anchorX = [event[@"anchorX"] doubleValue];
    value.anchorY = [event[@"anchorY"] doubleValue];
    value.anchorZ = [event[@"anchorZ"] doubleValue];
    value.radius = [event[@"radius"] doubleValue];
    value.azimuth = [event[@"azimuth"] doubleValue];
    value.elevation = [event[@"elevation"] doubleValue];
    value.orbitRadiansPerSecond = [event[@"orbitRadiansPerSecond"] doubleValue];
    emitter->onCameraEvent(value);
  };
  _splatView.cameraPoseEvent = ^(NSDictionary *event) {
    if (!emitter) return;
    SplatKitViewEventEmitter::OnCameraPose value;
    value.x = [event[@"x"] doubleValue]; value.y = [event[@"y"] doubleValue];
    value.z = [event[@"z"] doubleValue]; value.yaw = [event[@"yaw"] doubleValue];
    value.pitch = [event[@"pitch"] doubleValue];
    emitter->onCameraPose(value);
  };
  _splatView.focusResultEvent = ^(NSDictionary *event) {
    if (!emitter) return;
    SplatKitViewEventEmitter::OnFocusResult value;
    value.requestId = [event[@"requestId"] UTF8String];
    value.hit = [event[@"hit"] boolValue];
    emitter->onFocusResult(value);
  };
  _splatView.capabilitiesEvent = ^(NSDictionary *event) {
    if (!emitter) return;
    SplatKitViewEventEmitter::OnCapabilities value;
    value.maxLodCapacitySplats = [event[@"maxLodCapacitySplats"] intValue];
    value.minResidencyCapacitySplats = [event[@"minResidencyCapacitySplats"] intValue];
    value.maxResidencyCapacitySplats = [event[@"maxResidencyCapacitySplats"] intValue];
    value.supportsHiZOcclusion = [event[@"supportsHiZOcclusion"] boolValue];
    value.supportsSubgroups = [event[@"supportsSubgroups"] boolValue];
    value.maxTextureDimension = [event[@"maxTextureDimension"] intValue];
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
  _splatView.maxFramesPerSecond = next.maxFramesPerSecond;
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
    // A field this backend cannot apply keeps the backend's own value, so it raises no fallback
    // warning for a request the host never made.
    if (const SKDeviceCapabilities *device = DeviceCapabilities()) {
      policy = device->fallback;
      const SKRenderPolicySupport &support = device->policy;
      if (support.lodErrorPixels) policy.lodErrorPixels = static_cast<float>(next.policy.lodErrorPixels);
      if (support.alphaThreshold) policy.alphaThreshold = static_cast<float>(next.policy.alphaThreshold);
      if (support.subpixelThreshold) policy.subpixelThreshold = static_cast<float>(next.policy.subpixelThreshold);
      if (support.enableFrustumCulling) policy.enableFrustumCulling = next.policy.enableFrustumCulling;
      if (support.enableHiZOcclusion) policy.enableHiZOcclusion = next.policy.enableHiZOcclusion;
      if (support.enableEarlyTermination) policy.enableEarlyTermination = next.policy.enableEarlyTermination;
      if (support.sortDepth) policy.sortDepth = static_cast<uint32_t>(next.policy.sortDepth);
    }
    [_splatView setPolicy:policy revision:next.policy.revision];
    _policyChanged = YES;
  }
  if ((!previous || next.camera.revision != previous->camera.revision) && next.camera.revision != 0) {
    SKCameraRequest camera{};
    camera.mode = next.camera.mode;
    camera.anchor = {static_cast<float>(next.camera.anchorX),
                     static_cast<float>(next.camera.anchorY),
                     static_cast<float>(next.camera.anchorZ)};
    camera.radius = static_cast<float>(next.camera.radius);
    camera.azimuth = static_cast<float>(next.camera.azimuth);
    camera.elevation = static_cast<float>(next.camera.elevation);
    camera.orbitRadiansPerSecond = static_cast<float>(next.camera.orbitRadiansPerSecond);
    _cameraChanged = [_splatView setCameraRequest:camera revision:next.camera.revision] || _cameraChanged;
  }
  if (!previous || next.world.requestId != previous->world.requestId) _worldChanged = YES;
  if (!previous || next.collider.requestId != previous->collider.requestId) _colliderChanged = YES;
}

- (void)finalizeUpdates:(RNComponentViewUpdateMask)updateMask {
  [super finalizeUpdates:updateMask];
  const BOOL worldChanged = _worldChanged, policyChanged = _policyChanged;
  const BOOL colliderChanged = _colliderChanged;
  const BOOL cameraChanged = _cameraChanged;
  _cameraChanged = NO;
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
      // Absent lens fields arrive as zero. One plane without the other is still a range,
      // which the engine refuses.
      SKLensSettings lens{};
      lens.hasFieldOfView = world.fieldOfViewDegrees != 0;
      lens.fieldOfViewDegrees = static_cast<float>(world.fieldOfViewDegrees);
      lens.hasDepthRange = world.nearPlane != 0 || world.farPlane != 0;
      lens.nearPlane = static_cast<float>(world.nearPlane);
      lens.farPlane = static_cast<float>(world.farPlane);
      // An absent frame arrives empty, and nil keeps each file's own.
      NSString *sourceFrame = world.sourceFrame.empty()
          ? nil : [NSString stringWithUTF8String:world.sourceFrame.c_str()];
      // The engine this load creates applies the stored policy, so it is applied once.
      [_splatView loadWorld:[NSString stringWithUTF8String:world.filePath.c_str()]
                   requestId:[NSString stringWithUTF8String:world.requestId.c_str()]
                        kind:world.kind
                 maxShDegree:world.maxShDegree
                 sourceFrame:sourceFrame
                 lodCapacity:world.lodCapacitySplats
           residencyCapacity:world.residencyCapacitySplats
                        lens:lens];
      return;
    }
  }
  if (policyChanged) [_splatView applyStoredPolicy];
  if (cameraChanged) [_splatView applyStoredCamera];
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
  } else if ([commandName isEqualToString:@"lookAt"] && args.count == 9) {
    SKVec3 position = {[args[0] floatValue], [args[1] floatValue], [args[2] floatValue]};
    SKVec3 target = {[args[3] floatValue], [args[4] floatValue], [args[5] floatValue]};
    SKVec3 up = {[args[6] floatValue], [args[7] floatValue], [args[8] floatValue]};
    [_splatView lookAtFrom:position target:target up:up];
  } else if ([commandName isEqualToString:@"setAnchor"] && args.count == 3) {
    SKVec3 point = {[args[0] floatValue], [args[1] floatValue], [args[2] floatValue]};
    [_splatView setAnchor:point];
  } else if ([commandName isEqualToString:@"orbit"] && args.count == 2) {
    [_splatView orbitWithDeltaAzimuth:[args[0] floatValue] deltaElevation:[args[1] floatValue]];
  } else if ([commandName isEqualToString:@"dolly"] && args.count == 1) {
    [_splatView dolly:[args[0] floatValue]];
  } else if ([commandName isEqualToString:@"focus"] && args.count == 3) {
    [_splatView focusRequestId:args[0] x:[args[1] floatValue] y:[args[2] floatValue]];
  } else if ([commandName isEqualToString:@"animateOrbit"] && args.count == 3) {
    [_splatView animateOrbitDegrees:[args[0] floatValue] degreesPerSecond:[args[1] floatValue]
                        easeInOut:[args[2] boolValue]];
  }
}

- (void)prepareForRecycle {
  [super prepareForRecycle];
  _cameraChanged = NO;
  _worldChanged = NO;
  _policyChanged = NO;
  _colliderChanged = NO;
  _splatView.worldEvent = nil;
  _splatView.statsEvent = nil;
  _splatView.policyEvent = nil;
  _splatView.capabilitiesEvent = nil;
  _splatView.colliderEvent = nil;
  _splatView.cameraPoseEvent = nil;
  _splatView.cameraEvent = nil;
  _splatView.focusResultEvent = nil;
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
  _splatView.cameraEvent = nil;
  _splatView.focusResultEvent = nil;
  [_splatView dispose];
  [super invalidate];
}
@end
