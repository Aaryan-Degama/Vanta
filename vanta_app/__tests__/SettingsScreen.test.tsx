/**
 * Tests for the indexing-progress poller on the Settings screen.
 *
 * The native engine reports "failed" when a run stops on an error, and it only
 * sets "loading_models" once the service has started the run, so the previous
 * run's status is still visible for a moment after Start is pressed. The poller
 * must therefore ignore a stale terminal status at the start of a run, and
 * stop with an error alert once the current run really fails.
 */
import React from 'react';
import { Alert } from 'react-native';
import { render, fireEvent, act } from '@testing-library/react-native';
import SettingsScreen from '../screens/SettingsScreen';
import { ThemeProvider } from '../ThemeContext';
import * as bridge from '../modules/vanta-bridge';

// The idle button reads "Resume" when the mocked stats show partial progress.
const START_BUTTON = /^(Start Indexing|Resume)$/;

const progress = (status: string) =>
  JSON.stringify({ processed: 0, total: 0, status, currentFile: '' });

describe('SettingsScreen indexing poller', () => {
  afterEach(() => {
    jest.useRealTimers();
    jest.restoreAllMocks();
  });

  it('ignores a stale "failed" status, then alerts when the run fails', async () => {
    const alertSpy = jest.spyOn(Alert, 'alert').mockImplementation(() => {});
    const getIndexProgress = jest.spyOn(bridge, 'getIndexProgress');
    const tick = () => act(() => jest.advanceTimersByTimeAsync(1000));

    // Left over from a previous run.
    getIndexProgress.mockResolvedValue(progress('failed'));
    const result = await render(
      <ThemeProvider>
        <SettingsScreen />
      </ThemeProvider>
    );

    jest.useFakeTimers();
    await fireEvent.press(result.getByText(START_BUTTON));
    await tick();
    expect(alertSpy).not.toHaveBeenCalled();
    expect(result.getByText('Pause')).toBeTruthy();

    getIndexProgress.mockResolvedValue(progress('loading_models'));
    await tick();
    expect(alertSpy).not.toHaveBeenCalled();

    getIndexProgress.mockResolvedValue(progress('failed'));
    await tick();
    expect(alertSpy).toHaveBeenCalledTimes(1);
    expect(alertSpy).toHaveBeenCalledWith('Indexing failed', expect.any(String));
    expect(result.getByText(START_BUTTON)).toBeTruthy();

    // Polling has stopped: no further alerts.
    await tick();
    expect(alertSpy).toHaveBeenCalledTimes(1);
  });

  it('still reports a run that fails before it was ever seen active', async () => {
    const alertSpy = jest.spyOn(Alert, 'alert').mockImplementation(() => {});
    jest.spyOn(bridge, 'getIndexProgress').mockResolvedValue(progress('failed'));
    const result = await render(
      <ThemeProvider>
        <SettingsScreen />
      </ThemeProvider>
    );

    jest.useFakeTimers();
    await fireEvent.press(result.getByText(START_BUTTON));
    await act(() => jest.advanceTimersByTimeAsync(5000));
    expect(alertSpy).toHaveBeenCalledTimes(1);
    expect(result.getByText(START_BUTTON)).toBeTruthy();
  });
});
