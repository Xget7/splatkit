/**
 * The render HUD: a performance panel, a quality picker and the world's credit, over the
 * SplatKitView.
 *
 * Everything here is the app's own UI. The SDK reports numbers through onStats and
 * onCapabilities and draws none of this itself.
 *
 * @format
 */

import { memo } from 'react';
import {
  PixelRatio,
  Platform,
  Pressable,
  StyleSheet,
  Text,
  View,
  useWindowDimensions,
} from 'react-native';
import { QualityPreset, qualityPresets } from '@splatkit/react-native';

export type RenderStats = Readonly<{
  loadedSplats: number;
  drawnSplats: number;
  frameMillis: number | null;
  gpuMillis: number | null;
  sortMillis: number | null;
}>;

/** The policy the frame is drawn under, as the builder resolved it. */
export type RenderSettings = Readonly<{
  renderScale: number;
  lodBudgetSplats: number;
  lodErrorPixels: number | null;
  sortDepth: number | null;
}>;

const PRESET_TITLES: Readonly<Record<QualityPreset, string>> = Object.freeze({
  [QualityPreset.highEnd]: 'Max',
  [QualityPreset.high]: 'High',
  [QualityPreset.balanced]: 'Balanced',
  [QualityPreset.performance]: 'Fast',
});

const BACKEND = Platform.select({ ios: 'Metal', android: 'Vulkan' }) ?? 'GPU';

/** Above this the frame rate reads as smooth, below the lower one as a problem. */
const SMOOTH_FPS = 50;
const ROUGH_FPS = 25;

/** Stats arrive twice a second, so this many samples is the last half minute. */
export const GRAPH_SAMPLES = 60;
const GRAPH_HEIGHT = 40;
/** The top of the graph: the display's refresh rate, which no sample can exceed. */
const GRAPH_MAX_FPS = 60;
const GRAPH_GUIDES: readonly number[] = Object.freeze([60, 30]);

const MILLIS_PER_SECOND = 1000;
const SEPARATOR = '  |  ';

function compact(value: number): string {
  if (value >= 1e6) return `${(value / 1e6).toFixed(1)}M`;
  if (value >= 1e3) return `${Math.round(value / 1e3)}k`;
  return `${value}`;
}

function millis(value: number | null): string {
  return value === null ? '-' : `${value.toFixed(1)} ms`;
}

function rate(value: number | null): string {
  return value === null ? '-' : value.toFixed(0);
}

function paceStyle(fps: number | null) {
  if (fps === null) return styles.paceIdle;
  if (fps >= SMOOTH_FPS) return styles.paceSmooth;
  return fps >= ROUGH_FPS ? styles.paceRough : styles.paceSlow;
}

function barHeight(fps: number | null): number {
  if (fps === null) return 0;
  return Math.max(1, Math.min(1, fps / GRAPH_MAX_FPS) * GRAPH_HEIGHT);
}

function Cell({ label, value }: { label: string; value: string }) {
  return (
    <View style={styles.cell}>
      <Text style={styles.cellLabel}>{label}</Text>
      <Text style={styles.cellValue}>{value}</Text>
    </View>
  );
}

function Graph({ samples }: { samples: readonly number[] }) {
  const recent = samples.slice(-GRAPH_SAMPLES);
  const slots: readonly (number | null)[] = [
    ...new Array<null>(GRAPH_SAMPLES - recent.length).fill(null),
    ...recent,
  ];
  return (
    <View style={styles.graph}>
      <View style={styles.plot}>
        {GRAPH_GUIDES.map(guide => (
          <View
            key={guide}
            style={[
              styles.guide,
              { top: GRAPH_HEIGHT * (1 - guide / GRAPH_MAX_FPS) },
            ]}
          />
        ))}
        <View style={styles.bars}>
          {slots.map((fps, index) => (
            <View
              // Slots are positions on the time axis, not samples: the index is the identity.
              key={index}
              style={[
                styles.bar,
                fps !== null && paceStyle(fps),
                { height: barHeight(fps) },
              ]}
            />
          ))}
        </View>
      </View>
      <View style={styles.scale}>
        {GRAPH_GUIDES.map(guide => (
          <Text
            key={guide}
            style={[
              styles.scaleText,
              { top: GRAPH_HEIGHT * (1 - guide / GRAPH_MAX_FPS) - 4 },
            ]}
          >
            {guide}
          </Text>
        ))}
      </View>
    </View>
  );
}

