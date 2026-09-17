import type {RenderOptions, SHDegree, SplatLimits, WorldRequest} from './contracts';
import {validateRenderOptions, validateWorldRequest} from './contracts';
import type {NativeProps} from './specs/SplatViewNativeComponent';

/**
 * `hardware` is the default and the fastest choice for most scenes. `hybrid` composites screen
 * tiles in compute and suits close-up views where many large translucent splats overlap each
 * pixel; on distant or sparse scenes it adds GPU work and lowers the frame rate. It is
 * experimental and iOS-only today. `computeTile` is not implemented by any adapter yet.
 */
export const RasterStrategy = {
  hardware: 'hardware',
  computeTile: 'computeTile',
  hybrid: 'hybrid',
} as const;
export type RasterStrategy = (typeof RasterStrategy)[keyof typeof RasterStrategy];

export type SortDepth = 16 | 32;

export const PerformanceMode = { auto: 'auto', manual: 'manual' } as const;
export type PerformanceMode = (typeof PerformanceMode)[keyof typeof PerformanceMode];

/** Ordered by decreasing detail; `build` resolves each against the device's own limits. */
export const QualityPreset = {
  highEnd: 'highEnd',
  high: 'high',
  balanced: 'balanced',
  performance: 'performance',
} as const;
export type QualityPreset = (typeof QualityPreset)[keyof typeof QualityPreset];
export const qualityPresets: readonly QualityPreset[] = Object.freeze([
  QualityPreset.highEnd, QualityPreset.high, QualityPreset.balanced, QualityPreset.performance,
]);

/** Which policy fields one native adapter applies; the rest fall back natively. */
export type NativePolicySupport = Readonly<{
  raster: boolean;
  /** The strategies native applies when `raster` is true. */
  rasterStrategies: readonly RasterStrategy[];
  tileSize: boolean;
  lodErrorPixels: boolean;
  alphaThreshold: boolean;
  subpixelThreshold: boolean;
  enableFrustumCulling: boolean;
  enableHiZOcclusion: boolean;
  enableEarlyTermination: boolean;
  sortDepth: boolean;
}>;

/** Host-supplied constraints. Use nativeCapabilitiesFromEvent for the native report. */
export type DeviceCapabilities = Readonly<{
  limits: SplatLimits;
  supportsComputeTiles: boolean;
  supportsHiZOcclusion: boolean;
  supportsSubgroups: boolean;
  maxTextureDimension: number;
  /** The native adapter's policy acceptance. Absent means it is unknown yet. */
  policy?: NativePolicySupport;
}>;

/** The policy as it crosses the Fabric boundary; enum values are the numbers native uses. */
export type NativeRenderPolicy = Readonly<{
  revision: number;
  raster: number;
  tileSize: number;
  lodErrorPixels: number;
  alphaThreshold: number;
  subpixelThreshold: number;
  enableFrustumCulling: boolean;
  enableHiZOcclusion: boolean;
  enableEarlyTermination: boolean;
  sortDepth: number;
}>;

/** The flat onCapabilities event payload, as declared in the spec. */
export type NativeCapabilitiesEvent = Readonly<{
  maxLodCapacitySplats: number;
  minResidencyCapacitySplats: number;
  maxResidencyCapacitySplats: number;
  supportsComputeTiles: boolean;
  supportsHiZOcclusion: boolean;
  supportsSubgroups: boolean;
  maxTextureDimension: number;
  policyRaster: boolean;
  policyRasterMask: number;
  policyTileSize: boolean;
  policyLodErrorPixels: boolean;
  policyAlphaThreshold: boolean;
  policySubpixelThreshold: boolean;
  policyEnableFrustumCulling: boolean;
  policyEnableHiZOcclusion: boolean;
  policyEnableEarlyTermination: boolean;
  policySortDepth: boolean;
}>;

