/**
 * Tests for the face re-scan flow on the People screen.
 *
 * Native resolves resetFaceData with false when it refuses the re-scan (an
 * indexing run is in progress, or the face model failed to load) or when the
 * re-scan fails part-way. The screen must tell the user instead of silently
 * returning to the grid.
 */
import React from 'react';
import { Alert } from 'react-native';
import { render, fireEvent, act } from '@testing-library/react-native';
import { PeopleScreen } from '../screens/PeopleScreen';
import { ThemeProvider } from '../ThemeContext';
import VantaEngine from '../modules/vanta-bridge/src/VantaEngineModule';

jest.mock('../modules/vanta-bridge/src/VantaEngineModule', () => ({
  __esModule: true,
  default: {
    getTopEntities: jest.fn(async () => '[]'),
    resetFaceData: jest.fn(),
  },
}));

// Presses Re-scan, confirms the destructive alert, and waits for the reset.
async function rescan(resetResult: boolean) {
  (VantaEngine.resetFaceData as jest.Mock).mockResolvedValue(resetResult);
  const alertSpy = jest.spyOn(Alert, 'alert').mockImplementation(() => {});
  const result = await render(
    <ThemeProvider>
      <PeopleScreen />
    </ThemeProvider>
  );
  await fireEvent.press(await result.findByText('Re-scan'));
  const buttons = alertSpy.mock.calls[0][2]!;
  await act(async () => {
    await buttons.find((b) => b.text === 'Re-scan')!.onPress!();
  });
  return alertSpy;
}

describe('PeopleScreen re-scan', () => {
  afterEach(() => jest.restoreAllMocks());

  it('alerts when native reports the re-scan did not complete', async () => {
    const alertSpy = await rescan(false);
    expect(alertSpy).toHaveBeenCalledTimes(2);
    expect(alertSpy).toHaveBeenLastCalledWith('Re-scan did not complete', expect.any(String));
  });

  it('shows no extra alert when the re-scan completes', async () => {
    const alertSpy = await rescan(true);
    expect(alertSpy).toHaveBeenCalledTimes(1);
  });
});
