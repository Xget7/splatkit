/**
 * The render HUD: a stats card and a quality picker, over the SplatKitView.
 *
 * Everything here is the app's own UI. The SDK reports numbers through onStats and
 * onCapabilities and draws none of this itself.
 *
 * @format
 */

import { memo } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';
import { QualityPreset, qualityPresets } from '@splatkit/react-native';

export type RenderStats = Readonly<{
  loadedSplats: number;
  drawnSplats: number;
  frameMillis: number | null;
  gpuMillis: number | null;
  sortMillis: number | null;
}>;

const PRESET_TITLES: Readonly<Record<QualityPreset, string>> = Object.freeze({
  [QualityPreset.highEnd]: 'Max',
  [QualityPreset.high]: 'High',
  [QualityPreset.balanced]: 'Balanced',
  [QualityPreset.performance]: 'Fast',
});

/** Above this the frame rate reads as smooth, below the lower one as a problem. */
const SMOOTH_FPS = 50;
const ROUGH_FPS = 25;

function compact(value: number): string {
  if (value >= 1e6) return `${(value / 1e6).toFixed(1)}M`;
  if (value >= 1e3) return `${Math.round(value / 1e3)}k`;
  return `${value}`;
}

function millis(value: number | null): string {
  return value === null ? '-' : `${value.toFixed(0)} ms`;
}

function Row({ label, value }: { label: string; value: string }) {
  return (
    <View style={styles.row}>
      <Text style={styles.rowLabel}>{label}</Text>
      <Text style={styles.rowValue}>{value}</Text>
    </View>
  );
}

function StatsCard({ stats }: { stats: RenderStats | null }) {
  // The engine skips frames while nothing moves, so no frame time is a still view, not 0 fps.
  const frameMillis =
    stats !== null && stats.frameMillis !== null && stats.frameMillis > 0
      ? stats.frameMillis
      : null;
  const fps = frameMillis === null ? null : 1000 / frameMillis;
  const pace =
    fps === null
      ? styles.dotIdle
      : fps >= SMOOTH_FPS
      ? styles.dotSmooth
      : fps >= ROUGH_FPS
      ? styles.dotRough
      : styles.dotSlow;
  return (
    <View style={styles.card} pointerEvents="none">
      <View style={styles.headline}>
        <Text style={styles.fps}>{fps === null ? 'Idle' : fps.toFixed(0)}</Text>
        {fps !== null && <Text style={styles.fpsUnit}>FPS</Text>}
      </View>
      <View style={styles.headline}>
        <View style={[styles.dot, pace]} />
        <Text style={styles.frame}>
          {frameMillis === null ? 'no redraw' : millis(frameMillis)}
        </Text>
      </View>
      <View style={styles.divider} />
      <Row label="Drawn" value={stats ? compact(stats.drawnSplats) : '-'} />
      <Row label="Scene" value={stats ? compact(stats.loadedSplats) : '-'} />
      <Row
        label="GPU/sort"
        value={
          stats ? `${millis(stats.gpuMillis)} / ${millis(stats.sortMillis)}` : '-'
        }
      />
    </View>
  );
}

type Props = Readonly<{
  stats: RenderStats | null;
  preset: QualityPreset;
  onPreset: (preset: QualityPreset) => void;
  top: number;
  bottom: number;
}>;

function Hud({ stats, preset, onPreset, top, bottom }: Props) {
  return (
    <>
      <View style={[styles.cardSlot, { top: top + 8 }]} pointerEvents="none">
        <StatsCard stats={stats} />
      </View>
      <View style={[styles.presets, { bottom: bottom + 34 }]}>
        {qualityPresets.map(value => {
          const active = value === preset;
          return (
            <Pressable
              key={value}
              onPress={() => onPreset(value)}
              style={[styles.preset, active && styles.presetActive]}
            >
              <Text style={[styles.presetText, active && styles.presetTextActive]}>
                {PRESET_TITLES[value]}
              </Text>
            </Pressable>
          );
        })}
      </View>
    </>
  );
}

const styles = StyleSheet.create({
  cardSlot: {
    position: 'absolute',
    left: 16,
  },
  card: {
    width: 150,
    paddingHorizontal: 10,
    paddingVertical: 8,
    borderRadius: 12,
    backgroundColor: 'rgba(17, 17, 20, 0.62)',
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: 'rgba(255, 255, 255, 0.12)',
  },
  headline: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 5,
  },
  fps: {
    color: 'white',
    fontSize: 22,
    fontWeight: '600',
    fontVariant: ['tabular-nums'],
  },
  fpsUnit: {
    color: 'rgba(255, 255, 255, 0.55)',
    fontSize: 9,
    fontWeight: '700',
  },
  dot: {
    width: 5,
    height: 5,
    borderRadius: 2.5,
  },
  dotIdle: {
    backgroundColor: '#9ca3af',
  },
  dotSmooth: {
    backgroundColor: '#4ade80',
  },
  dotRough: {
    backgroundColor: '#facc15',
  },
  dotSlow: {
    backgroundColor: '#f87171',
  },
  frame: {
    color: 'rgba(255, 255, 255, 0.55)',
    fontSize: 9,
    fontWeight: '500',
    fontVariant: ['tabular-nums'],
  },
  divider: {
    height: StyleSheet.hairlineWidth,
    backgroundColor: 'rgba(255, 255, 255, 0.15)',
    marginVertical: 7,
  },
  row: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingVertical: 1,
  },
  rowLabel: {
    color: 'rgba(255, 255, 255, 0.55)',
    fontSize: 10,
  },
  rowValue: {
    color: 'white',
    fontSize: 10,
    fontWeight: '600',
    fontVariant: ['tabular-nums'],
  },
  presets: {
    position: 'absolute',
    left: 0,
    right: 0,
    flexDirection: 'row',
    justifyContent: 'center',
    gap: 6,
  },
  preset: {
    paddingHorizontal: 12,
    paddingVertical: 6,
    borderRadius: 999,
    backgroundColor: 'rgba(17, 17, 20, 0.62)',
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: 'rgba(255, 255, 255, 0.12)',
  },
  presetActive: {
    backgroundColor: 'rgba(255, 255, 255, 0.92)',
    borderColor: 'transparent',
  },
  presetText: {
    color: 'rgba(255, 255, 255, 0.75)',
    fontSize: 11,
    fontWeight: '600',
  },
  presetTextActive: {
    color: '#111114',
  },
});

export default memo(Hud);
