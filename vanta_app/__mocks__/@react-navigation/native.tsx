/**
 * Mock for @react-navigation/native used by Jest tests.
 *
 * Provides stub implementations of the navigation context and hooks so that
 * screens can render without the full native navigator.
 */
import React from 'react';

// Dummy navigation object returned by useNavigation in tests.
const mockNavigation = {
  navigate: () => {},
  goBack: () => {},
  setOptions: () => {},
};

/**
 * Hook that returns a stub navigation object.
 */
export function useNavigation() {
  return mockNavigation;
}

/**
 * Hook that returns an empty route object.
 */
export function useRoute() {
  return { params: {} };
}

/**
 * Stub NavigationContainer that simply renders its children.
 */
export function NavigationContainer({ children }: { children: React.ReactNode }) {
  return <>{children}</>;
}
