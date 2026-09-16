/**
 * SplatKit React Native dev app.
 *
 * End-to-end smoke test for the Fabric prop/event path: JS -> Codegen -> Kotlin adapter -> JNI
 * -> Vulkan. Renders a full-screen SplatKitView with a compact control overlay.
 *
 * @format
 */

import React, {useMemo, useState} from 'react';
import {
  NativeSyntheticEvent,
  ScrollView,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import {
  DeviceCapabilities,
  NativeCapabilitiesEvent,
  NativeRenderPolicy,
  QualityPreset,
  SortDepth,
  SplatKitBuilder,
  SplatKitConfiguration,
  SplatKitView,
  WorldEvent,
  nativeCapabilitiesFromEvent,
  toNativePolicyProp,
  toNativeViewProps,
} from '@splatkit/react-native';

// The adapter requires an absolute, readable local path. App-specific external storage needs
// no runtime permission and can be filled with `adb push` (see README.md).
const APPLICATION_ID = 'com.splatkit.rndev';
const WORLD_FILE_PATH = `/sdcard/Android/data/${APPLICATION_ID}/files/world.spz`;

// Conservative limits for an unknown Android GPU. Used only for the very first build, because
// the real onCapabilities event can't arrive until an engine exists, and an engine needs a
// world. Replaced by nativeCapabilitiesFromEvent() as soon as that event lands.
const FALLBACK_CAPABILITIES: DeviceCapabilities = {
  limits: {
    maxLodCapacitySplats: 2_200_000,
    minResidencyCapacitySplats: 100_000,
    maxResidencyCapacitySplats: 8_000_000,
  },
  supportsComputeTiles: false,
  supportsHiZOcclusion: false,
  supportsSubgroups: true,
  maxTextureDimension: 16384,
};

const PRESETS: readonly QualityPreset[] = ['highEnd', 'high', 'balanced', 'performance'];
const SUBPIXEL_THRESHOLDS = [0.5, 1, 2] as const;
const RASTER_NAMES: Readonly<Record<number, string>> = {0: 'hardware', 1: 'computeTile', 2: 'hybrid'};

// Mirrors the onStats / onPolicyEvent payload shapes declared in
// packages/react-native-splatkit/src/specs/SplatViewNativeComponent.ts. Those event types are
// local to that spec module (not exported), so the shapes are duplicated here for display.
type StatsEvent = Readonly<{
  requestId: string;
  loadedSplats: number;
  drawnSplats: number;
  frameMillis: number;
  frameTimingAvailable: boolean;
  gpuMillis: number;
  gpuTimingAvailable: boolean;
  sortMillis: number;
  sortTimingAvailable: boolean;
}>;

type PolicyEvent = Readonly<{
  revision: number;
  phase: 'applied' | 'warning' | 'rejected';
  errorCode: string;
  message: string;
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

function logEvent(name: string, payload: unknown): void {
  console.log('[SplatKitRN] ' + name + ' ' + JSON.stringify(payload));
}

function buildConfig(
  worldGeneration: number,
  preset: QualityPreset,
  sortDepth: SortDepth,
  subpixelThreshold: number,
  paused: boolean,
  capabilities: DeviceCapabilities,
): SplatKitConfiguration {
  return new SplatKitBuilder()
    .withWorld({
      requestId: `world-${worldGeneration}`,
      filePath: WORLD_FILE_PATH,
      maxShDegree: 3,
    })
    .withPreset(preset)
    .withPerformance({sortDepth, subpixelThreshold})
    .withRender({paused})
    .build(capabilities);
}

// Native applies a policy once per revision, so the revision advances exactly when the requested
// policy changes. Adjusting state during render keeps the new revision in the same commit as
// the props that caused it, so both reach native in one Fabric transaction.
function usePolicyProp(config: SplatKitConfiguration | null): NativeRenderPolicy | undefined {
  const content = config ? JSON.stringify(toNativePolicyProp(config, 1)) : null;
  const [tracked, setTracked] = useState({content, revision: 1});
  if (tracked.content !== content) setTracked({content, revision: tracked.revision + 1});
  return config ? toNativePolicyProp(config, tracked.revision) : undefined;
}

export default function App(): React.JSX.Element {
  const [worldGeneration, setWorldGeneration] = useState(1);
  const [worldRemoved, setWorldRemoved] = useState(false);
  const [preset, setPreset] = useState<QualityPreset>('balanced');
  const [sortDepth, setSortDepth] = useState<SortDepth>(16);
  const [subpixelIndex, setSubpixelIndex] = useState(0);
  const [paused, setPaused] = useState(false);
  const [capabilities, setCapabilities] = useState<DeviceCapabilities>(FALLBACK_CAPABILITIES);
  const [capabilitiesSource, setCapabilitiesSource] = useState<'fallback' | 'native'>('fallback');

  const [lastWorldEvent, setLastWorldEvent] = useState<WorldEvent | null>(null);
  const [lastStats, setLastStats] = useState<StatsEvent | null>(null);
  const [lastPolicyEvent, setLastPolicyEvent] = useState<PolicyEvent | null>(null);

  const subpixelThreshold = SUBPIXEL_THRESHOLDS[subpixelIndex];

  // Derived while rendering, never in an effect: an effect would commit the new state with the
  // previous configuration first, and native would apply that stale policy.
  const {config, buildError} = useMemo(() => {
    try {
      return {
        config: buildConfig(worldGeneration, preset, sortDepth, subpixelThreshold, paused, capabilities),
        buildError: null,
      };
    } catch (error) {
      return {config: null, buildError: error instanceof Error ? error.message : String(error)};
    }
  }, [worldGeneration, preset, sortDepth, subpixelThreshold, paused, capabilities]);
  const policy = usePolicyProp(config);

  const toggleSortDepth = (): void => setSortDepth(d => (d === 16 ? 32 : 16));
  const cycleSubpixelThreshold = (): void => setSubpixelIndex(i => (i + 1) % SUBPIXEL_THRESHOLDS.length);

  const reloadWorld = (): void => {
    setWorldGeneration(g => g + 1);
    setWorldRemoved(false);
  };

  const handleWorldEvent = (event: NativeSyntheticEvent<WorldEvent>): void => {
    logEvent('onWorldEvent', event.nativeEvent);
    setLastWorldEvent(event.nativeEvent);
  };

  const handleStats = (event: NativeSyntheticEvent<StatsEvent>): void => {
    logEvent('onStats', event.nativeEvent);
    setLastStats(event.nativeEvent);
  };

  const handlePolicyEvent = (event: NativeSyntheticEvent<PolicyEvent>): void => {
    logEvent('onPolicyEvent', event.nativeEvent);
    setLastPolicyEvent(event.nativeEvent);
  };

  const handleCapabilities = (event: NativeSyntheticEvent<NativeCapabilitiesEvent>): void => {
    logEvent('onCapabilities', event.nativeEvent);
    setCapabilities(nativeCapabilitiesFromEvent(event.nativeEvent));
    setCapabilitiesSource('native');
  };

  const baseViewProps = config ? toNativeViewProps(config) : null;
  const viewProps = baseViewProps && worldRemoved ? {...baseViewProps, world: undefined} : baseViewProps;

  return (
    <View style={styles.container}>
      {viewProps ? (
        <SplatKitView
          style={StyleSheet.absoluteFill}
          {...viewProps}
          policy={policy}
          onWorldEvent={handleWorldEvent}
          onStats={handleStats}
          onPolicyEvent={handlePolicyEvent}
          onCapabilities={handleCapabilities}
        />
      ) : null}

      <ScrollView style={styles.overlay} contentContainerStyle={styles.overlayContent}>
        {buildError ? <Text style={styles.error}>Builder error: {buildError}</Text> : null}

        <View style={styles.row}>
          {PRESETS.map(p => (
            <TouchableOpacity
              key={p}
              onPress={() => setPreset(p)}
              style={[styles.button, preset === p && styles.buttonActive]}>
              <Text style={styles.buttonText}>{p}</Text>
            </TouchableOpacity>
          ))}
        </View>

        <View style={styles.row}>
          <TouchableOpacity style={styles.button} onPress={toggleSortDepth}>
            <Text style={styles.buttonText}>sortDepth {sortDepth}</Text>
          </TouchableOpacity>
          <TouchableOpacity style={styles.button} onPress={cycleSubpixelThreshold}>
            <Text style={styles.buttonText}>subpixel {subpixelThreshold}</Text>
          </TouchableOpacity>
          <TouchableOpacity style={styles.button} onPress={() => setPaused(p => !p)}>
            <Text style={styles.buttonText}>{paused ? 'Resume' : 'Pause'}</Text>
          </TouchableOpacity>
        </View>

        <View style={styles.row}>
          <TouchableOpacity style={styles.button} onPress={reloadWorld}>
            <Text style={styles.buttonText}>Reload world</Text>
          </TouchableOpacity>
          <TouchableOpacity style={styles.button} onPress={() => setWorldRemoved(true)}>
            <Text style={styles.buttonText}>Remove world</Text>
          </TouchableOpacity>
        </View>

        <Text style={styles.sectionTitle}>World event</Text>
        <Text style={styles.mono}>
          {lastWorldEvent
            ? `phase=${lastWorldEvent.phase} loaded=${lastWorldEvent.loadedSplats} ` +
              `errorCode=${lastWorldEvent.errorCode || '(none)'} message=${lastWorldEvent.message || '(none)'}`
            : '(none yet)'}
        </Text>

        <Text style={styles.sectionTitle}>Stats</Text>
        <Text style={styles.mono}>
          {lastStats ? `loaded=${lastStats.loadedSplats} drawn=${lastStats.drawnSplats}` : '(none yet)'}
        </Text>

        <Text style={styles.sectionTitle}>Capabilities ({capabilitiesSource})</Text>
        <Text style={styles.mono}>
          {`lod<=${capabilities.limits.maxLodCapacitySplats} ` +
            `residency=[${capabilities.limits.minResidencyCapacitySplats},` +
            `${capabilities.limits.maxResidencyCapacitySplats}] ` +
            `computeTiles=${capabilities.supportsComputeTiles} hiZ=${capabilities.supportsHiZOcclusion} ` +
            `subgroups=${capabilities.supportsSubgroups} maxTex=${capabilities.maxTextureDimension} ` +
            `policy=${capabilities.policy ? 'reported' : '(not yet reported)'}`}
        </Text>

        <Text style={styles.sectionTitle}>Policy event</Text>
        <Text style={styles.mono}>
          {lastPolicyEvent
            ? `rev=${lastPolicyEvent.revision} phase=${lastPolicyEvent.phase} ` +
              `errorCode=${lastPolicyEvent.errorCode || '(none)'} message=${lastPolicyEvent.message || '(none)'} ` +
              `raster=${RASTER_NAMES[lastPolicyEvent.raster] ?? lastPolicyEvent.raster} ` +
              `sortDepth=${lastPolicyEvent.sortDepth} subpixel=${lastPolicyEvent.subpixelThreshold}`
            : '(none yet)'}
        </Text>

        <Text style={styles.sectionTitle}>Builder diagnostics</Text>
        <Text style={styles.mono}>
          {config && config.performance.diagnostics.length > 0
            ? config.performance.diagnostics.map(d => `${d.code} ${d.option}: ${d.message}`).join('\n')
            : '(none)'}
        </Text>
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#000',
  },
  overlay: {
    position: 'absolute',
    left: 0,
    right: 0,
    bottom: 0,
    maxHeight: '58%',
    backgroundColor: 'rgba(0,0,0,0.62)',
  },
  overlayContent: {
    padding: 10,
    // Fixed clearance for the gesture nav bar / 3-button nav, in place of a bottom safe-area
    // inset (no react-native-safe-area-context dependency).
    paddingBottom: 24,
    gap: 6,
  },
  row: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 6,
  },
  button: {
    paddingVertical: 6,
    paddingHorizontal: 10,
    borderRadius: 6,
    backgroundColor: 'rgba(255,255,255,0.14)',
  },
  buttonActive: {
    backgroundColor: 'rgba(88,166,255,0.55)',
  },
  buttonText: {
    color: '#fff',
    fontSize: 12,
    fontWeight: '600',
  },
  sectionTitle: {
    color: '#8ab4ff',
    fontSize: 11,
    fontWeight: '700',
    marginTop: 4,
  },
  mono: {
    color: '#eee',
    fontSize: 11,
  },
  error: {
    color: '#ff6b6b',
    fontSize: 12,
    fontWeight: '700',
  },
});
