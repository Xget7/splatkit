/**
 * An autonomous camera route for benchmarking, derived from the world's own collider.
 *
 * The waypoints come from scripts that voxelise the collider, dilate it by a clearance
 * radius and take the longest path through the connected free space, so the camera floats
 * through rooms and doorways without ever intersecting geometry. Flying rather than walking
 * keeps the measurement about rendering: no gravity, no step resolution, no input.
 */

export type Waypoint = Readonly<{ x: number; y: number; z: number }>;

export type Pose = Readonly<{
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch: number;
}>;

/** Metres per second along the route: slow enough to read, fast enough to finish. */
export const FLY_SPEED = 0.9;

/** Seconds spent easing into and out of each end of the route. */
const EASE_SECONDS = 1.5;

/**
 * Metres either side of the camera that its heading is measured across. Wide enough to span
 * the route's vertical climbs, where a short lookahead finds no horizontal motion to face.
 */
const HEADING_SPAN = 0.9;

/** Radians the camera may tilt: a climb reads as a glance up, never a stare at the floor. */
const MAX_PITCH = 0.3;

export const ROUTE: readonly Waypoint[] = Object.freeze([
  { x: -11.34, y: 1.11, z: 2.17 },
  { x: -10.74, y: 1.11, z: 2.17 },
  { x: -10.14, y: 1.11, z: 2.17 },
  { x: -9.54, y: 1.11, z: 2.17 },
  { x: -9.24, y: 1.41, z: 2.17 },
  { x: -9.24, y: 2.01, z: 2.17 },
  { x: -9.24, y: 2.01, z: 2.77 },
  { x: -9.24, y: 2.01, z: 3.37 },
  { x: -8.94, y: 2.01, z: 3.67 },
  { x: -8.64, y: 2.01, z: 3.97 },
  { x: -8.04, y: 2.01, z: 3.97 },
  { x: -7.44, y: 2.01, z: 3.97 },
  { x: -6.84, y: 2.01, z: 3.97 },
  { x: -6.24, y: 2.01, z: 3.97 },
  { x: -5.94, y: 2.01, z: 3.67 },
  { x: -5.94, y: 2.31, z: 3.37 },
  { x: -5.34, y: 2.31, z: 3.37 },
  { x: -4.74, y: 2.31, z: 3.37 },
  { x: -4.14, y: 2.31, z: 3.37 },
  { x: -3.54, y: 2.31, z: 3.37 },
  { x: -2.94, y: 2.31, z: 3.37 },
  { x: -2.34, y: 2.31, z: 3.37 },
  { x: -2.04, y: 2.31, z: 3.07 },
  { x: -2.04, y: 2.31, z: 2.47 },
  { x: -2.04, y: 2.31, z: 1.87 },
  { x: -2.04, y: 2.31, z: 1.27 },
]);

function distance(a: Waypoint, b: Waypoint): number {
  return Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z);
}

/** Cumulative arc length at each waypoint, so the camera moves at a constant speed. */
const lengths: readonly number[] = ROUTE.reduce<number[]>((all, point, index) => {
  all.push(index === 0 ? 0 : all[index - 1] + distance(ROUTE[index - 1], point));
  return all;
}, []);

export const ROUTE_LENGTH = lengths[lengths.length - 1];

/** One full pass, plus the ease at each end. */
export const ROUTE_SECONDS = ROUTE_LENGTH / FLY_SPEED + EASE_SECONDS * 2;

/**
 * Catmull-Rom keeps the path smooth through the waypoints rather than cornering at each
 * one, which is what a hand-flown camera looks like and what a benchmark should sample.
 */
function spline(a: Waypoint, b: Waypoint, c: Waypoint, d: Waypoint, t: number): Waypoint {
  const t2 = t * t;
  const t3 = t2 * t;
  const axis = (p: Waypoint, q: Waypoint, r: Waypoint, s: Waypoint, key: keyof Waypoint) =>
    0.5 *
    (2 * q[key] +
      (-p[key] + r[key]) * t +
      (2 * p[key] - 5 * q[key] + 4 * r[key] - s[key]) * t2 +
      (-p[key] + 3 * q[key] - 3 * r[key] + s[key]) * t3);
  return { x: axis(a, b, c, d, 'x'), y: axis(a, b, c, d, 'y'), z: axis(a, b, c, d, 'z') };
}

function at(travelled: number): Waypoint {
  const clamped = Math.max(0, Math.min(ROUTE_LENGTH, travelled));
  let segment = 0;
  while (segment < lengths.length - 2 && lengths[segment + 1] < clamped) {
    segment += 1;
  }
  const span = lengths[segment + 1] - lengths[segment];
  const t = span > 0 ? (clamped - lengths[segment]) / span : 0;
  const index = (i: number) => ROUTE[Math.max(0, Math.min(ROUTE.length - 1, i))];
  return spline(index(segment - 1), index(segment), index(segment + 1), index(segment + 2), t);
}

function heading(travelled: number): { yaw: number; pitch: number } {
  // A central difference, so the ends of the route still have a heading: one side clamps
  // to the endpoint and the other supplies the direction.
  const ahead = at(travelled + HEADING_SPAN);
  const behind = at(travelled - HEADING_SPAN);
  const dx = ahead.x - behind.x;
  const dy = ahead.y - behind.y;
  const dz = ahead.z - behind.z;
  const flat = Math.hypot(dx, dz);
  return { yaw: Math.atan2(dx, dz), pitch: flat > 0 ? Math.atan2(dy, flat) : 0 };
}

function smoothstep(t: number): number {
  const u = Math.max(0, Math.min(1, t));
  return u * u * (3 - 2 * u);
}

/**
 * The pose this many seconds into the route. The camera faces its direction of travel, and
 * time past the end reverses, so the route runs as long as the benchmark does. It dwells at
 * each end for twice EASE_SECONDS and spends the dwell panning half a turn, so the reversal
 * is a look around the room rather than a cut.
 */
export function poseAt(seconds: number): Pose {
  const period = ROUTE_SECONDS * 2;
  const phase = ((seconds % period) + period) % period;
  const forward = phase < ROUTE_SECONDS;
  const local = forward ? phase : period - phase;
  const travelled = Math.max(
    0,
    Math.min(ROUTE_LENGTH, (local - EASE_SECONDS) * FLY_SPEED),
  );

  const along = heading(travelled);
  // Progress through the half turn: 0 entering a dwell, 1 leaving it, across both ends.
  const farTurn = smoothstep(
    (phase - (ROUTE_SECONDS - EASE_SECONDS)) / (EASE_SECONDS * 2),
  );
  const nearTurn = smoothstep(
    (((phase + EASE_SECONDS) % period) - 0) / (EASE_SECONDS * 2),
  );
  const inNearDwell = phase < EASE_SECONDS || phase >= period - EASE_SECONDS;
  const turn = inNearDwell ? 1 + nearTurn : farTurn;

  return {
    ...at(travelled),
    yaw: along.yaw + Math.PI * turn,
    pitch:
      Math.max(-MAX_PITCH, Math.min(MAX_PITCH, along.pitch)) *
      (forward ? 1 : -1),
  };
}
