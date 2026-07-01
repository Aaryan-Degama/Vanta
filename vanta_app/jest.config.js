/**
 * Jest configuration for the Vanta React Native app.
 *
 * Jest is a JavaScript test runner. `jest-expo` extends Jest with presets for
 * React Native and Expo, including transformer rules for TypeScript and asset
 * imports. This config runs all `*.test.ts` and `*.test.tsx` files while
 * mocking native modules that cannot run in Node.
 */
module.exports = {
  // Use Expo's Jest preset; it handles Metro, TypeScript, and asset imports.
  preset: 'jest-expo',

  // Test file patterns. We only run unit/integration tests, not E2E tests.
  testMatch: ['**/__tests__/**/*.test.ts', '**/__tests__/**/*.test.tsx'],

  // Modules that cannot run in Node are replaced with manual mocks.
  moduleNameMapper: {
    // Mock the vanta-bridge native module so tests never call JNI/C++ code.
    '^../modules/vanta-bridge$': '<rootDir>/__mocks__/vanta-bridge.ts',
    '^../../modules/vanta-bridge$': '<rootDir>/__mocks__/vanta-bridge.ts',
    '^../ThemeContext$': '<rootDir>/__mocks__/ThemeContext.tsx',
    // Mock React Navigation so the navigator tree can render in Node.
    '^@react-navigation/native$': '<rootDir>/__mocks__/@react-navigation/native.tsx',
    '^@react-navigation/native-stack$': '<rootDir>/__mocks__/@react-navigation/native-stack.tsx',
    // Mock vector icons to avoid loading native fonts in Node.
    '^@expo/vector-icons$': '<rootDir>/__mocks__/@expo/vector-icons.tsx',
    // Mock the Expo runtime so native module declarations load in Node.
    '^expo$': '<rootDir>/__mocks__/expo.ts',
  },

  // Files to run after Jest is initialized but before tests execute.
  setupFilesAfterEnv: ['<rootDir>/jest.setup.js'],

  // Collect coverage from source files (not from tests or mocks).
  collectCoverageFrom: [
    'App.tsx',
    'ThemeContext.tsx',
    'screens/**/*.{ts,tsx}',
    'modules/vanta-bridge/src/**/*.{ts,tsx}',
    '!**/*.d.ts',
  ],

  // React Native dependencies are precompiled and should not be transformed.
  // Everything else inside node_modules is transformed so ESM-only packages
  // like expo-modules-core work in the Node test environment.
  transformIgnorePatterns: [
    'node_modules/(?!(react-native|@react-native|@react-navigation|@expo|expo|expo-modules-core|expo-font|@expo/vector-icons|@unimodules|react-native-cloneable|react-native-safe-area-context|react-native-screens|@testing-library)/)',
  ],

  // Environment globals available in tests.
  testEnvironment: 'node',
};
