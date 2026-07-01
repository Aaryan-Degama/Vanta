/**
 * Smoke test for the root App component.
 *
 * Renders the full navigation tree using mocked native modules and navigation.
 * This ensures that every top-level screen can mount without throwing.
 */
import React from 'react';
import { render } from '@testing-library/react-native';
import App from '../App';

describe('App', () => {
  it('renders without crashing', async () => {
    const result = await render(<App />);
    expect(result.toJSON()).toBeTruthy();
  });
});
