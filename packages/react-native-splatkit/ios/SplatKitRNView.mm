#import "SplatKitRNView.h"
#import <CoreMotion/CoreMotion.h>
#import <QuartzCore/CADisplayLink.h>

#include <atomic>

/// Stable failure codes shared with the Android adapter, so hosts can branch on them.
static NSString *const kErrorInvalidRequest = @"INVALID_REQUEST";
static NSString *const kErrorWorldLoadFailed = @"WORLD_LOAD_FAILED";
static NSString *const kErrorColliderLoadFailed = @"COLLIDER_LOAD_FAILED";
static NSString *const kErrorGpuUnavailable = @"GPU_UNAVAILABLE";
static NSString *const kErrorInvalidPolicy = @"INVALID_POLICY";
static NSString *const kErrorPolicyPreparationFailed = @"POLICY_PREPARATION_FAILED";

/// Stats snapshots leave the render thread at most twice a second.
static const CFTimeInterval kStatsInterval = 0.5;
/// SplatMetalView's touch sensitivity: radians per point dragged.
static const CGFloat kLookSensitivity = 0.004;

@class _SplatKitRNLinkProxy;

@interface SplatKitRNView ()
@property(nonatomic, strong, nullable) CADisplayLink *displayLink;
@property(nonatomic, strong) _SplatKitRNLinkProxy *linkProxy;
@property(nonatomic, copy) NSString *requestId;
@property(nonatomic, getter=isAttached) BOOL attached;
@property(nonatomic, getter=isAppActive) BOOL appActive;
@property(nonatomic) CFTimeInterval lastStatsAt;
@property(nonatomic) CFTimeInterval lastPoseAt;
@property(nonatomic, strong, nullable) CMMotionManager *motionManager;
@property(nonatomic, strong, nullable) NSOperationQueue *motionQueue;
@property(nonatomic, copy, nullable) NSString *colliderRequestId;
@property(nonatomic, copy, nullable) NSString *colliderPath;
- (void)draw:(CADisplayLink *)link;
@end

/// CADisplayLink retains its target, so the link points at this weak proxy instead of
/// the view to avoid a retain cycle that would keep a recycled view (and its engine)
/// alive past `dispose`.
@interface _SplatKitRNLinkProxy : NSObject
@property(nonatomic, weak) SplatKitRNView *target;
@end

@implementation _SplatKitRNLinkProxy
- (void)tick:(CADisplayLink *)link {
  [self.target draw:link];
}
@end

@implementation SplatKitRNView {
  SKSplatEngine *_engine;
  dispatch_queue_t _loaderQueue;
  /// Bumped on every accepted `loadWorld:` and on `dispose`. Each load's callbacks capture
  /// the value they were created with and are dropped once it is no longer current.
  std::atomic<uint64_t> _generation;
  /// The policy the host last requested, re-applied to every engine `loadWorld:` creates.
  SKRenderPolicy _requestedPolicy;
  NSInteger _policyRevision;
  BOOL _hasPolicy;
  SKCharacterSettings _character;
  BOOL _hasCharacter;
  SKCameraPose _lastPose;
  BOOL _hasLastPose;
}

+ (Class)layerClass { return CAMetalLayer.class; }

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.opaque = YES;
    self.backgroundColor = UIColor.blackColor;
    self.appActive = YES;
    self.renderScale = 1;
    self.shDegree = 3;
    self.cullMarginDegrees = 10;
    self.touchLookEnabled = YES;
    self.lookSensitivity = kLookSensitivity;
    _generation = 0;
    _loaderQueue = dispatch_queue_create("com.splatkit.rn.loader", DISPATCH_QUEUE_SERIAL);
    self.linkProxy = [[_SplatKitRNLinkProxy alloc] init];
    self.linkProxy.target = self;
    // Looking is the only touch the view handles; walking comes from the host's own control.
    UIPanGestureRecognizer *look = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(onLook:)];
    look.maximumNumberOfTouches = 1;
    [self addGestureRecognizer:look];
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    [center addObserver:self selector:@selector(appDidEnterBackground)
                   name:UIApplicationDidEnterBackgroundNotification object:nil];
    [center addObserver:self selector:@selector(appWillEnterForeground)
                   name:UIApplicationWillEnterForegroundNotification object:nil];
  }
  return self;
}