/** Capacities are configured by the performance policy, not duplicated here. */
export type SplatKitWorldRequest = Readonly<
  Omit<WorldRequest, 'lodCapacitySplats' | 'residencyCapacitySplats'>
>;

export type PerformancePolicy = Readonly<{
  mode: PerformanceMode;
  preset: QualityPreset;
  raster: RasterStrategy;
  tileSize: 8 | 16 | 32;
  lodErrorPixels: number;
  lodBudgetSplats: number;
  alphaThreshold: number;
  subpixelThreshold: number;
  enableFrustumCulling: boolean;
  enableHiZOcclusion: boolean;
  enableEarlyTermination: boolean;
  sortDepth: SortDepth;
  renderScale: number;
  shDegree: SHDegree;
  residencyCapacitySplats: number;
  /** A request only. It never implicitly enables dynamic quality. */
  targetFps: number | null;
}>;

export type PerformanceOptions = Partial<Omit<PerformancePolicy, 'mode' | 'preset'>>;

/** null means native support is unknown: build with capabilities from onCapabilities. */
export type EffectivePerformancePolicy = Readonly<{
  raster: RasterStrategy | null;
  tileSize: 8 | 16 | 32 | null;
  lodErrorPixels: number | null;
  lodBudgetSplats: number;
  alphaThreshold: number | null;
  subpixelThreshold: number | null;
  enableFrustumCulling: boolean | null;
  enableHiZOcclusion: boolean | null;
  enableEarlyTermination: boolean | null;
  sortDepth: SortDepth | null;
  renderScale: number;
  shDegree: SHDegree;
  residencyCapacitySplats: number;
  targetFps: null;
}>;

export type PerformanceDiagnostic = Readonly<{
  severity: 'warning';
  code: 'fabric-option-unavailable' | 'native-support-unknown' |
    'capability-fallback-unavailable' | 'limit-clamped' | 'native-option-fallback';
  option: keyof PerformancePolicy;
  requested: PerformancePolicy[keyof PerformancePolicy];
  /** A candidate fallback, not a claim about applied native state. */
  fallback: PerformancePolicy[keyof PerformancePolicy] | null;
  message: string;
}>;

export type PerformanceResolution = Readonly<{
  requested: PerformancePolicy;
  effective: EffectivePerformancePolicy;
  diagnostics: readonly PerformanceDiagnostic[];
}>;

export type SplatKitConfiguration = Readonly<{
  world: WorldRequest;
  render: RenderOptions;
  performance: PerformanceResolution;
}>;

/** What applying a changed policy costs. Compare against these, not the strings. */
export const PolicyChangeKind = {
  none: 'none',
  /** New Fabric props reach the live engine; nothing reloads. */
  nativePropUpdate: 'nativePropUpdate',
  /** A load-time budget moved: only a fresh world request picks it up. */
  worldReload: 'worldReload',
  unavailable: 'unavailable',
} as const;
export type PolicyChangeKind = (typeof PolicyChangeKind)[keyof typeof PolicyChangeKind];

/** What one policy application reported. */
export const PolicyPhase = {
  applied: 'applied',
  /** A field fell back; the message joins the reasons. */
  warning: 'warning',
  rejected: 'rejected',
} as const;
export type PolicyPhase = (typeof PolicyPhase)[keyof typeof PolicyPhase];

const loadKeys = new Set<keyof PerformancePolicy>([
  'lodBudgetSplats', 'residencyCapacitySplats',
]);
// renderScale and shDegree are their own props; the rest travel in the `policy` prop.
const propKeys = new Set<keyof PerformancePolicy>([
  'renderScale', 'shDegree', 'raster', 'tileSize', 'lodErrorPixels', 'alphaThreshold',
  'subpixelThreshold', 'enableFrustumCulling', 'enableHiZOcclusion',
  'enableEarlyTermination', 'sortDepth',
]);

