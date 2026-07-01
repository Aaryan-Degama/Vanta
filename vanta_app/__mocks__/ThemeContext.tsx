/**
 * Mock theme context for Jest tests.
 *
 * Provides deterministic light-mode colors so snapshots and assertions are
 * stable across test runs regardless of the host OS color scheme.
 */
import React, { createContext, useContext, ReactNode } from 'react';

// Default light-mode color palette used in tests.
const mockColors = {
  background: '#ffffff',
  text: '#000000',
  surface: '#f2f2f7',
  primary: '#007aff',
  chipBackground: '#e5e5ea',
  chipText: '#1c1c1e',
  border: '#c7c7cc',
  placeholder: '#8e8e93',
};

// Theme context shape exposed to consumers.
interface ThemeContextType {
  isDarkMode: boolean;
  toggleDarkMode: () => void;
  colors: typeof mockColors;
}

// Create the context with a sensible default value.
const ThemeContext = createContext<ThemeContextType>({
  isDarkMode: false,
  toggleDarkMode: () => {
    // No-op in tests; tests can spy on this if needed.
  },
  colors: mockColors,
});

/**
 * Hook that returns the mocked theme context.
 */
export function useTheme() {
  return useContext(ThemeContext);
}

/**
 * Provider that wraps components under test with the mocked theme.
 */
export function ThemeProvider({ children }: { children: ReactNode }) {
  return (
    <ThemeContext.Provider
      value={{
        isDarkMode: false,
        toggleDarkMode: () => {},
        colors: mockColors,
      }}
    >
      {children}
    </ThemeContext.Provider>
  );
}