- (void)dealloc {
  [NSNotificationCenter.defaultCenter removeObserver:self];
  [self dispose];
}

// MARK: - Lifecycle

- (void)setPaused:(BOOL)paused {
  _paused = paused;
  [self updateDisplayLink];
}

- (void)setRenderScale:(CGFloat)value {
  _renderScale = MIN(MAX(value, 0.1), 2.0);
  [_engine setRenderScale:(float)_renderScale];
}

- (void)setShDegree:(NSInteger)value {
  _shDegree = MIN(MAX(value, 0), 3);
  [_engine setShDegree:(int)_shDegree];
}

- (void)setLinearBlending:(BOOL)value {
  _linearBlending = value;
  [_engine setLinearBlending:value];
}

- (void)setCullMarginDegrees:(CGFloat)value {
  _cullMarginDegrees = MIN(MAX(value, 0), 80);
  [_engine setCullMargin:(float)_cullMarginDegrees];
}

// MARK: - Navigation

// The engine renders on the main thread here, so input drives it directly.
- (void)onLook:(UIPanGestureRecognizer *)gesture {
  const CGPoint d = [gesture translationInView:self];
  [gesture setTranslation:CGPointZero inView:self];
  if (!self.touchLookEnabled) return;
  [_engine lookWithDeltaYaw:-(float)(d.x * self.lookSensitivity)
                 deltaPitch:-(float)(d.y * self.lookSensitivity)];
}

- (void)setWalkVelocityForward:(float)forward right:(float)right {
  [_engine setVelocityForward:forward right:right];
}

- (void)lookWithDeltaYaw:(float)deltaYaw deltaPitch:(float)deltaPitch {
  [_engine lookWithDeltaYaw:deltaYaw deltaPitch:deltaPitch];
}

- (void)setPose:(SKCameraPose)pose {
  _engine.cameraPose = pose;
}

- (BOOL)setCharacter:(SKCharacterSettings)character {
  _character = character;
  _hasCharacter = YES;
  return _engine == nil ? YES : [_engine setCharacter:character];
}

// MARK: - Motion

- (void)setMotionEnabled:(BOOL)enabled {
  _motionEnabled = enabled;
  [_engine setMotionEnabled:enabled];
  if (enabled) {
    [self startMotion];
  } else {
    [self stopMotion];
  }
}

- (void)startMotion {
  if (!self.motionEnabled || !self.isAttached || !self.isAppActive) return;
  if (self.motionManager == nil) {
    self.motionManager = [[CMMotionManager alloc] init];
    self.motionManager.deviceMotionUpdateInterval = 1.0 / 60.0;
    self.motionQueue = [[NSOperationQueue alloc] init];
    self.motionQueue.name = @"com.splatkit.rn.motion";
    self.motionQueue.maxConcurrentOperationCount = 1;
  }
  CMMotionManager *manager = self.motionManager;
  if (!manager.isDeviceMotionAvailable || manager.isDeviceMotionActive) return;
  __weak SplatKitRNView *weakSelf = self;
  [manager startDeviceMotionUpdatesUsingReferenceFrame:CMAttitudeReferenceFrameXArbitraryCorrectedZVertical
                                               toQueue:self.motionQueue
                                           withHandler:^(CMDeviceMotion *motion, NSError *error) {
    if (motion == nil) return;
    const CMRotationMatrix m = motion.attitude.rotationMatrix;
    dispatch_async(dispatch_get_main_queue(), ^{
      SplatKitRNView *view = weakSelf;
      if (view == nil || !view.motionEnabled) return;
      [view applyAttitude:m];
    });
  }];
}

- (void)stopMotion {
  [self.motionManager stopDeviceMotionUpdates];
}

