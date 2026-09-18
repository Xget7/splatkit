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

import { useCallback, useMemo, useRef, useState } from 'react';
import { StatusBar, StyleSheet, Text, View } from 'react-native';
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
import Hud, { RenderStats } from './Hud';
import Joystick from './Joystick';

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

/**
 * The example opens at the sharpest preset: a phone renders a scene this size comfortably,
 * and anything coarser leaves distant geometry visibly soft. The picker moves it down.
 */
const INITIAL_PRESET: QualityPreset = QualityPreset.highEnd;

/**
 * How far a LOD node may drift from the full-detail scene, in pixels of the drawn frame.
 * The presets choose a value per tier; this app asks for less than any of them, because
 * the far half of a captured room is where the error shows and a phone can afford it.
 */
const LOD_ERROR_PIXELS = 0.5;

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
    value: conservativeCapabilities,
    revision: FIRST_REVISION,
  });
  const [preset, setPreset] = useState<QualityPreset>(INITIAL_PRESET);
  const [status, setStatus] = useState<string | null>('Loading world');
  const [walking, setWalking] = useState(false);
  const [stats, setStats] = useState<RenderStats | null>(null);

  const configuration = useMemo(
    () =>
      new SplatKitBuilder()
        .withWorld({
          requestId: WORLD_REQUEST,
          filePath: worldPath,
          maxShDegree: 3,
        })
        .withPreset(preset)
        .withPerformance({ lodErrorPixels: LOD_ERROR_PIXELS })
        .build(capabilities.value),
    [worldPath, preset, capabilities.value],
  );

  // The LOD and residency budgets are read once, while the engine builds the world, so a
  // raised budget only reaches it under a new request. The first build guesses conservative
  // limits and the engine's real ones arrive later, so the id carries the budgets it was
  // built with: it changes exactly when a reload would pick something up, and never else.
  const { lodCapacitySplats, residencyCapacitySplats } = configuration.world;
  const world = useMemo(
    () => ({
      ...configuration.world,
      requestId: `${WORLD_REQUEST}-${lodCapacitySplats}-${residencyCapacitySplats}`,
    }),
    [configuration.world, lodCapacitySplats, residencyCapacitySplats],
  );

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
    },
    [worldPath],
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
    setStats({
      loadedSplats,
      drawnSplats,
      frameMillis: optionalTimingMillis(frameTimingAvailable, frameMillis),
      gpuMillis: optionalTimingMillis(gpuTimingAvailable, gpuMillis),
      sortMillis: optionalTimingMillis(sortTimingAvailable, sortMillis),
    });
  }, []);

  const onPolicyEvent = useCallback((event: EventOf<'onPolicyEvent'>) => {
    const { phase, message } = event.nativeEvent;
    if (phase === PolicyPhase.rejected)
      console.warn(`SplatKit policy rejected: ${message}`);
  }, []);

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
        collider={{ requestId: COLLIDER_REQUEST, filePath: colliderPath }}
        character={WALKER}
        policy={toNativePolicyProp(configuration, capabilities.revision)}
        onCapabilities={onCapabilities}
        onWorldEvent={onWorldEvent}
        onColliderEvent={onColliderEvent}
        onStats={onStats}
        onPolicyEvent={onPolicyEvent}
      />
      <Hud
        stats={stats}
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
      {status !== null && (
        <Text
          pointerEvents="none"
          style={[styles.status, { bottom: insets.bottom + 12 }]}
        >
          {status}
        </Text>
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
  status: {
    position: 'absolute',
    left: 16,
    right: 16,
    color: 'white',
    fontSize: 13,
    textAlign: 'center',
    textShadowColor: 'black',
    textShadowRadius: 4,
  },
});

export default App;
