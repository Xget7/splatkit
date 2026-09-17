/**
 * SplatKit React Native example: one full-screen SplatKitView.
 * Drag with one finger to look around and with two fingers to walk.
 *
 * @format
 */

import { useCallback, useMemo, useState } from 'react';
import { StatusBar, StyleSheet, Text, View } from 'react-native';
import {
  SafeAreaProvider,
  useSafeAreaInsets,
} from 'react-native-safe-area-context';
import {
  DeviceCapabilities,
  SplatKitBuilder,
  SplatKitView,
  SplatKitViewProps,
  nativeCapabilitiesFromEvent,
  toNativePolicyProp,
  toNativeViewProps,
} from '@splatkit/react-native';

type EventOf<
  K extends 'onCapabilities' | 'onWorldEvent' | 'onStats' | 'onPolicyEvent',
> = Parameters<NonNullable<SplatKitViewProps[K]>>[0];

// Conservative limits for the first build only; each engine reports its own in onCapabilities.
const INITIAL_CAPABILITIES: DeviceCapabilities = {
  limits: {
    maxLodCapacitySplats: 1_000_000,
    minResidencyCapacitySplats: 100_000,
    maxResidencyCapacitySplats: 1_000_000,
  },
  supportsComputeTiles: false,
  supportsHiZOcclusion: false,
  supportsSubgroups: false,
  maxTextureDimension: 4096,
};

type Props = Readonly<{
  // Set by MainActivity on Android and SceneDelegate on iOS; see README.md.
  worldPath: string;
}>;

function App({ worldPath }: Props) {
  return (
    <SafeAreaProvider>
      <StatusBar barStyle="light-content" />
      <Splat worldPath={worldPath} />
    </SafeAreaProvider>
  );
}

function Splat({ worldPath }: Props) {
  const insets = useSafeAreaInsets();
  // Every new configuration needs a new policy revision.
  const [capabilities, setCapabilities] = useState({
    value: INITIAL_CAPABILITIES,
    revision: 1,
  });
  const [status, setStatus] = useState('Loading world');

  const configuration = useMemo(
    () =>
      new SplatKitBuilder()
        .withWorld({ requestId: 'world', filePath: worldPath, maxShDegree: 3 })
        .withPreset('balanced')
        .build(capabilities.value),
    [worldPath, capabilities.value],
  );

  const onCapabilities = useCallback((event: EventOf<'onCapabilities'>) => {
    const value = nativeCapabilitiesFromEvent(event.nativeEvent);
    setCapabilities(previous => ({ value, revision: previous.revision + 1 }));
  }, []);

  const onWorldEvent = useCallback(
    (event: EventOf<'onWorldEvent'>) => {
      const { phase, loadedSplats, message } = event.nativeEvent;
      setStatus(
        phase === 'failed'
          ? `Could not load ${worldPath}: ${message}`
          : `${loadedSplats.toLocaleString()} splats ${phase}`,
      );
    },
    [worldPath],
  );

  const onStats = useCallback((event: EventOf<'onStats'>) => {
    const { drawnSplats, gpuMillis, gpuTimingAvailable } = event.nativeEvent;
    const gpu = gpuTimingAvailable ? `, GPU ${gpuMillis.toFixed(1)} ms` : '';
    setStatus(`${drawnSplats.toLocaleString()} splats drawn${gpu}`);
  }, []);

  const onPolicyEvent = useCallback((event: EventOf<'onPolicyEvent'>) => {
    const { phase, message } = event.nativeEvent;
    if (phase === 'rejected')
      console.warn(`SplatKit policy rejected: ${message}`);
  }, []);

  return (
    <View style={styles.container}>
      <SplatKitView
        style={StyleSheet.absoluteFill}
        {...toNativeViewProps(configuration)}
        policy={toNativePolicyProp(configuration, capabilities.revision)}
        onCapabilities={onCapabilities}
        onWorldEvent={onWorldEvent}
        onStats={onStats}
        onPolicyEvent={onPolicyEvent}
      />
      <Text
        pointerEvents="none"
        style={[styles.status, { bottom: insets.bottom + 12 }]}
      >
        {status}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: 'black',
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
