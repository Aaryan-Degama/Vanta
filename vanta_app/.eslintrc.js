/**
 * ESLint configuration for the Vanta React Native app.
 *
 * ESLint is a static analyzer that catches bugs, unused variables, and style
 * issues before runtime. This config extends recommended rules from TypeScript,
 * React, React Hooks, React Native, and Prettier.
 */
module.exports = {
  // Use the TypeScript parser so ESLint understands TS/TSX syntax.
  parser: '@typescript-eslint/parser',

  parserOptions: {
    // Use a dedicated TypeScript project that includes config files (e.g.
    // .eslintrc.js) so typescript-eslint can run type-aware rules on them.
    project: './tsconfig.eslint.json',
    // Support JSX (required for React Native .tsx files).
    ecmaFeatures: {
      jsx: true,
    },
    // Target modern ECMAScript.
    ecmaVersion: 'latest',
    sourceType: 'module',
  },

  // Plugins add custom rule sets.
  plugins: [
    // TypeScript-specific lint rules (e.g., no-explicit-any, consistent-type-imports).
    '@typescript-eslint',
    // React-specific rules (e.g., no-deprecated, jsx-key).
    'react',
    // Rules for React Hooks (e.g., rules-of-hooks, exhaustive-deps).
    'react-hooks',
    // Rules tailored to React Native (e.g., no-unused-styles, no-color-literals).
    'react-native',
  ],

  // Extends recommended rule sets.
  extends: [
    // ESLint's built-in recommended rules.
    'eslint:recommended',
    // Recommended TypeScript rules.
    'plugin:@typescript-eslint/recommended',
    // React recommended rules.
    'plugin:react/recommended',
    // React Hooks recommended rules.
    'plugin:react-hooks/recommended',
    // React Native recommended rules.
    'plugin:react-native/all',
    // Disables rules that conflict with Prettier formatting.
    'prettier',
  ],

  // Environment settings tell ESLint which globals are available.
  env: {
    // Browser globals (used by React Native's web-compatible APIs).
    browser: true,
    // ES2024 globals.
    es2024: true,
    // Jest globals for test files.
    jest: true,
    // Node globals (used by config files and Metro).
    node: true,
  },

  settings: {
    react: {
      // Tell the React plugin which version of React we are using.
      version: 'detect',
    },
  },

  // Custom rule overrides.
  rules: {
    // Allow `any` during the initial quality pass; tighten this later.
    '@typescript-eslint/no-explicit-any': 'warn',
    // Warn on unused variables instead of erroring, to avoid breaking the build
    // while we refactor prototype code.
    '@typescript-eslint/no-unused-vars': ['warn', { argsIgnorePattern: '^_' }],
    // React 19 does not require React to be in scope.
    'react/react-in-jsx-scope': 'off',
    // Prop-types are not used in TypeScript code.
    'react/prop-types': 'off',
    // Inline styles are common in rapid React Native prototyping; warn only.
    'react-native/no-inline-styles': 'warn',
    // Color literals are acceptable for theme prototyping; warn only.
    'react-native/no-color-literals': 'warn',
    // Sorting style properties alphabetically is pedantic and not worth the churn.
    'react-native/split-platform-components': 'off',
    // PermissionsAndroid is intentionally used in cross-platform TSX files.
    'react-native/sort-styles': 'off',
    // The existing Animated.Value refs pattern is valid React Native code.
    'react-hooks/refs': 'off',
    // setState in useEffect is used for one-shot data fetches; disable for now.
    'react-hooks/set-state-in-effect': 'off',
    // Allow console logs in the prototype but warn so they are reviewed.
    'no-console': 'warn',
  },

  // Files and directories to ignore.
  ignorePatterns: [
    // Node dependencies.
    'node_modules/',
    // Android build output.
    'android/',
    // iOS build output.
    'ios/',
    // Expo generated files.
    '.expo/',
    // Metro bundler cache.
    '.metro/',
    // Third-party native module code.
    'modules/vanta-bridge/android/',
    'modules/vanta-bridge/cpp/thirdparty/',
  ],
};
