/**
 * SplatKit React Native example: one full-screen SplatKitView you can walk through.
 *
 * Drag anywhere to look around and push the thumb stick to walk once the collider is
 * ready. The SDK draws no walking control and no HUD: the stick, the stats card and the
 * quality picker below are this app's own, and they drive the view through props and
 * SplatKitCommands.
 *
 * @format
 */

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  Platform,
  Pressable,
  StatusBar,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import {
  SafeAreaProvider,
  useSafeAreaInsets,
} from 'react-native-safe-area-context';
import {
  ColliderPhase,
  PolicyPhase,
  QualityPreset,
  SplatKitBuilder,
  SplatKitCommands,
  SplatKitView,
  SplatKitViewProps,
  WorldPhase,
  conservativeCapabilities,
  nativeCapabilitiesFromEvent,
  optionalTimingMillis,
  toNativePolicyProp,
  toNativeViewProps,
} from '@splatkit/react-native';
import Hud, { GRAPH_SAMPLES, RenderSettings, RenderStats } from './Hud';
import Joystick from './Joystick';
import { ROUTE_SECONDS, poseAt } from './Flythrough';

type EventOf<
  K extends
    | 'onCapabilities'
    | 'onWorldEvent'
    | 'onStats'
    | 'onPolicyEvent'
    | 'onColliderEvent',
> = Parameters<NonNullable<SplatKitViewProps[K]>>[0];

/** The first policy revision; each capabilities report and preset change takes the next. */
const FIRST_REVISION = 1;

/** Meters per second at the rim of the stick: an unhurried walk. */
const WALK_SPEED = 1.4;

/** The walker: eye height, shoulder width and the rise it can climb, in meters. */
const WALKER = { eyeHeight: 1.5, bodyRadius: 0.35, stepHeight: 0.35 } as const;

const WORLD_REQUEST = 'world';
const COLLIDER_REQUEST = 'collider';

/** Clears the HUD's preset row, which sits 34pt up and stands about 26pt tall. */
const STATUS_LIFT = 72;

/**
 * Shown under the picker. The world is not part of this repository, and a Creative Commons
 * scene has to name its author wherever it is shown.
 */
const WORLD_CREDIT: string | null =
  'Les Tanins by Stéphane Agullo  |  CC BY 4.0  |  colours adjusted';

const MILLIS_PER_SECOND = 1000;

/**
 * The example opens at the sharpest preset: a phone renders a scene this size comfortably,
 * and anything coarser leaves distant geometry visibly soft. The picker moves it down.
 */
const INITIAL_PRESET: QualityPreset = QualityPreset.high;

/**
 * How far a LOD node may drift from the full-detail scene, in pixels of the drawn frame.
 * The presets choose a value per tier; this app asks for less than any of them, because
 * the far half of a captured room is where the error shows and a phone can afford it.
 */
const LOD_ERROR_PIXELS = 0.5;

/**
 * Depth sort is the single most expensive stage in a dense scene: 32-bit keys cost over
 * 50ms a frame here, and 16 bits order a room-sized world indistinguishably.
 */
const SORT_DEPTH = 16;

/**
 * The most splats one frame may select, per tier. Past about two million drawn the depth
 * sort costs as much as the rasteriser in a dense room, so the top tiers stop well short of
 * the four million the backend allows. The top two share a budget, which makes switching
 * between them a change of resolution only: a different budget reloads the world.
 */
const LOD_BUDGETS: Readonly<Record<QualityPreset, number>> = Object.freeze({
  [QualityPreset.highEnd]: 2_500_000,
  [QualityPreset.high]: 2_500_000,
  [QualityPreset.balanced]: 2_000_000,
  [QualityPreset.performance]: 1_000_000,
});

/**
 * Metal's LOD capacity is a constant of the backend, not of the device, so on iOS the first
 * request can already ask for it. Elsewhere the first request is built against the limits
 * every adapter accepts, because the real ones only arrive with the engine.
 */
