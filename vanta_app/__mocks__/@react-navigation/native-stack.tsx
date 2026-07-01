/**
 * Mock for @react-navigation/native-stack used by Jest tests.
 *
 * The real navigator depends on native gesture handlers and screen containers.
 * This mock renders the screen components passed to `component` so tests can
 * assert on the full app tree in Node.
 */
import React from 'react';

/**
 * Mock the createNativeStackNavigator factory.
 *
 * `Navigator` renders its children. `Screen` renders the component passed in
 * the `component` prop, or `children` if no component is provided.
 */
export function createNativeStackNavigator() {
  return {
    Navigator: ({ children }: { children: React.ReactNode }) => <>{children}</>,
    Screen: ({
      component: Component,
      children,
    }: {
      component?: React.ComponentType<any>;
      children?: React.ReactNode;
    }) => {
      if (Component) {
        return <Component />;
      }
      return <>{children}</>;
    },
  };
}
