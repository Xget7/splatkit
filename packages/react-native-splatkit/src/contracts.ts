export type SHDegree = 0 | 1 | 2 | 3;

/** Immutable load transaction: options apply before decoding this world. */
export type WorldRequest = Readonly<{
  /** Unique for each replacement, including retries of the same file. */
  requestId: string;
  /** Absolute, readable local path. No URL fetching or JS byte transport. */
  filePath: string;
  maxShDegree: SHDegree;
  /** Zero disables load-time tree building, not selection of prebuilt trees. */
  lodCapacitySplats: number;
  /** Resident streaming splats, NOT bytes or a process memory guarantee. */
  residencyCapacitySplats: number;
}>;

export type RenderOptions = Readonly<{
  paused: boolean;
  renderScale: number;
  shDegree: SHDegree;
}>;

/** Immutable collider transaction. An empty requestId releases walk mode. */
export type ColliderRequest = Readonly<{
  requestId: string;
  /** Absolute, readable local path to a collider GLB. */
  filePath: string;
}>;

/** The walker's shape in walk mode, in meters. */
export type Character = Readonly<{
  eyeHeight: number;
  bodyRadius: number;
  stepHeight: number;
}>;

/**
 * What a collider load reports. Compare against these rather than the strings: the native
 * adapters and the codegen spec spell the same values, and the test suite keeps them equal.
 */
export const ColliderPhase = {
  /** Walk mode is on and the walker stands on the collider. */
  ready: 'ready',
  failed: 'failed',
} as const;
export type ColliderPhase = (typeof ColliderPhase)[keyof typeof ColliderPhase];

export type ColliderEvent = Readonly<{
  requestId: string;
  phase: ColliderPhase;
  errorCode: string;
  message: string;
}>;

export type CameraPose = Readonly<{
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch: number;
}>;

/** Values must come from the native adapter, never a JS device-name lookup. */
export type SplatLimits = Readonly<{
  maxLodCapacitySplats: number;
  minResidencyCapacitySplats: number;
  maxResidencyCapacitySplats: number;
}>;

/** What a world load reports. See {@link ColliderPhase} on comparing against constants. */
export const WorldPhase = {
  /** Decoded and on the GPU; nothing has been drawn with it yet. */
  uploaded: 'uploaded',
  /** The first frame containing the world has been presented. */
  frameReady: 'frameReady',
  failed: 'failed',
} as const;
export type WorldPhase = (typeof WorldPhase)[keyof typeof WorldPhase];

export type WorldEvent = Readonly<{
  requestId: string;
  phase: WorldPhase;
  loadedSplats: number;
  /** Empty on success; adapters must supply a stable code on failure. */
  errorCode: string;
  message: string;
}>;

function integer(name: string, value: number, min: number, max: number): void {
  if (!Number.isSafeInteger(value) || value < min || value > max) {
    throw new RangeError(`${name} must be an integer in [${min}, ${max}]`);
  }
}

/** Structural validation only: filesystem access and GPU limits remain native. */
export function validateWorldRequest(request: WorldRequest, limits: SplatLimits): void {
  if (typeof request.requestId !== 'string' || request.requestId.trim().length === 0) {
    throw new TypeError('requestId must be a nonempty string');
  }
  if (!localPath(request.filePath)) {
    throw new TypeError('filePath must be an absolute local file path, not a URL');
  }
  integer('maxLodCapacitySplats', limits.maxLodCapacitySplats, 0, 0x7fffffff);
  integer('minResidencyCapacitySplats', limits.minResidencyCapacitySplats, 1, 0x7fffffff);
  integer('maxResidencyCapacitySplats', limits.maxResidencyCapacitySplats,
    limits.minResidencyCapacitySplats, 0x7fffffff);
  integer('maxShDegree', request.maxShDegree, 0, 3);
  integer('lodCapacitySplats', request.lodCapacitySplats, 0, limits.maxLodCapacitySplats);
  integer('residencyCapacitySplats', request.residencyCapacitySplats,
    limits.minResidencyCapacitySplats, limits.maxResidencyCapacitySplats);
}

function localPath(value: unknown): value is string {
  return typeof value === 'string' && value.startsWith('/') && !value.startsWith('//') &&
    value !== '/' && !value.includes('\0');
}

export function validateColliderRequest(request: ColliderRequest): void {
  if (typeof request.requestId !== 'string' || request.requestId.trim().length === 0) {
    throw new TypeError('requestId must be a nonempty string');
  }
  if (!localPath(request.filePath)) {
    throw new TypeError('filePath must be an absolute local file path, not a URL');
  }
}

/** Native refuses these too; failing here names the field instead of keeping the old walker. */
export function validateCharacter(character: Character): void {
  const {eyeHeight, bodyRadius, stepHeight} = character;
  if (!Number.isFinite(eyeHeight) || eyeHeight <= 0 || eyeHeight > 100) {
    throw new RangeError('eyeHeight must be finite and in (0, 100]');
  }
  if (!Number.isFinite(bodyRadius) || bodyRadius < 0 || bodyRadius >= eyeHeight) {
    throw new RangeError('bodyRadius must be finite, not negative and under eyeHeight');
  }
  if (!Number.isFinite(stepHeight) || stepHeight < 0 || stepHeight >= eyeHeight) {
    throw new RangeError('stepHeight must be finite, not negative and under eyeHeight');
  }
}

export function validateRenderOptions(options: RenderOptions): void {
  if (typeof options.paused !== 'boolean') throw new TypeError('paused must be boolean');
  if (!Number.isFinite(options.renderScale) || options.renderScale < 0.1 || options.renderScale > 2) {
    throw new RangeError('renderScale must be finite and in [0.1, 2]');
  }
  integer('shDegree', options.shDegree, 0, 3);
}

/** Zero may be a real measurement; native availability must be explicit. */
export function optionalTimingMillis(available: boolean, value: number): number | null {
  return available && Number.isFinite(value) && value >= 0 ? value : null;
}
