/**
 * Jest setup file.
 *
 * Runs once before the test suite. It suppresses console noise in tests and
 * stubs Expo modules that are not available in the Node test environment.
 */

// Silence non-error console output during tests to keep the test report clean.
global.console = {
  ...console,
  log: jest.fn(),
  warn: jest.fn(),
  info: jest.fn(),
  debug: jest.fn(),
};

// Some Expo modules inspect process.env.EXPO_OS in tests.
process.env.EXPO_OS = 'ios';
