/**
 * A thumb stick. Reports a direction in [-1, 1] on each axis, y positive upwards, while
 * held, and (0, 0) once on release.
 *
 * PanResponder, not React state, drives the value: the knob is animated and the direction
 * goes out on every touch move, so nothing re-renders while walking.
 *
 * @format
 */

import { useMemo, useRef } from 'react';
import { Animated, PanResponder, StyleSheet, View } from 'react-native';

const RADIUS = 62;
const KNOB = 52;

type Props = Readonly<{
  onChange: (forward: number, right: number) => void;
}>;

function Joystick({ onChange }: Props) {
  const knob = useRef(new Animated.ValueXY({ x: 0, y: 0 })).current;
  const change = useRef(onChange);
  change.current = onChange;

  const responder = useMemo(
    () =>
      PanResponder.create({
        onStartShouldSetPanResponder: () => true,
        onMoveShouldSetPanResponder: () => true,
        onPanResponderMove: (_event, gesture) => {
          let { dx, dy } = gesture;
          const length = Math.hypot(dx, dy);
          if (length > RADIUS) {
            dx = (dx / length) * RADIUS;
            dy = (dy / length) * RADIUS;
          }
          knob.setValue({ x: dx, y: dy });
          change.current(-dy / RADIUS, dx / RADIUS);
        },
        onPanResponderRelease: () => {
          Animated.spring(knob, {
            toValue: { x: 0, y: 0 },
            useNativeDriver: true,
            speed: 24,
            bounciness: 6,
          }).start();
          change.current(0, 0);
        },
        onPanResponderTerminate: () => {
          knob.setValue({ x: 0, y: 0 });
          change.current(0, 0);
        },
      }),
    [knob],
  );

  return (
    <View style={styles.base} {...responder.panHandlers}>
      <Animated.View
        style={[styles.knob, { transform: knob.getTranslateTransform() }]}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  base: {
    width: RADIUS * 2,
    height: RADIUS * 2,
    borderRadius: RADIUS,
    backgroundColor: 'rgba(255, 255, 255, 0.12)',
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: 'rgba(255, 255, 255, 0.25)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  knob: {
    width: KNOB,
    height: KNOB,
    borderRadius: KNOB / 2,
    backgroundColor: 'rgba(255, 255, 255, 0.55)',
  },
});

export default Joystick;
