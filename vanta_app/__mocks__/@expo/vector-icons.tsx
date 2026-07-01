/**
 * Mock for @expo/vector-icons used by Jest tests.
 *
 * The real vector icon font loader depends on native font assets. This mock
 * renders a simple Text element with the icon name so tests do not need to
 * load fonts.
 */
import React from 'react';
import { Text } from 'react-native';

/**
 * Mock Ionicons component.
 *
 * Renders the icon name as text so tests can assert on icon presence without
 * requiring the native font.
 */
export function Ionicons({ name, size, color }: { name: string; size?: number; color?: string }) {
  return <Text style={{ fontSize: size, color }}>{name}</Text>;
}