const METAL_LOD_CAPACITY_SPLATS = 4_000_000;

const INITIAL_CAPABILITIES: typeof conservativeCapabilities =
  Platform.OS === 'ios'
    ? {
        ...conservativeCapabilities,
        limits: {
          ...conservativeCapabilities.limits,
          maxLodCapacitySplats: METAL_LOD_CAPACITY_SPLATS,
        },
      }
    : conservativeCapabilities;

/**
 * The share of the screen's pixels each tier draws per axis. These scenes are fill-bound, so
 * resolution is what the picker really trades: the frame's cost follows the pixels covered,
 * not the splats drawn. The app opens below full resolution because a minute of it heats a
 * phone until it throttles.
 */
const RENDER_SCALES: Readonly<Record<QualityPreset, number>> = Object.freeze({
  [QualityPreset.highEnd]: 1,
  [QualityPreset.high]: 0.7,
  [QualityPreset.balanced]: 0.6,
  [QualityPreset.performance]: 0.5,
});

/** One pass out and one back, then the camera parks and the renderer goes idle. */
const FLIGHT_SECONDS = ROUTE_SECONDS * 2;

type Props = Readonly<{
  // Set by MainActivity on Android and SceneDelegate on iOS; see README.md.
  worldPath: string;
  colliderPath: string;
}>;

function App(props: Props) {
  return (
    <SafeAreaProvider>
      <StatusBar barStyle="light-content" />
      <Splat {...props} />
    </SafeAreaProvider>
  );
}

