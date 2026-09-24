#import <UIKit/UIKit.h>
#import <SplatKit/SKSplatEngine.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^SplatKitRNEventBlock)(NSDictionary *event);

/// Objective-C surface for the Fabric component.
///
/// One native engine is created per accepted load, like the Android adapter, so a
/// replacement is isolated from the previous world's callbacks. Decoding runs on a
/// serial loader queue and uploads happen on the view's render thread (the main
/// thread here); the display link drives one frame per tick. Events are tagged with
/// the load generation that produced them and stale events are dropped.
@interface SplatKitRNView : UIView
@property(nonatomic, assign) BOOL paused;
@property(nonatomic, assign) CGFloat renderScale;
@property(nonatomic, assign) NSInteger shDegree;
@property(nonatomic, assign) BOOL linearBlending;
@property(nonatomic, assign) CGFloat cullMarginDegrees;
@property(nonatomic, assign) BOOL touchLookEnabled;
@property(nonatomic, assign) CGFloat lookSensitivity;
@property(nonatomic, assign) BOOL motionEnabled;
/// Seconds between camera pose events; 0 sends none.
@property(nonatomic, assign) CFTimeInterval cameraPoseInterval;
@property(nonatomic, copy, nullable) SplatKitRNEventBlock worldEvent;
@property(nonatomic, copy, nullable) SplatKitRNEventBlock statsEvent;
@property(nonatomic, copy, nullable) SplatKitRNEventBlock policyEvent;
@property(nonatomic, copy, nullable) SplatKitRNEventBlock capabilitiesEvent;
@property(nonatomic, copy, nullable) SplatKitRNEventBlock colliderEvent;
@property(nonatomic, copy, nullable) SplatKitRNEventBlock cameraPoseEvent;
@property(nonatomic, copy, nullable) SplatKitRNEventBlock focusResultEvent;
- (void)loadWorld:(NSString *)path requestId:(NSString *)requestId maxShDegree:(NSInteger)maxShDegree
 lodCapacity:(NSInteger)lodCapacity residencyCapacity:(NSInteger)residencyCapacity;
/// Loads a collider GLB and enables walk mode when it is ready. An empty path releases it.
- (void)loadCollider:(NSString *)path requestId:(NSString *)requestId;
/// The walker's shape, applied at once and to a collider loaded later. NO when a value is
/// not a walkable one, and then the previous settings stay.
- (BOOL)setCharacter:(SKCharacterSettings)character;
- (void)setWalkVelocityForward:(float)forward right:(float)right;
- (void)lookWithDeltaYaw:(float)deltaYaw deltaPitch:(float)deltaPitch;
- (void)setPose:(SKCameraPose)pose;
- (void)lookAtFrom:(SKVec3)position target:(SKVec3)target up:(SKVec3)up;
- (void)setAnchor:(SKVec3)point;
- (void)orbitWithDeltaAzimuth:(float)deltaAzimuth deltaElevation:(float)deltaElevation;
- (void)dolly:(float)deltaRadius;
- (void)focusRequestId:(NSString *)requestId x:(float)x y:(float)y;
- (void)animateOrbitDegrees:(float)degrees degreesPerSecond:(float)degreesPerSecond
              easeInOut:(BOOL)easeInOut;
/// Remembers the host policy without applying it; a revision of 0 or less forgets it. Every
/// engine a later `loadWorld:` creates applies the remembered policy before decoding.
- (void)setPolicy:(SKRenderPolicy)policy revision:(NSInteger)revision;
/// Applies the remembered policy to the current engine, if both exist, and reports the
/// outcome under its revision.
- (void)applyStoredPolicy;
/// Pauses rendering and detaches the layer, keeping the engine and loaded world so the
/// view can reattach cheaply. Called when the view leaves a window.
- (void)detach;
/// Stops rendering, releases the engine and breaks the display-link cycle. Safe to call
/// more than once; the next `loadWorld:` creates a fresh engine.
- (void)dispose;
/// Disposes and forgets the policy, so a recycled view keeps nothing from its last mount.
- (void)recycle;
@end

NS_ASSUME_NONNULL_END