type PanelProps = Readonly<{
  stats: RenderStats | null;
  samples: readonly number[];
  settings: RenderSettings;
}>;

function Panel({ stats, samples, settings }: PanelProps) {
  const window = useWindowDimensions();
  // The engine skips frames while nothing moves, so no frame time is a still view, not 0 fps.
  const frameMillis =
    stats !== null && stats.frameMillis !== null && stats.frameMillis > 0
      ? stats.frameMillis
      : null;
  const fps = frameMillis === null ? null : MILLIS_PER_SECOND / frameMillis;
  const recent = samples.slice(-GRAPH_SAMPLES);
  const average =
    recent.length === 0
      ? null
      : recent.reduce((sum, sample) => sum + sample, 0) / recent.length;
  const low = recent.length === 0 ? null : Math.min(...recent);

  const pixels = PixelRatio.get() * settings.renderScale;
  const resolution = `${Math.round(window.width * pixels)} x ${Math.round(
    window.height * pixels,
  )}`;
  const caption = [
    `${resolution} px`,
    `LOD ${compact(settings.lodBudgetSplats)}${
      settings.lodErrorPixels === null ? '' : ` @ ${settings.lodErrorPixels} px`
    }`,
    settings.sortDepth === null ? null : `${settings.sortDepth}-bit sort`,
  ]
    .filter(part => part !== null)
    .join(SEPARATOR);

  return (
    <View style={styles.panel} pointerEvents="none">
      <View style={styles.header}>
        <Text style={styles.brand}>SPLATKIT</Text>
        <Text style={styles.backend}>React Native{SEPARATOR}{BACKEND}</Text>
      </View>
      <View style={styles.body}>
        <View style={styles.now}>
          <View style={styles.headline}>
            <Text style={styles.fps}>{fps === null ? 'Idle' : rate(fps)}</Text>
            {fps !== null && <Text style={styles.fpsUnit}>FPS</Text>}
          </View>
          <View style={styles.headline}>
            <View style={[styles.dot, paceStyle(fps)]} />
            <Text style={styles.frame}>
              {frameMillis === null ? 'no redraw' : millis(frameMillis)}
            </Text>
          </View>
        </View>
        <Graph samples={samples} />
      </View>
      <View style={styles.divider} />
      <View style={styles.cells}>
        <Cell label="AVG" value={rate(average)} />
        <Cell label="LOW" value={rate(low)} />
        <Cell label="GPU" value={millis(stats?.gpuMillis ?? null)} />
        <Cell label="SORT" value={millis(stats?.sortMillis ?? null)} />
        <Cell label="DRAWN" value={stats ? compact(stats.drawnSplats) : '-'} />
        <Cell label="SCENE" value={stats ? compact(stats.loadedSplats) : '-'} />
      </View>
      <Text style={styles.caption} numberOfLines={1}>
        {caption}
      </Text>
    </View>
  );
}

type Props = PanelProps &
  Readonly<{
    preset: QualityPreset;
    onPreset: (preset: QualityPreset) => void;
    /** Who made the world and under what licence; null draws nothing. */
    credit: string | null;
    top: number;
    bottom: number;
  }>;

function Hud({
  stats,
  samples,
  settings,
  preset,
  onPreset,
  credit,
  top,
  bottom,
}: Props) {
  return (
    <>
      <View style={[styles.panelSlot, { top: top + 8 }]} pointerEvents="none">
        <Panel stats={stats} samples={samples} settings={settings} />
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
      {credit !== null && (
        <View
          style={[styles.creditSlot, { bottom: bottom + 10 }]}
          pointerEvents="none"
        >
          <Text style={styles.credit} numberOfLines={1}>
            {credit}
          </Text>
        </View>
      )}
    </>
  );
}

