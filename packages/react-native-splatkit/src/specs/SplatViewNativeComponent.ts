import type {CodegenTypes, HostComponent, ViewProps} from 'react-native';
import {codegenNativeComponent} from 'react-native';

// Keep these wire records local: Codegen does not resolve arbitrary imported aliases.
type NativeWorldRequest = Readonly<{
  requestId: string;
  filePath: string;
  maxShDegree: CodegenTypes.Int32;
  lodCapacitySplats: CodegenTypes.Int32;
  residencyCapacitySplats: CodegenTypes.Int32;
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
  paused?: CodegenTypes.WithDefault<boolean, false>;
  renderScale?: CodegenTypes.WithDefault<CodegenTypes.Double, 1>;
  shDegree?: CodegenTypes.WithDefault<CodegenTypes.Int32, 3>;
  /** The resolved renderer policy; native re-validates and reports the effective one. */
  policy?: NativeRenderPolicy;
  onWorldEvent?: CodegenTypes.DirectEventHandler<NativeWorldEvent>;
  /** Adapter must throttle snapshots to at most 2 Hz; no per-frame JS callbacks. */
  onStats?: CodegenTypes.DirectEventHandler<NativeStatsEvent>;
  onPolicyEvent?: CodegenTypes.DirectEventHandler<NativePolicyEvent>;
  /** Emitted once per engine, which each world load creates, before its first policy event. */
  onCapabilities?: CodegenTypes.DirectEventHandler<NativeCapabilitiesEvent>;
}

export default codegenNativeComponent<NativeProps>('SplatKitView') as HostComponent<NativeProps>;