/** Describes Fabric work only; it does not guarantee runtime safety or no stalls. */
export function classifyPolicyChange(
  previous: PerformancePolicy,
  next: PerformancePolicy,
): PolicyChangeKind {
  let unavailable = false;
  let propUpdate = false;
  for (const key of Object.keys(previous) as (keyof PerformancePolicy)[]) {
    if (previous[key] === next[key] || key === 'mode' || key === 'preset') continue;
    if (loadKeys.has(key)) return PolicyChangeKind.worldReload;
    if (propKeys.has(key)) propUpdate = true;
    else unavailable = true;
  }
  if (propUpdate) return PolicyChangeKind.nativePropUpdate;
  return unavailable ? PolicyChangeKind.unavailable : PolicyChangeKind.none;
}

/** Converts resolved values to the complete current Fabric prop surface. */
export function toNativeViewProps(configuration: SplatKitConfiguration): Pick<NativeProps,
  'world' | 'paused' | 'renderScale' | 'shDegree'> {
  return {
    world: configuration.world,
    paused: configuration.render.paused,
    renderScale: configuration.performance.effective.renderScale,
    shDegree: configuration.performance.effective.shDegree,
  };
}

const rasterWire: Readonly<Record<RasterStrategy, number>> = {
  hardware: 0, computeTile: 1, hybrid: 2,
};

/**
 * The complete requested policy as the Fabric `policy` prop expects it. Pass a new positive
 * revision for each change; native reports `onPolicyEvent` under it and treats 0 or less as
 * no policy, so those are rejected here.
 */
export function toNativePolicyProp(configuration: SplatKitConfiguration,
  revision: number): NativeRenderPolicy {
  integer('revision', revision, 1, 0x7fffffff);
  const policy = configuration.performance.requested;
  return {
    revision,
    raster: rasterWire[policy.raster],
    tileSize: policy.tileSize,
    lodErrorPixels: policy.lodErrorPixels,
    alphaThreshold: policy.alphaThreshold,
    subpixelThreshold: policy.subpixelThreshold,
    enableFrustumCulling: policy.enableFrustumCulling,
    enableHiZOcclusion: policy.enableHiZOcclusion,
    enableEarlyTermination: policy.enableEarlyTermination,
    sortDepth: policy.sortDepth,
  };
}

/** Builds the capabilities `build` takes from the native adapter's onCapabilities event. */
export function nativeCapabilitiesFromEvent(event: NativeCapabilitiesEvent): DeviceCapabilities {
  return Object.freeze({
    limits: Object.freeze({
      maxLodCapacitySplats: event.maxLodCapacitySplats,
      minResidencyCapacitySplats: event.minResidencyCapacitySplats,
      maxResidencyCapacitySplats: event.maxResidencyCapacitySplats,
    }),
    supportsComputeTiles: event.supportsComputeTiles,
    supportsHiZOcclusion: event.supportsHiZOcclusion,
    supportsSubgroups: event.supportsSubgroups,
    maxTextureDimension: event.maxTextureDimension,
    policy: Object.freeze({
      raster: event.policyRaster,
      rasterStrategies: Object.freeze((Object.keys(rasterWire) as RasterStrategy[])
        .filter(strategy => (event.policyRasterMask & (1 << rasterWire[strategy])) !== 0)),
      tileSize: event.policyTileSize,
      lodErrorPixels: event.policyLodErrorPixels,
      alphaThreshold: event.policyAlphaThreshold,
      subpixelThreshold: event.policySubpixelThreshold,
      enableFrustumCulling: event.policyEnableFrustumCulling,
      enableHiZOcclusion: event.policyEnableHiZOcclusion,
      enableEarlyTermination: event.policyEnableEarlyTermination,
      sortDepth: event.policySortDepth,
    }),
  });
}

/** The shared native RenderPolicy defaults; a backend starts from its own fallback. */
const nativeDefaults = {
  raster: 'hardware', tileSize: 16, lodErrorPixels: 1, alphaThreshold: 1 / 255,
  subpixelThreshold: 0.5, enableFrustumCulling: true, enableHiZOcclusion: false,
  enableEarlyTermination: true, sortDepth: 32,
} as const;