/// CMRotationMatrix maps reference to device; the engine wants its transpose, with the
/// device axes turned so "x right, y up on screen" holds in the interface orientation.
- (void)applyAttitude:(CMRotationMatrix)m {
  const float x[3] = {(float)m.m11, (float)m.m12, (float)m.m13};
  const float y[3] = {(float)m.m21, (float)m.m22, (float)m.m23};
  const float z[3] = {(float)m.m31, (float)m.m32, (float)m.m33};
  float right[3], up[3];
  switch (self.window.windowScene.interfaceOrientation) {
    case UIInterfaceOrientationLandscapeRight:
      for (int i = 0; i < 3; i++) { right[i] = y[i]; up[i] = -x[i]; }
      break;
    case UIInterfaceOrientationLandscapeLeft:
      for (int i = 0; i < 3; i++) { right[i] = -y[i]; up[i] = x[i]; }
      break;
    case UIInterfaceOrientationPortraitUpsideDown:
      for (int i = 0; i < 3; i++) { right[i] = -x[i]; up[i] = -y[i]; }
      break;
    default:
      for (int i = 0; i < 3; i++) { right[i] = x[i]; up[i] = y[i]; }
      break;
  }
  const float rowMajor[9] = {right[0], up[0], z[0], right[1], up[1], z[1], right[2], up[2], z[2]};
  [_engine setAttitude:rowMajor];
}

// MARK: - Policy and capabilities

- (void)setPolicy:(SKRenderPolicy)policy revision:(NSInteger)revision {
  _requestedPolicy = policy;
  _policyRevision = revision;
  _hasPolicy = revision > 0;
}

- (void)applyStoredPolicy {
  if (!_hasPolicy || _engine == nil) return;
  NSString *reason = nil;
  NSArray<NSString *> *warnings = nil;
  const SKRenderPolicyOutcome outcome =
      [_engine applyRenderPolicy:_requestedPolicy reason:&reason warnings:&warnings];
  [self emitPolicyForRevision:_policyRevision outcome:outcome reason:reason warnings:warnings];
}

/// Mirrors the Android adapter: applied, warning (fell back) or rejected (previous kept).
- (void)emitPolicyForRevision:(NSInteger)revision outcome:(SKRenderPolicyOutcome)outcome
                       reason:(NSString *)reason warnings:(NSArray<NSString *> *)warnings {
  if (self.policyEvent == nil || _engine == nil) return;
  const SKRenderPolicy effective = _engine.renderPolicy;
  const BOOL applied = outcome == SKRenderPolicyOutcomeApplied;
  NSString *phase = applied ? (warnings.count > 0 ? @"warning" : @"applied") : @"rejected";
  NSString *code = applied ? @""
      : outcome == SKRenderPolicyOutcomePreparationFailed ? kErrorPolicyPreparationFailed
                                                          : kErrorInvalidPolicy;
  NSString *message = applied ? [warnings componentsJoinedByString:@"; "] : reason;
  self.policyEvent(@{
    @"revision": @(revision),
    @"phase": phase,
    @"errorCode": code,
    @"message": message ?: @"",
    @"raster": @(effective.raster),
    @"tileSize": @(effective.tileSize),
    @"lodErrorPixels": @(effective.lodErrorPixels),
    @"alphaThreshold": @(effective.alphaThreshold),
    @"subpixelThreshold": @(effective.subpixelThreshold),
    @"enableFrustumCulling": @(effective.enableFrustumCulling),
    @"enableHiZOcclusion": @(effective.enableHiZOcclusion),
    @"enableEarlyTermination": @(effective.enableEarlyTermination),
    @"sortDepth": @(effective.sortDepth),
  });
}

