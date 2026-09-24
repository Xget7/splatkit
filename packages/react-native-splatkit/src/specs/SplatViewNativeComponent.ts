import type {CodegenTypes, HostComponent, ViewProps} from 'react-native';
import {codegenNativeComponent, codegenNativeCommands} from 'react-native';

// Keep these wire records local: Codegen does not resolve arbitrary imported aliases.
type NativeWorldRequest = Readonly<{
  requestId: string;
  filePath: string;
  maxShDegree: CodegenTypes.Int32;
  lodCapacitySplats: CodegenTypes.Int32;
  residencyCapacitySplats: CodegenTypes.Int32;
}>;

/** Immutable collider transaction, the walk-mode counterpart of a world request. */
type NativeColliderRequest = Readonly<{
  /** Unique for each replacement; empty releases walk mode. */
  requestId: string;
  /** Absolute, readable local path to a collider GLB. */
  filePath: string;
}>;

type NativeColliderEvent = Readonly<{
  requestId: string;
  phase: 'ready' | 'failed';
  errorCode: string;
  message: string;
}>;

/** The walker's shape in walk mode, in meters. Zero eyeHeight means the native default. */
type NativeCharacter = Readonly<{
  eyeHeight: CodegenTypes.Double;
  bodyRadius: CodegenTypes.Double;
  stepHeight: CodegenTypes.Double;
}>;

/** Where the camera is and where it looks: meters and radians, in the world's frame. */
type NativeCameraPoseEvent = Readonly<{
  x: CodegenTypes.Double;
  y: CodegenTypes.Double;
  z: CodegenTypes.Double;
  yaw: CodegenTypes.Double;
  pitch: CodegenTypes.Double;
}>;

type NativeFocusResult = Readonly<{
  requestId: string;
  hit: boolean;
}>;

type NativeWorldEvent = Readonly<{
  requestId: string;
  phase: 'uploaded' | 'frameReady' | 'failed';
  loadedSplats: CodegenTypes.Double;
  errorCode: string;
  message: string;
}>;

type NativeStatsEvent = Readonly<{
  requestId: string;
  loadedSplats: CodegenTypes.Double;
  drawnSplats: CodegenTypes.Double;
  frameMillis: CodegenTypes.Double;
  frameTimingAvailable: boolean;
  gpuMillis: CodegenTypes.Double;
  gpuTimingAvailable: boolean;
  sortMillis: CodegenTypes.Double;
  sortTimingAvailable: boolean;
}>;

/** The complete renderer policy. Native re-validates it and reports the effective one. */
type NativeRenderPolicy = Readonly<{
  /** Positive; 0 or less means no policy. Bump it per change: native applies each new revision. */
  revision: CodegenTypes.Int32;
  /** 0 hardware, 1 computeTile, 2 hybrid. */
  raster: CodegenTypes.Int32;
  /** 8, 16 or 32. */
  tileSize: CodegenTypes.Int32;
  lodErrorPixels: CodegenTypes.Double;
  alphaThreshold: CodegenTypes.Double;
  subpixelThreshold: CodegenTypes.Double;
  enableFrustumCulling: boolean;
  enableHiZOcclusion: boolean;
  enableEarlyTermination: boolean;
  /** 16 or 32. */
  sortDepth: CodegenTypes.Int32;
}>;

/**
 * One per application, including re-application to the engine each world load creates.
 * applied: the whole request landed. warning: a field fell back; message joins the reasons.
 * rejected: the previous policy stays and is reported; errorCode is INVALID_POLICY or
 * POLICY_PREPARATION_FAILED.
 */
type NativePolicyEvent = Readonly<{
  revision: CodegenTypes.Int32;
  phase: 'applied' | 'warning' | 'rejected';
  errorCode: string;
  message: string;
  raster: CodegenTypes.Int32;
  tileSize: CodegenTypes.Int32;
  lodErrorPixels: CodegenTypes.Double;
  alphaThreshold: CodegenTypes.Double;
  subpixelThreshold: CodegenTypes.Double;
  enableFrustumCulling: boolean;
  enableHiZOcclusion: boolean;
  enableEarlyTermination: boolean;
  sortDepth: CodegenTypes.Int32;
}>;

/** Native limits, feature flags and the policy fields the adapter accepts. */
type NativeCapabilitiesEvent = Readonly<{
  maxLodCapacitySplats: CodegenTypes.Int32;
  minResidencyCapacitySplats: CodegenTypes.Int32;
  maxResidencyCapacitySplats: CodegenTypes.Int32;
  supportsComputeTiles: boolean;
  supportsHiZOcclusion: boolean;
  supportsSubgroups: boolean;
  maxTextureDimension: CodegenTypes.Int32;
  policyRaster: boolean;
  /** Raster strategies native applies: bit 0 hardware, bit 1 computeTile, bit 2 hybrid. */
  policyRasterMask: CodegenTypes.Int32;
  policyTileSize: boolean;
  policyLodErrorPixels: boolean;
  policyAlphaThreshold: boolean;
  policySubpixelThreshold: boolean;
  policyEnableFrustumCulling: boolean;
  policyEnableHiZOcclusion: boolean;
  policyEnableEarlyTermination: boolean;
  policySortDepth: boolean;
}>;

