import {
  MAX_INT32,
  MAX_RENDER_SCALE,
  MAX_SH_DEGREE,
  MIN_RENDER_SCALE,
  integer,
  range,
} from './validation';

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

function localPath(value: unknown): value is string {
  return typeof value === 'string' && value.startsWith('/') && !value.startsWith('//') &&
    value !== '/' && !value.includes('\0');
}

function validateTransaction(request: Readonly<{requestId: string; filePath: string}>): void {
  if (typeof request.requestId !== 'string' || request.requestId.trim().length === 0) {
    throw new TypeError('requestId must be a nonempty string');
  }
  if (!localPath(request.filePath)) {
    throw new TypeError('filePath must be an absolute local file path, not a URL');
  }
}

/** Structural validation only: filesystem access and GPU limits remain native. */
export function validateWorldRequest(request: WorldRequest, limits: SplatLimits): void {
  validateTransaction(request);
  integer('maxLodCapacitySplats', limits.maxLodCapacitySplats, 0, MAX_INT32);
  integer('minResidencyCapacitySplats', limits.minResidencyCapacitySplats, 1, MAX_INT32);
  integer('maxResidencyCapacitySplats', limits.maxResidencyCapacitySplats,
    limits.minResidencyCapacitySplats, MAX_INT32);
  integer('maxShDegree', request.maxShDegree, 0, MAX_SH_DEGREE);
  integer('lodCapacitySplats', request.lodCapacitySplats, 0, limits.maxLodCapacitySplats);
  integer('residencyCapacitySplats', request.residencyCapacitySplats,
    limits.minResidencyCapacitySplats, limits.maxResidencyCapacitySplats);
}

export function validateColliderRequest(request: ColliderRequest): void {
  validateTransaction(request);
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
  range('renderScale', options.renderScale, MIN_RENDER_SCALE, MAX_RENDER_SCALE);
  integer('shDegree', options.shDegree, 0, MAX_SH_DEGREE);
}

/** Zero may be a real measurement; native availability must be explicit. */
export function optionalTimingMillis(available: boolean, value: number): number | null {
  return available && Number.isFinite(value) && value >= 0 ? value : null;
}

/** Stable native camera modes. First person walks when a collider is available. */
export const CameraMode = {firstPerson: 0, orbit: 1} as const;
export type CameraMode = (typeof CameraMode)[keyof typeof CameraMode];
export const CameraPhase = {applied: 'applied', rejected: 'rejected'} as const;
export type CameraPhase = (typeof CameraPhase)[keyof typeof CameraPhase];

export type WorldPoint = Readonly<{x: number; y: number; z: number}>;

/** Bump revision per change. Commands may move the camera until the next revision. */
export type CameraRequest = Readonly<{revision: number}> & (
  | Readonly<{mode: typeof CameraMode.firstPerson}>
  | Readonly<{
      mode: typeof CameraMode.orbit;
      anchor: WorldPoint;
      radius: number;
      /** Radians. Native clamps elevation to its supported range. */
      azimuth: number;
      elevation: number;
      /** Signed continuous azimuth rate; zero stops. Separate from animateOrbit. */
      orbitRadiansPerSecond: number;
    }>
);

/** Flat Codegen transport; prefer toNativeCameraProp for constructing it. */
export type NativeCameraRequest = Readonly<{
  revision: number;
  mode: CameraMode;
  anchorX: number;
  anchorY: number;
  anchorZ: number;
  radius: number;
  azimuth: number;
  elevation: number;
  orbitRadiansPerSecond: number;
}>;

/** Snapshot at application time, not a per-frame subscription. */
export type CameraEvent = Omit<NativeCameraRequest, 'mode'> & Readonly<{
  /** Codegen transports integer modes; compare with CameraMode. */
  mode: number;
  hasAnchor: boolean;
  phase: CameraPhase;
  errorCode: string;
  message: string;
}>;

function cameraNumber(name: string, value: number): void {
  if (!Number.isFinite(value) || !Number.isFinite(Math.fround(value))) {
    throw new RangeError(`${name} must be finite and representable by native`);
  }
}

/** Structural validation only; native owns transitions and effective limits. */
export function validateCameraRequest(request: CameraRequest): void {
  integer('revision', request.revision, 1, MAX_INT32);
  if (request.mode === CameraMode.firstPerson) return;
  if (request.mode !== CameraMode.orbit) throw new RangeError('invalid camera mode');
  if (!request.anchor || typeof request.anchor !== 'object') {
    throw new TypeError('anchor must be a world point');
  }
  cameraNumber('anchor.x', request.anchor.x);
  cameraNumber('anchor.y', request.anchor.y);
  cameraNumber('anchor.z', request.anchor.z);
  cameraNumber('radius', request.radius);
  if (Math.fround(request.radius) <= 0) throw new RangeError('radius must be positive');
  cameraNumber('azimuth', request.azimuth);
  cameraNumber('elevation', request.elevation);
  cameraNumber('orbitRadiansPerSecond', request.orbitRadiansPerSecond);
}

export function toNativeCameraProp(request: CameraRequest): NativeCameraRequest {
  validateCameraRequest(request);
  if (request.mode === CameraMode.firstPerson) {
    return {revision: request.revision, mode: request.mode, anchorX: 0, anchorY: 0,
      anchorZ: 0, radius: 1, azimuth: 0, elevation: 0, orbitRadiansPerSecond: 0};
  }
  return {revision: request.revision, mode: request.mode,
    anchorX: request.anchor.x, anchorY: request.anchor.y, anchorZ: request.anchor.z,
    radius: request.radius, azimuth: request.azimuth, elevation: request.elevation,
    orbitRadiansPerSecond: request.orbitRadiansPerSecond};
}
