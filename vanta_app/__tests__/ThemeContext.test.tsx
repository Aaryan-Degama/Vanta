/**
 * Unit tests for the theme context.
 *
 * Verifies that the mocked ThemeProvider renders children and that useTheme
 * returns the expected default values. This gives us a lightweight smoke test
 * for the Jest/Expo test harness itself.
 */
import React from 'react';
import { render } from '@testing-library/react-native';
import { Text } from 'react-native';
import { ThemeProvider, useTheme } from '../ThemeContext';

/**
 * Test component that consumes the theme context and renders the current
 * background color so we can assert on it.
 */
function ThemeConsumer() {
  const { colors } = useTheme();
  return <Text testID="bg-color">{colors.background}</Text>;
}

describe('ThemeContext', () => {
  it('renders children with a light-mode background color', async () => {
    const result = await render(
      <ThemeProvider>
        <ThemeConsumer />
      </ThemeProvider>
    );

    expect(result.getByTestId('bg-color').children[0]).toBe('#ffffff');
  });
});
