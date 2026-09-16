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

/** Values must come from the native adapter, never a JS device-name lookup. */
export type SplatLimits = Readonly<{
  maxLodCapacitySplats: number;
  minResidencyCapacitySplats: number;
  maxResidencyCapacitySplats: number;
}>;

export type WorldEvent = Readonly<{
  requestId: string;
  phase: 'uploaded' | 'frameReady' | 'failed';
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
  if (typeof request.filePath !== 'string' || !request.filePath.startsWith('/') ||
      request.filePath.startsWith('//') || request.filePath.includes('\0') ||
      request.filePath === '/') {
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
