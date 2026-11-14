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

  it('long-press selects results and shares exactly the selected ones', async () => {
    const result = await render(
      <ThemeProvider>
        <SearchScreen />
      </ThemeProvider>
    );
    const input = result.getByPlaceholderText('<Query>');
    await fireEvent.changeText(input, 'beach');
    await fireEvent(input, 'submitEditing');

    // Long-press enters selection; a tap then toggles instead of opening the viewer.
    await fireEvent(await result.findByTestId('result-1'), 'longPress');
    expect(result.getByText('1 selected')).toBeTruthy();
    await fireEvent.press(result.getByTestId('result-0'));
    await fireEvent.press(result.getByTestId('result-0'));
    expect(result.getByText('1 selected')).toBeTruthy();

    await fireEvent.press(result.getByLabelText('Share'));
    expect(bridge.shareFiles).toHaveBeenCalledWith(['/mock/photo2.jpg']);
    // A successful share leaves selection mode.
    expect(await result.findByText('2 Results')).toBeTruthy();
  });
});
