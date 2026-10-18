// Shared by contracts and performance; not part of the package entry point.

/** The largest value Fabric carries as an Int32 prop. */
export const MAX_INT32 = 0x7fffffff;

export const MAX_SH_DEGREE = 3;

/** The render scale the native adapters accept, as a share of the view's pixels per axis. */
export const MIN_RENDER_SCALE = 0.1;
export const MAX_RENDER_SCALE = 2;

/** Vertical field of view, in degrees, a world request may set. */
export const MIN_FIELD_OF_VIEW_DEGREES = 10;
export const MAX_FIELD_OF_VIEW_DEGREES = 120;
/**
 * The widest far to near plane ratio. The shaders read both planes back from the float
 * projection, and past this the far plane drifts from where it was set.
 */
export const MAX_DEPTH_RANGE_RATIO = 100_000;

export function integer(name: string, value: number, min: number, max: number): void {
  if (!Number.isSafeInteger(value) || value < min || value > max) {
    throw new RangeError(`${name} must be an integer in [${min}, ${max}]`);
  }
}

export function range(name: string, value: number, min: number, max: number): void {
  if (!Number.isFinite(value) || value < min || value > max) {
    throw new RangeError(`${name} must be finite and in [${min}, ${max}]`);
  }
}
