/**
 * Smoke test for the root App component.
 *
 * Renders the full navigation tree using mocked native modules and navigation.
 * This ensures that every top-level screen can mount without throwing.
 */
import React from 'react';
import { render, waitFor } from '@testing-library/react-native';
import AsyncStorage from '@react-native-async-storage/async-storage';
import App from '../App';
import { setSearchOptions } from '../modules/vanta-bridge';

describe('App', () => {
  it('renders without crashing', async () => {
    const result = await render(<App />);
    expect(result.toJSON()).toBeTruthy();
  });

  it('applies saved search options to the native engine at startup', async () => {
    // Only spell check was turned off; unset keys default to true.
    await AsyncStorage.setItem('use_spellcheck', JSON.stringify(false));
    (setSearchOptions as jest.Mock).mockClear();

    await render(<App />);

    await waitFor(() => expect(setSearchOptions).toHaveBeenCalledWith(true, false, true));
    expect(setSearchOptions).toHaveBeenCalledTimes(1);
  });
});