function Splat({ worldPath, colliderPath }: Props) {
  const insets = useSafeAreaInsets();
  const view = useRef<React.ComponentRef<typeof SplatKitView>>(null);
  // Every new configuration needs a new policy revision. The engine reports its own limits in onCapabilities, which only arrive once it exists:
  // the first world request is built against the limits every adapter accepts.
  const [capabilities, setCapabilities] = useState({
    value: INITIAL_CAPABILITIES,
    revision: FIRST_REVISION,
  });
  const [preset, setPreset] = useState<QualityPreset>(INITIAL_PRESET);
  const [status, setStatus] = useState<string | null>('Loading world');
  const [walking, setWalking] = useState(false);
  const [flying, setFlying] = useState(false);
  const [orbitRequested, setOrbitRequested] = useState(false);
  const [closerRequested, setCloserRequested] = useState(false);
  // A loaded collider puts the engine in walk mode, where every pose settles onto the floor
  // and fights a scripted route. The route flies collider-free; taking control loads it.
  const [control, setControl] = useState(false);
  const [stats, setStats] = useState<RenderStats | null>(null);
  const [samples, setSamples] = useState<readonly number[]>([]);

  const configuration = useMemo(
    () =>
      new SplatKitBuilder()
        .withWorld({
          requestId: WORLD_REQUEST,
          filePath: worldPath,
          maxShDegree: 3,
        })
        .withPreset(preset)
        .withPerformance({
          lodErrorPixels: LOD_ERROR_PIXELS,
          sortDepth: SORT_DEPTH,
          lodBudgetSplats: LOD_BUDGETS[preset],
          renderScale: RENDER_SCALES[preset],
        })
        .build(capabilities.value),
    [worldPath, preset, capabilities.value],
  );

  const { renderScale, lodBudgetSplats, lodErrorPixels, sortDepth } =
    configuration.performance.effective;
  const settings = useMemo<RenderSettings>(
    () => ({ renderScale, lodBudgetSplats, lodErrorPixels, sortDepth }),
    [renderScale, lodBudgetSplats, lodErrorPixels, sortDepth],
  );

  // The LOD budget is fixed when a world loads, so a preset that asks for a different one
  // needs a new request. Only the requested budget makes one: the real limits arriving with
  // the engine never do, because reloading a world this size is seconds of tree build and a
  // second memory peak. Every other policy field reaches the engine live.
  const budget = configuration.performance.requested.lodBudgetSplats;
  const request = useRef<{
    budget: number;
    world: SplatKitViewProps['world'];
  } | null>(null);
  if (request.current === null || request.current.budget !== budget) {
    request.current = {
      budget,
      world: {
        ...configuration.world,
        requestId: `${WORLD_REQUEST}-${budget}`,
      },
    };
  }
  const world = request.current.world;

  const onCapabilities = useCallback((event: EventOf<'onCapabilities'>) => {
    const value = nativeCapabilitiesFromEvent(event.nativeEvent);
    setCapabilities(previous => ({ value, revision: previous.revision + 1 }));
  }, []);

  const onWorldEvent = useCallback(
    (event: EventOf<'onWorldEvent'>) => {
      const { phase, loadedSplats, message } = event.nativeEvent;
      if (phase === WorldPhase.failed) {
        setStatus(`Could not load ${worldPath}: ${message}`);
        return;
      }
      // A large world takes seconds to decode, build and upload, and the first frame comes
      // later still. The status line carries the load until then and the HUD takes over.
      setStatus(
        phase === WorldPhase.frameReady
          ? null
          : `${loadedSplats.toLocaleString()} splats uploaded`,
      );
      // The benchmark starts itself, so a run needs no hands and every run is the same run.
      if (phase === WorldPhase.frameReady && !control) setFlying(true);
    },
    [worldPath, control],
  );

  const onColliderEvent = useCallback((event: EventOf<'onColliderEvent'>) => {
    const { phase, message } = event.nativeEvent;
    setWalking(phase === ColliderPhase.ready);
    if (phase === ColliderPhase.failed) setStatus(`Collider failed: ${message}`);
  }, []);

  const onStats = useCallback((event: EventOf<'onStats'>) => {
    const {
      loadedSplats,
      drawnSplats,
      frameMillis,
      frameTimingAvailable,
      gpuMillis,
      gpuTimingAvailable,
      sortMillis,
      sortTimingAvailable,
    } = event.nativeEvent;
    const frame = optionalTimingMillis(frameTimingAvailable, frameMillis);
    // An idle renderer reports no frame time; the graph holds still rather than dropping to 0.
    if (frame !== null && frame > 0) {
      setSamples(previous =>
        [...previous, MILLIS_PER_SECOND / frame].slice(-GRAPH_SAMPLES),
      );
    }
    setStats({
      loadedSplats,
      drawnSplats,
      frameMillis: frame,
      gpuMillis: optionalTimingMillis(gpuTimingAvailable, gpuMillis),
      sortMillis: optionalTimingMillis(sortTimingAvailable, sortMillis),
    });
  }, []);

  const onPolicyEvent = useCallback((event: EventOf<'onPolicyEvent'>) => {
    const { phase, message } = event.nativeEvent;
    if (phase === PolicyPhase.rejected)
      console.warn(`SplatKit policy rejected: ${message}`);
  }, []);

  // The route is driven straight from a frame callback: no React state per frame, so the
  // measurement reflects the renderer rather than the bridge. Any touch hands control back.
  useEffect(() => {
    if (!flying) return undefined;
    let frame = 0;
    const started = Date.now();
    const step = () => {
      const target = view.current;
      const elapsed = (Date.now() - started) / MILLIS_PER_SECOND;
      if (elapsed >= FLIGHT_SECONDS) {
        setFlying(false);
        return;
      }
      if (target) {
        const pose = poseAt(elapsed);
        SplatKitCommands.setCameraPose(
          target,
          pose.x,
          pose.y,
          pose.z,
          pose.yaw,
          pose.pitch,
        );
      }
      frame = requestAnimationFrame(step);
    };
    frame = requestAnimationFrame(step);
    return () => cancelAnimationFrame(frame);
  }, [flying]);

  useEffect(() => {
    if (!orbitRequested) return;
    const target = view.current;
    if (target) {
      SplatKitCommands.dolly(target, -0.8);
      SplatKitCommands.animateOrbit(target, 360, 12, false);
    }
    setOrbitRequested(false);
  }, [orbitRequested]);

  useEffect(() => {
    if (!closerRequested) return;
    const target = view.current;
    if (target) SplatKitCommands.dolly(target, -0.3);
    setCloserRequested(false);
  }, [closerRequested]);

  // Straight to the native view, so the stick moves the camera with no React commit.
  const onStick = useCallback((forward: number, right: number) => {
    const target = view.current;
    if (target)
      SplatKitCommands.setWalkVelocity(
        target,
        forward * WALK_SPEED,
        right * WALK_SPEED,
      );
  }, []);

  return (
    <View style={styles.container}>
      <SplatKitView
        ref={view}
        style={StyleSheet.absoluteFill}
        {...toNativeViewProps(configuration)}
        world={world}
        collider={
          control
            ? { requestId: COLLIDER_REQUEST, filePath: colliderPath }
            : undefined
        }
        character={control ? WALKER : undefined}
        policy={toNativePolicyProp(configuration, capabilities.revision)}
        onCapabilities={onCapabilities}
        onWorldEvent={onWorldEvent}
        onColliderEvent={onColliderEvent}
        onStats={onStats}
        onPolicyEvent={onPolicyEvent}
      />
      <Hud
        stats={stats}
        samples={samples}
        settings={settings}
        credit={WORLD_CREDIT}
        preset={preset}
        onPreset={setPreset}
        top={insets.top}
        bottom={insets.bottom}
      />
      {walking && (
        <View style={[styles.stick, { bottom: insets.bottom + 84 }]}>
          <Joystick onChange={onStick} />
        </View>
      )}
      {!control && status === null && (
        <View style={[styles.modes, { bottom: insets.bottom + STATUS_LIFT }]}>
          <Pressable
            onPress={() => setFlying(current => !current)}
            style={styles.fly}
          >
            <Text style={styles.flyText}>
              {flying ? 'Stop' : 'Fly route'}
            </Text>
          </Pressable>
          <Pressable
            onPress={() => {
              setFlying(false);
              setOrbitRequested(true);
            }}
            style={styles.fly}
          >
            <Text style={styles.flyText}>Orbit 360°</Text>
          </Pressable>
          <Pressable
            onPress={() => {
              setFlying(false);
              setCloserRequested(true);
            }}
            style={styles.fly}
          >
            <Text style={styles.flyText}>Closer</Text>
          </Pressable>
          <Pressable
            onPress={() => {
              setFlying(false);
              setControl(true);
            }}
            style={styles.fly}
          >
            <Text style={styles.flyText}>Walk</Text>
          </Pressable>
        </View>
      )}
      {status !== null && (
        <View
          pointerEvents="none"
          style={[styles.statusCard, { bottom: insets.bottom + STATUS_LIFT }]}
        >
          <Text style={styles.status}>{status}</Text>
        </View>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: 'black',
  },
  stick: {
    position: 'absolute',
    left: 28,
  },
  modes: {
    position: 'absolute',
    right: 16,
    flexDirection: 'row',
    gap: 6,
  },
  fly: {
    paddingHorizontal: 12,
    paddingVertical: 8,
    borderRadius: 999,
    backgroundColor: 'rgba(17, 17, 20, 0.62)',
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: 'rgba(255, 255, 255, 0.12)',
  },
  flyText: {
    color: 'rgba(255, 255, 255, 0.85)',
    fontSize: 11,
    fontWeight: '600',
  },
  statusCard: {
    position: 'absolute',
    left: 16,
    right: 16,
    paddingHorizontal: 12,
    paddingVertical: 8,
    borderRadius: 12,
    backgroundColor: 'rgba(17, 17, 20, 0.72)',
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: 'rgba(255, 255, 255, 0.12)',
  },
  status: {
    color: 'rgba(255, 255, 255, 0.92)',
    fontSize: 13,
    lineHeight: 17,
    textAlign: 'center',
  },
});

export default App;
