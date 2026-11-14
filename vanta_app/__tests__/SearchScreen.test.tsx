/**
 * Smoke test for the Search screen.
 *
 * Verifies that SearchScreen renders with the mocked native module and theme
 * provider, and that the search input is present.
 */
import React from 'react';
import { render, fireEvent } from '@testing-library/react-native';
import SearchScreen from '../screens/SearchScreen';
import { ThemeProvider } from '../ThemeContext';
import * as bridge from '../modules/vanta-bridge';

describe('SearchScreen', () => {
  it('renders the search input', async () => {
    const result = await render(
      <ThemeProvider>
        <SearchScreen />
      </ThemeProvider>
    );

    expect(result.getByPlaceholderText('<Query>')).toBeTruthy();
  });

  it('shows a "No results" state when a search returns nothing', async () => {
    jest.spyOn(bridge, 'searchImages').mockResolvedValueOnce('[]');
    const result = await render(
      <ThemeProvider>
        <SearchScreen />
      </ThemeProvider>
    );
    const input = result.getByPlaceholderText('<Query>');

    // Idle view before any search.
    expect(result.getByText('Search your photos')).toBeTruthy();

    await fireEvent.changeText(input, 'unicorn');
    await fireEvent(input, 'submitEditing');
    expect(await result.findByText('No results')).toBeTruthy();

    // Clearing the query returns to the idle view.
    await fireEvent.changeText(input, '');
    expect(result.getByText('Search your photos')).toBeTruthy();
  });
});
