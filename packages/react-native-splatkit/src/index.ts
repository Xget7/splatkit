// SplatKitView is the Codegen host component; build its props with toNativeViewProps and toNativePolicyProp.
import {Commands} from './specs/SplatViewNativeComponent';

export * from './contracts';
export * from './performance';
export {default as SplatKitView} from './specs/SplatViewNativeComponent';
export type {
  NativeProps as SplatKitViewProps,
  NativeCommands as SplatKitCommandSet,
} from './specs/SplatViewNativeComponent';

/** Imperative navigation commands for a mounted SplatKitView ref. */
export const SplatKitCommands = Commands;