const styles = StyleSheet.create({
  panelSlot: {
    position: 'absolute',
    left: 16,
    right: 16,
  },
  panel: {
    paddingHorizontal: 12,
    paddingVertical: 10,
    borderRadius: 14,
    backgroundColor: 'rgba(17, 17, 20, 0.66)',
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: 'rgba(255, 255, 255, 0.12)',
  },
  header: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 6,
  },
  brand: {
    color: 'white',
    fontSize: 10,
    fontWeight: '800',
    letterSpacing: 1.6,
  },
  backend: {
    color: 'rgba(255, 255, 255, 0.55)',
    fontSize: 9,
    fontWeight: '600',
  },
  body: {
    flexDirection: 'row',
    alignItems: 'flex-end',
    gap: 12,
  },
  now: {
    width: 70,
  },
  headline: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 5,
  },
  fps: {
    color: 'white',
    fontSize: 28,
    lineHeight: 32,
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
  paceIdle: {
    backgroundColor: '#9ca3af',
  },
  paceSmooth: {
    backgroundColor: '#4ade80',
  },
  paceRough: {
    backgroundColor: '#facc15',
  },
  paceSlow: {
    backgroundColor: '#f87171',
  },
  frame: {
    color: 'rgba(255, 255, 255, 0.55)',
    fontSize: 9,
    fontWeight: '500',
    fontVariant: ['tabular-nums'],
  },
  graph: {
    flex: 1,
    flexDirection: 'row',
    gap: 4,
  },
  plot: {
    flex: 1,
    height: GRAPH_HEIGHT,
  },
  guide: {
    position: 'absolute',
    left: 0,
    right: 0,
    height: StyleSheet.hairlineWidth,
    backgroundColor: 'rgba(255, 255, 255, 0.22)',
  },
  bars: {
    position: 'absolute',
    left: 0,
    right: 0,
    top: 0,
    bottom: 0,
    flexDirection: 'row',
    alignItems: 'flex-end',
    gap: 1,
  },
  bar: {
    flex: 1,
    borderTopLeftRadius: 1,
    borderTopRightRadius: 1,
  },
  scale: {
    width: 12,
    height: GRAPH_HEIGHT,
  },
  scaleText: {
    position: 'absolute',
    left: 0,
    color: 'rgba(255, 255, 255, 0.45)',
    fontSize: 7,
    lineHeight: 8,
    fontVariant: ['tabular-nums'],
  },
  divider: {
    height: StyleSheet.hairlineWidth,
    backgroundColor: 'rgba(255, 255, 255, 0.15)',
    marginVertical: 8,
  },
  cells: {
    flexDirection: 'row',
    justifyContent: 'space-between',
  },
  cell: {
    alignItems: 'flex-start',
  },
  cellLabel: {
    color: 'rgba(255, 255, 255, 0.5)',
    fontSize: 8,
    fontWeight: '700',
    letterSpacing: 0.6,
  },
  cellValue: {
    color: 'white',
    fontSize: 12,
    fontWeight: '600',
    fontVariant: ['tabular-nums'],
    marginTop: 1,
  },
  caption: {
    color: 'rgba(255, 255, 255, 0.5)',
    fontSize: 9,
    fontVariant: ['tabular-nums'],
    marginTop: 8,
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
  creditSlot: {
    position: 'absolute',
    left: 16,
    right: 16,
  },
  credit: {
    textAlign: 'center',
    color: 'rgba(255, 255, 255, 0.7)',
    fontSize: 10,
    fontWeight: '500',
    textShadowColor: 'rgba(0, 0, 0, 0.8)',
    textShadowRadius: 3,
  },
});

export default memo(Hud);