export interface NativeProps extends ViewProps {
  world?: NativeWorldRequest;
  /** Enables walk mode when it is ready; an empty requestId releases it. */
  collider?: NativeColliderRequest;
  /** The walker's shape, applied at once and to a collider loaded later. */
  character?: NativeCharacter;
  paused?: CodegenTypes.WithDefault<boolean, false>;
  renderScale?: CodegenTypes.WithDefault<CodegenTypes.Double, 1>;
  shDegree?: CodegenTypes.WithDefault<CodegenTypes.Int32, 3>;
  /** Blend in linear light instead of the encoded space the training used. */
  linearBlending?: CodegenTypes.WithDefault<boolean, false>;
  /** CPU fallback's angular culling margin in degrees; the GPU path uses projected bounds. */
  cullMarginDegrees?: CodegenTypes.WithDefault<CodegenTypes.Double, 10>;
  /** Drives the camera with the gyroscope. Ignored where the sensor is missing. */
  motionEnabled?: CodegenTypes.WithDefault<boolean, false>;
  /** Whether a drag on the view turns the camera. Off when the host looks with its own control. */
  touchLookEnabled?: CodegenTypes.WithDefault<boolean, true>;
  /** Radians per point dragged to look. */
  lookSensitivity?: CodegenTypes.WithDefault<CodegenTypes.Double, 0.004>;
  /** Seconds between onCameraPose events; 0, the default, never sends one. */
  cameraPoseInterval?: CodegenTypes.WithDefault<CodegenTypes.Double, 0>;
  /** The resolved renderer policy; native re-validates and reports the effective one. */
  policy?: NativeRenderPolicy;
  onWorldEvent?: CodegenTypes.DirectEventHandler<NativeWorldEvent>;
  /** Adapter must throttle snapshots to at most 2 Hz; no per-frame JS callbacks. */
  onStats?: CodegenTypes.DirectEventHandler<NativeStatsEvent>;
  onColliderEvent?: CodegenTypes.DirectEventHandler<NativeColliderEvent>;
  /** Throttled to cameraPoseInterval, and sent only when the pose changed. */
  onCameraPose?: CodegenTypes.DirectEventHandler<NativeCameraPoseEvent>;
  /** One result per focus command; a miss leaves the current anchor unchanged. */
  onFocusResult?: CodegenTypes.DirectEventHandler<NativeFocusResult>;
  onPolicyEvent?: CodegenTypes.DirectEventHandler<NativePolicyEvent>;
  /** Emitted once per engine, which each world load creates, before its first policy event. */
  onCapabilities?: CodegenTypes.DirectEventHandler<NativeCapabilitiesEvent>;
}

type ComponentType = HostComponent<NativeProps>;

/**
 * Imperative navigation, for the host's own controls: a joystick or a look pad drives the
 * camera at touch rate without a React commit per frame.
 */
export interface NativeCommands {
  /** Meters per second until called again with zeros. Forward is where the camera looks. */
  setWalkVelocity: (
    viewRef: React.ComponentRef<ComponentType>,
    forward: CodegenTypes.Double,
    right: CodegenTypes.Double,
  ) => void;
  /** Radians. Pitch is clamped, and ignored while the gyroscope drives the view. */
  look: (
    viewRef: React.ComponentRef<ComponentType>,
    deltaYaw: CodegenTypes.Double,
    deltaPitch: CodegenTypes.Double,
  ) => void;
  /** Teleports; while walking the camera settles on the floor under the new point. */
  setCameraPose: (
    viewRef: React.ComponentRef<ComponentType>,
    x: CodegenTypes.Double,
    y: CodegenTypes.Double,
    z: CodegenTypes.Double,
    yaw: CodegenTypes.Double,
    pitch: CodegenTypes.Double,
  ) => void;
  /** World-space position, target and up vector. */
  lookAt: (
    viewRef: React.ComponentRef<ComponentType>,
    x: CodegenTypes.Double, y: CodegenTypes.Double, z: CodegenTypes.Double,
    targetX: CodegenTypes.Double, targetY: CodegenTypes.Double, targetZ: CodegenTypes.Double,
    upX: CodegenTypes.Double, upY: CodegenTypes.Double, upZ: CodegenTypes.Double,
  ) => void;
  /** Start orbiting a world-space point from the current camera position. */
  setAnchor: (
    viewRef: React.ComponentRef<ComponentType>,
    x: CodegenTypes.Double, y: CodegenTypes.Double, z: CodegenTypes.Double,
  ) => void;
  /** Orbit by radians; an anchor must first be set or focused. */
  orbit: (
    viewRef: React.ComponentRef<ComponentType>,
    deltaAzimuth: CodegenTypes.Double, deltaElevation: CodegenTypes.Double,
  ) => void;
  /** Change orbit radius in meters; positive moves away from the anchor. */
  dolly: (viewRef: React.ComponentRef<ComponentType>, deltaRadius: CodegenTypes.Double) => void;
  /** Pick an anchor using normalized view coordinates; result arrives in onFocusResult. */
  focus: (
    viewRef: React.ComponentRef<ComponentType>,
    requestId: string, x: CodegenTypes.Double, y: CodegenTypes.Double,
  ) => void;
  /** Turn a finite number of degrees at the average speed in degrees per second. */
  animateOrbit: (
    viewRef: React.ComponentRef<ComponentType>,
    degrees: CodegenTypes.Double, degreesPerSecond: CodegenTypes.Double, easeInOut: boolean,
  ) => void;
}

export const Commands: NativeCommands = codegenNativeCommands<NativeCommands>({
  supportedCommands: ['setWalkVelocity', 'look', 'setCameraPose', 'lookAt', 'setAnchor',
    'orbit', 'dolly', 'focus', 'animateOrbit'],
});

export default codegenNativeComponent<NativeProps>('SplatKitView') as ComponentType;