- (void)emitCapabilities {
  if (self.capabilitiesEvent == nil || _engine == nil) return;
  const SKDeviceCapabilities caps = _engine.deviceCapabilities;
  self.capabilitiesEvent(@{
    @"maxLodCapacitySplats": @(caps.maxLodCapacitySplats),
    @"minResidencyCapacitySplats": @(caps.minResidencyCapacitySplats),
    @"maxResidencyCapacitySplats": @(caps.maxResidencyCapacitySplats),
    @"supportsComputeTiles": @(caps.supportsComputeTiles),
    @"supportsHiZOcclusion": @(caps.supportsHiZOcclusion),
    @"supportsSubgroups": @(caps.supportsSubgroups),
    @"maxTextureDimension": @(caps.maxTextureDimension),
    @"policyRaster": @(caps.policy.raster),
    @"policyRasterMask": @(caps.policy.raster ? (caps.policy.rasterMask != 0 ? caps.policy.rasterMask : 7u) : 0u),
    @"policyTileSize": @(caps.policy.tileSize),
    @"policyLodErrorPixels": @(caps.policy.lodErrorPixels),
    @"policyAlphaThreshold": @(caps.policy.alphaThreshold),
    @"policySubpixelThreshold": @(caps.policy.subpixelThreshold),
    @"policyEnableFrustumCulling": @(caps.policy.enableFrustumCulling),
    @"policyEnableHiZOcclusion": @(caps.policy.enableHiZOcclusion),
    @"policyEnableEarlyTermination": @(caps.policy.enableEarlyTermination),
    @"policySortDepth": @(caps.policy.sortDepth),
  });
}

- (void)didMoveToWindow {
  [super didMoveToWindow];
  if (self.window != nil) {
    self.attached = YES;
    [self startDisplayLink];
    [self attachLayer];
    [self startMotion];
  } else {
    [self detach];
  }
}

- (void)layoutSubviews {
  [super layoutSubviews];
  [self attachLayer];
}

- (void)attachLayer {
  SKSplatEngine *engine = _engine;
  if (engine == nil || CGSizeEqualToSize(self.bounds.size, CGSizeZero)) return;
  CAMetalLayer *layer = (CAMetalLayer *)self.layer;
  CGFloat scale = self.window.screen.scale ?: UIScreen.mainScreen.scale;
  CGSize size = CGSizeMake(round(self.bounds.size.width * scale), round(self.bounds.size.height * scale));
  layer.drawableSize = size;
  [engine setLayer:layer];
  [engine setDrawableSize:size];
}