type PresetValues = Omit<PerformancePolicy, 'mode' | 'preset'>;
// Every preset rasterizes in hardware; screen tiles only pay off on some scenes, so they are
// an explicit withPerformance({raster: 'hybrid'}) choice.
const presets: Readonly<Record<QualityPreset, PresetValues>> = {
  highEnd: {raster: 'hardware', tileSize: 16, lodErrorPixels: 0.75,
    lodBudgetSplats: 4_000_000, alphaThreshold: 1 / 255, subpixelThreshold: 0.35,
    enableFrustumCulling: true, enableHiZOcclusion: true, enableEarlyTermination: true,
    sortDepth: 32, renderScale: 1.25, shDegree: 3,
    residencyCapacitySplats: 4_000_000, targetFps: null},
  high: {raster: 'hardware', tileSize: 16, lodErrorPixels: 1,
    lodBudgetSplats: 3_000_000, alphaThreshold: 1 / 255, subpixelThreshold: 0.5,
    enableFrustumCulling: true, enableHiZOcclusion: true, enableEarlyTermination: true,
    sortDepth: 32, renderScale: 1, shDegree: 3,
    residencyCapacitySplats: 3_000_000, targetFps: null},
  balanced: {raster: 'hardware', tileSize: 16, lodErrorPixels: 1.25,
    lodBudgetSplats: 2_000_000, alphaThreshold: 1 / 255, subpixelThreshold: 0.65,
    enableFrustumCulling: true, enableHiZOcclusion: false, enableEarlyTermination: true,
    sortDepth: 16, renderScale: 0.85, shDegree: 2,
    residencyCapacitySplats: 2_000_000, targetFps: null},
  performance: {raster: 'hardware', tileSize: 8, lodErrorPixels: 2,
    lodBudgetSplats: 1_000_000, alphaThreshold: 2 / 255, subpixelThreshold: 1,
    enableFrustumCulling: true, enableHiZOcclusion: false, enableEarlyTermination: true,
    sortDepth: 16, renderScale: 0.65, shDegree: 1,
    residencyCapacitySplats: 1_000_000, targetFps: null},
};

const optionKeys = new Set<string>([
  'raster', 'tileSize', 'lodErrorPixels', 'lodBudgetSplats', 'alphaThreshold',
  'subpixelThreshold', 'enableFrustumCulling', 'enableHiZOcclusion',
  'enableEarlyTermination', 'sortDepth', 'renderScale', 'shDegree',
  'residencyCapacitySplats', 'targetFps',
]);
const policyPropKeys = [
  'raster', 'tileSize', 'lodErrorPixels', 'alphaThreshold', 'subpixelThreshold',
  'enableFrustumCulling', 'enableHiZOcclusion', 'enableEarlyTermination', 'sortDepth',
] as const;

function integer(name: string, value: number, min: number, max: number): void {
  if (!Number.isSafeInteger(value) || value < min || value > max) {
    throw new RangeError(`${name} must be an integer in [${min}, ${max}]`);
  }
}

function range(name: string, value: number, min: number, max: number): void {
  if (!Number.isFinite(value) || value < min || value > max) {
    throw new RangeError(`${name} must be finite and in [${min}, ${max}]`);
  }
}

