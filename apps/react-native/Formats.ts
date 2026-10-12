/**
 * The three formats a world ships in, each a copy of the same world in the app's files
 * directory, so the picker compares formats rather than worlds. Preparing a world in the
 * package README says which suits which world; scripts/prepare-world.sh writes all three.
 *
 * @format
 */

import { WorldKind } from '@splatkit/react-native';

export const WorldFormat = {
  /** One compressed file; past the LOD budget the phone builds its tree at load. */
  spz: 'spz',
  /** The same splats with the tree built offline, so the load only reads it. */
  lodsplat: 'lodsplat',
  /** Tiles under coarser levels, streamed in as the camera needs them. */
  tileset: 'tileset',
} as const;
export type WorldFormat = (typeof WorldFormat)[keyof typeof WorldFormat];

export const worldFormats: readonly WorldFormat[] = Object.freeze([
  WorldFormat.spz,
  WorldFormat.lodsplat,
  WorldFormat.tileset,
]);

type FormatFile = Readonly<{
  title: string;
  /** Relative to the files directory. */
  path: string;
  kind: WorldKind;
}>;

export const FORMAT_FILES: Readonly<Record<WorldFormat, FormatFile>> =
  Object.freeze({
    [WorldFormat.spz]: {
      title: 'SPZ',
      path: 'world.spz',
      kind: WorldKind.file,
    },
    [WorldFormat.lodsplat]: {
      title: 'LOD splat',
      path: 'world.lodsplat',
      kind: WorldKind.file,
    },
    // The index names its tiles relative to itself, so the whole folder is copied.
    [WorldFormat.tileset]: {
      title: 'Tileset',
      path: 'world.tiles/tileset.json',
      kind: WorldKind.tileset,
    },
  });

/** The walk collider, shared by all three: they are the same world. */
export const COLLIDER_PATH = 'collider.glb';

type SplatBudgets = Readonly<{
  lodBudgetSplats: number;
  residencyCapacitySplats: number;
}>;

/** The budget a world loads under: a file selects within its LOD budget, a tileset streams. */
export function loadBudget(kind: WorldKind, budgets: SplatBudgets): number {
  return kind === WorldKind.tileset
    ? budgets.residencyCapacitySplats
    : budgets.lodBudgetSplats;
}
