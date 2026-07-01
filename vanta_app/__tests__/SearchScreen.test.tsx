/**
 * Smoke test for the Search screen.
 *
 * Verifies that SearchScreen renders with the mocked native module and theme
 * provider, and that the search input is present.
 */
import React from 'react';
import { render } from '@testing-library/react-native';
import SearchScreen from '../screens/SearchScreen';
import { ThemeProvider } from '../ThemeContext';

describe('SearchScreen', () => {
  it('renders the search input', async () => {
    const result = await render(
      <ThemeProvider>
        <SearchScreen />
      </ThemeProvider>
    );

    expect(result.getByPlaceholderText('<Query>')).toBeTruthy();
  });
});