function validatePolicy(policy: PerformancePolicy): void {
  if (policy.mode !== 'auto' && policy.mode !== 'manual') throw new TypeError('invalid mode');
  if (typeof policy.preset !== 'string' || !Object.hasOwn(presets, policy.preset)) {
    throw new TypeError('invalid performance preset');
  }
  if (!['hardware', 'computeTile', 'hybrid'].includes(policy.raster)) {
    throw new TypeError('invalid raster strategy');
  }
  if (![8, 16, 32].includes(policy.tileSize)) throw new RangeError('invalid tileSize');
  if (![16, 32].includes(policy.sortDepth)) throw new RangeError('invalid sortDepth');
  integer('lodBudgetSplats', policy.lodBudgetSplats, 0, 0x7fffffff);
  integer('residencyCapacitySplats', policy.residencyCapacitySplats, 1, 0x7fffffff);
  integer('shDegree', policy.shDegree, 0, 3);
  range('renderScale', policy.renderScale, 0.1, 2);
  range('lodErrorPixels', policy.lodErrorPixels, Number.MIN_VALUE, Number.MAX_VALUE);
  range('alphaThreshold', policy.alphaThreshold, 0, 1);
  range('subpixelThreshold', policy.subpixelThreshold, 0, Number.MAX_VALUE);
  for (const key of ['enableFrustumCulling', 'enableHiZOcclusion',
    'enableEarlyTermination'] as const) {
    if (typeof policy[key] !== 'boolean') throw new TypeError(`${key} must be boolean`);
  }
  if (policy.targetFps !== null) range('targetFps', policy.targetFps, 1, 240);
}

function policyFor(preset: QualityPreset): PerformancePolicy {
  return Object.freeze({mode: 'auto', preset, ...presets[preset]});
}

export class SplatKitBuilder {
  private world?: SplatKitWorldRequest;
  private paused = false;
  private performance = policyFor('balanced');

  withWorld(world: SplatKitWorldRequest): this {
    for (const key of Object.keys(world)) {
      if (!['requestId', 'filePath', 'maxShDegree'].includes(key)) {
        throw new TypeError(`${key} must be configured with withPerformance()`);
      }
    }
    this.world = Object.freeze({...world});
    return this;
  }

  withRender(options: Readonly<{paused?: boolean}>): this {
    for (const key of Object.keys(options)) {
      if (key !== 'paused') throw new TypeError(`${key} must be configured with withPerformance()`);
    }
    if (options.paused !== undefined) {
      if (typeof options.paused !== 'boolean') throw new TypeError('paused must be boolean');
      this.paused = options.paused;
    }
    return this;
  }

  withPerformance(options: PerformanceOptions): this {
    for (const key of Object.keys(options)) {
      if (!optionKeys.has(key)) throw new TypeError(`unknown performance option: ${key}`);
    }
    const next = {...this.performance, ...options, mode: 'manual' as const};
    validatePolicy(next);
    this.performance = Object.freeze(next);
    return this;
  }

  withPreset(preset: QualityPreset): this {
    if (typeof preset !== 'string' || !Object.hasOwn(presets, preset)) {
      throw new TypeError('invalid performance preset');
    }
    this.performance = policyFor(preset);
    return this;
  }