- (void)startDisplayLink {
  if (self.displayLink != nil || _engine == nil) return;
  CADisplayLink *link = [CADisplayLink displayLinkWithTarget:self.linkProxy selector:@selector(tick:)];
  link.preferredFrameRateRange = CAFrameRateRangeMake(30, 120, 120);
  [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
  self.displayLink = link;
  [self updateDisplayLink];
}

- (void)stopDisplayLink {
  [self.displayLink invalidate];
  self.displayLink = nil;
}

- (void)updateDisplayLink {
  self.displayLink.paused = self.paused || !self.isAttached || !self.isAppActive;
}

- (void)appDidEnterBackground {
  self.appActive = NO;
  [self updateDisplayLink];
  [self stopMotion];
}

- (void)appWillEnterForeground {
  self.appActive = YES;
  [self updateDisplayLink];
  [self startMotion];
}

- (void)detach {
  self.attached = NO;
  [self stopDisplayLink];
  [self stopMotion];
  [_engine setLayer:nil];
}

- (void)dispose {
  [self stopDisplayLink];
  [self stopMotion];
  _generation.fetch_add(1, std::memory_order_acq_rel);
  [_engine setLayer:nil];
  _engine = nil;
  self.requestId = nil;
  _hasLastPose = NO;
}

- (void)recycle {
  [self dispose];
  _hasPolicy = NO;
  _policyRevision = 0;
  _hasCharacter = NO;
  self.colliderPath = nil;
  self.colliderRequestId = nil;
}

// MARK: - Loading

- (void)loadWorld:(NSString *)path requestId:(NSString *)requestId maxShDegree:(NSInteger)maxShDegree
 lodCapacity:(NSInteger)lodCapacity residencyCapacity:(NSInteger)residencyCapacity {
  if (![self validateRequestId:requestId path:path maxShDegree:maxShDegree]) {
    [self emitWorld:requestId phase:@"failed" count:0 code:kErrorInvalidRequest
             message:@"invalid world request"];
    return;
  }

  // Isolate this load from the previous one before it can emit stale callbacks.
  [self dispose];
  self.requestId = [requestId copy];
  const uint64_t generation = _generation.load(std::memory_order_acquire);

  SKSplatEngine *engine = [SKSplatEngine create];
  if (engine == nil) {
    [self emitWorld:requestId phase:@"failed" count:0 code:kErrorGpuUnavailable
             message:@"Metal is unavailable on this device"];
    return;
  }
  _engine = engine;

  // Settings are render-thread state; apply them here (the main thread) before the decode
  // is enqueued so this load's decode observes them, matching the engine's ordering rule.
  [engine setMaxShDegree:(int)MIN(MAX(maxShDegree, 0), 3)];
  [engine setSplatBudget:(int)MAX(lodCapacity, 0)];
  [engine setResidencyBudget:(int)MAX(residencyCapacity, 1)];
  [engine setRenderScale:(float)self.renderScale];
  [engine setShDegree:(int)self.shDegree];
  [engine setLinearBlending:self.linearBlending];
  [engine setCullMargin:(float)self.cullMarginDegrees];
  [engine setMotionEnabled:self.motionEnabled];
  if (_hasCharacter) [engine setCharacter:_character];
  // Report the new engine's capabilities once, then apply the host policy to it before the
  // decode is enqueued. Same order as the Android adapter.
  [self emitCapabilities];
  [self applyStoredPolicy];
  [self attachLayer];
  if (self.isAttached) [self startDisplayLink];

  __weak SplatKitRNView *weakSelf = self;
  engine.eventHandler = ^(SKSplatEvent event, NSString *message, uint32_t count) {
    SplatKitRNView *view = weakSelf;
    if (view == nil) return;
    dispatch_async(dispatch_get_main_queue(), ^{
      if (generation != view->_generation.load(std::memory_order_acquire)) return;
      [view deliverWorldEvent:event message:message count:count requestId:requestId];
    });
  };

  // Decode off the render thread; the next frame uploads. The serial queue bounds
  // concurrent decodes to one and the generation gate skips superseded work.
  dispatch_async(_loaderQueue, ^{
    SplatKitRNView *view = weakSelf;
    if (view == nil) return;
    if (generation != view->_generation.load(std::memory_order_acquire)) return;
    [engine loadWorldFile:path];
  });

  // The world's engine is new, so walk mode has to be rebuilt on it.
  if (self.colliderPath.length > 0) [self enqueueColliderOnGeneration:generation];
}

- (void)loadCollider:(NSString *)path requestId:(NSString *)requestId {
  if (requestId.length == 0 || path.length == 0) {
    self.colliderPath = nil;
    self.colliderRequestId = nil;
    return;
  }
  if (![self isLocalPath:path]) {
    [self emitCollider:requestId phase:@"failed" code:kErrorInvalidRequest
               message:@"invalid collider request"];
    return;
  }
  self.colliderPath = [path copy];
  self.colliderRequestId = [requestId copy];
  if (_engine == nil) return;
  [self enqueueColliderOnGeneration:_generation.load(std::memory_order_acquire)];
}

- (void)enqueueColliderOnGeneration:(uint64_t)generation {
  SKSplatEngine *engine = _engine;
  NSString *path = self.colliderPath;
  if (engine == nil || path.length == 0) return;
  __weak SplatKitRNView *weakSelf = self;
  dispatch_async(_loaderQueue, ^{
    SplatKitRNView *view = weakSelf;
    if (view == nil) return;
    if (generation != view->_generation.load(std::memory_order_acquire)) return;
    [engine loadColliderFile:path];
  });
}

- (BOOL)isLocalPath:(NSString *)path {
  return [path hasPrefix:@"/"] && ![path hasPrefix:@"//"] && ![path isEqualToString:@"/"] &&
      [path rangeOfString:@"\0"].location == NSNotFound;
}

- (BOOL)validateRequestId:(NSString *)requestId path:(NSString *)path maxShDegree:(NSInteger)maxShDegree {
  if ([requestId stringByTrimmingCharactersInSet:
      NSCharacterSet.whitespaceAndNewlineCharacterSet].length == 0) return NO;
  if (![path hasPrefix:@"/"] || [path hasPrefix:@"//"] || [path isEqualToString:@"/"] ||
      [path rangeOfString:@"\0"].location != NSNotFound) return NO;
  if (maxShDegree < 0 || maxShDegree > 3) return NO;
  return YES;
}

// MARK: - Events and stats

- (void)deliverWorldEvent:(SKSplatEvent)event message:(NSString *)message count:(uint32_t)count
    requestId:(NSString *)requestId {
  if (event == SKSplatEventColliderReady || event == SKSplatEventColliderFailed) {
    const BOOL ready = event == SKSplatEventColliderReady;
    [self emitCollider:self.colliderRequestId phase:ready ? @"ready" : @"failed"
                  code:ready ? @"" : kErrorColliderLoadFailed message:message];
    return;
  }
  if (self.worldEvent == nil) return;
  NSString *phase = event == SKSplatEventWorldReady ? @"uploaded"
      : event == SKSplatEventWorldFrameReady ? @"frameReady" : @"failed";
  NSString *code = event == SKSplatEventWorldFailed ? kErrorWorldLoadFailed : @"";
  self.worldEvent(@{@"requestId": requestId, @"phase": phase, @"loadedSplats": @(count),
                    @"errorCode": code, @"message": message ?: @""});
}

- (void)emitCollider:(NSString *)requestId phase:(NSString *)phase code:(NSString *)code
             message:(NSString *)message {
  if (self.colliderEvent == nil) return;
  self.colliderEvent(@{@"requestId": requestId ?: @"", @"phase": phase, @"errorCode": code,
                       @"message": message ?: @""});
}

- (void)emitWorld:(NSString *)requestId phase:(NSString *)phase count:(uint32_t)count
    code:(NSString *)code message:(NSString *)message {
  if (self.worldEvent == nil) return;
  self.worldEvent(@{@"requestId": requestId ?: @"", @"phase": phase, @"loadedSplats": @(count),
                    @"errorCode": code, @"message": message ?: @""});
}

- (void)draw:(CADisplayLink *)link {
  SKSplatEngine *engine = _engine;
  if (engine == nil) return;
  [engine render:(int64_t)(link.timestamp * 1000000000.0)];
  const CFTimeInterval now = CACurrentMediaTime();
  if (self.cameraPoseEvent != nil && self.cameraPoseInterval > 0 &&
      now - self.lastPoseAt >= self.cameraPoseInterval) {
    self.lastPoseAt = now;
    const SKCameraPose pose = engine.cameraPose;
    if (!_hasLastPose || memcmp(&pose, &_lastPose, sizeof(pose)) != 0) {
      _lastPose = pose;
      _hasLastPose = YES;
      self.cameraPoseEvent(@{@"x": @(pose.x), @"y": @(pose.y), @"z": @(pose.z),
                             @"yaw": @(pose.yaw), @"pitch": @(pose.pitch)});
    }
  }
  if (self.statsEvent == nil) return;
  if (now - self.lastStatsAt < kStatsInterval) return;
  self.lastStatsAt = now;
  SKSplatStats stats = engine.stats;
  self.statsEvent(@{@"requestId": self.requestId ?: @"", @"loadedSplats": @(stats.splatCount),
                    @"drawnSplats": @(stats.drawnSplatCount), @"frameMillis": @(stats.frameMillis),
                    @"frameTimingAvailable": @(stats.frameMillis > 0), @"gpuMillis": @(stats.gpuMillis),
                    @"gpuTimingAvailable": @(stats.gpuMillis > 0), @"sortMillis": @(stats.sortMillis),
                    @"sortTimingAvailable": @(stats.sortMillis > 0)});
}

@end
