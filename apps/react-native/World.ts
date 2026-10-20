/**
 * The example world: one `.lodsplat` file, the detail tree built offline by
 * `scripts/prepare-world.sh --lod`, and its walk collider. Both sit in the app's files
 * directory; README.md says how to copy them there.
 *
 * @format
 */

import {WorldKind} from '@splatkit/react-native';

/** Relative to the files directory. */
export const WORLD_PATH = 'world.lodsplat';
export const WORLD_KIND: WorldKind = WorldKind.file;
export const COLLIDER_PATH = 'collider.glb';