  build(capabilities: DeviceCapabilities): SplatKitConfiguration {
    if (!this.world) throw new Error('world is required');
    validatePolicy(this.performance);
    for (const key of ['supportsComputeTiles', 'supportsHiZOcclusion', 'supportsSubgroups'] as const) {
      if (typeof capabilities[key] !== 'boolean') throw new TypeError(`${key} must be boolean`);
    }
    integer('maxTextureDimension', capabilities.maxTextureDimension, 1, 0x7fffffff);

    const requested = Object.freeze({...this.performance});
    const diagnostics: PerformanceDiagnostic[] = [];
    const diagnosticKeys = new Set<string>();
    const warn = (code: PerformanceDiagnostic['code'], option: keyof PerformancePolicy,
      requestedValue: PerformanceDiagnostic['requested'], fallback: PerformanceDiagnostic['fallback'],
      message: string): void => {
      const key = `${code}:${option}`;
      if (diagnosticKeys.has(key)) return;
      diagnosticKeys.add(key);
      diagnostics.push(Object.freeze({severity: 'warning', code, option,
        requested: requestedValue, fallback, message}));
    };
    const clamp = (option: 'lodBudgetSplats' | 'residencyCapacitySplats', value: number,
      min: number, max: number): number => {
      const effective = Math.min(max, Math.max(min, value));
      if (effective !== value) warn('limit-clamped', option, value, effective,
        `${option} was clamped to the host-supplied limit`);
      return effective;
    };

    const lodBudgetSplats = clamp('lodBudgetSplats', requested.lodBudgetSplats, 0,
      capabilities.limits.maxLodCapacitySplats);
    const residencyCapacitySplats = clamp('residencyCapacitySplats',
      requested.residencyCapacitySplats, capabilities.limits.minResidencyCapacitySplats,
      capabilities.limits.maxResidencyCapacitySplats);
    const shDegree = Math.min(requested.shDegree, this.world.maxShDegree) as SHDegree;
    if (shDegree !== requested.shDegree) warn('limit-clamped', 'shDegree',
      requested.shDegree, shDegree, 'shDegree was capped by the world maxShDegree');

    const effectiveFields: {
      raster: RasterStrategy | null;
      tileSize: 8 | 16 | 32 | null;
      lodErrorPixels: number | null;
      alphaThreshold: number | null;
      subpixelThreshold: number | null;
      enableFrustumCulling: boolean | null;
      enableHiZOcclusion: boolean | null;
      enableEarlyTermination: boolean | null;
      sortDepth: SortDepth | null;
    } = {
      raster: null, tileSize: null, lodErrorPixels: null, alphaThreshold: null,
      subpixelThreshold: null, enableFrustumCulling: null, enableHiZOcclusion: null,
      enableEarlyTermination: null, sortDepth: null,
    };

    if (capabilities.policy) {
      // Native reports what it accepts, so the effective values are real, not null. A
      // request outside that support still warns once and falls back on the device.
      const support = capabilities.policy;
      const fields = effectiveFields as Record<(typeof policyPropKeys)[number],
        PerformancePolicy[keyof PerformancePolicy] | null>;
      for (const option of policyPropKeys) {
        const applied = option === 'raster'
          ? support.raster && support.rasterStrategies.includes(requested.raster)
          : support[option];
        if (applied) fields[option] = requested[option];
        else if (requested[option] !== nativeDefaults[option]) {
          warn('native-option-fallback', option, requested[option], null,
            `${option} fell back to the native default; read onPolicyEvent for the applied value`);
        }
      }
    } else {
      // The policy prop carries every field, but only onCapabilities says which ones native
      // applies. Warn only where the request differs from the native default.
      for (const option of policyPropKeys) {
        if (requested[option] === nativeDefaults[option]) continue;
        const hardwareFallback = option === 'raster' && !capabilities.supportsComputeTiles;
        warn(hardwareFallback ? 'capability-fallback-unavailable' : 'native-support-unknown',
          option, requested[option], hardwareFallback ? 'hardware' : null,
          hardwareFallback
            ? 'hardware is the candidate fallback; read onPolicyEvent for the applied raster'
            : `${option} support is unknown until native capabilities are supplied`);
      }
    }
    if (requested.targetFps !== null) {
      warn('fabric-option-unavailable', 'targetFps', requested.targetFps, null,
        'targetFps is a request only; adaptive quality is separate');
    }

    const world = Object.freeze({...this.world, lodCapacitySplats: lodBudgetSplats,
      residencyCapacitySplats});
    const render = Object.freeze({paused: this.paused, renderScale: requested.renderScale, shDegree});
    validateWorldRequest(world, capabilities.limits);
    validateRenderOptions(render);
    const effective: EffectivePerformancePolicy = Object.freeze({
      ...effectiveFields,
      lodBudgetSplats,
      renderScale: requested.renderScale,
      shDegree,
      residencyCapacitySplats,
      targetFps: null,
    });
    return Object.freeze({world, render, performance: Object.freeze({requested, effective,
      diagnostics: Object.freeze(diagnostics.slice())})});
  }
}
