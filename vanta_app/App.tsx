import React from 'react';
import {
  NavigationContainer,
  DefaultTheme,
  DarkTheme as NavigationDarkTheme,
} from '@react-navigation/native';
import { createNativeStackNavigator } from '@react-navigation/native-stack';
import { ThemeProvider, useTheme } from './ThemeContext';

import SearchScreen from './screens/SearchScreen';
import SettingsScreen from './screens/SettingsScreen';
import DebugScreen from './screens/DebugScreen';
import { PeopleScreen } from './screens/PeopleScreen';
import { EntityDetailScreen } from './screens/EntityDetailScreen';
import { ErrorBoundary } from './ErrorBoundary';

// Type-safe route parameter list for the root navigator.
export type RootStackParamList = {
  Search: undefined;
  Settings: undefined;
  Debug: undefined;
  People: undefined;
  EntityDetail: {
    entity: { entity_id: number; display_name: string; sample_count: number; confidence: number };
  };
};

const Stack = createNativeStackNavigator<RootStackParamList>();

function RootNavigator() {
  const { colors, isDarkMode } = useTheme();

  return (
    <NavigationContainer theme={isDarkMode ? NavigationDarkTheme : DefaultTheme}>
      <Stack.Navigator
        initialRouteName="Search"
        screenOptions={{
          headerShown: false,
          headerStyle: { backgroundColor: colors.background },
          headerTintColor: colors.text,
        }}
      >
        <Stack.Screen name="Search" component={SearchScreen} />
        <Stack.Screen name="Settings" component={SettingsScreen} />
        <Stack.Screen
          name="Debug"
          component={DebugScreen}
          options={{ headerShown: true, title: 'Developer' }}
        />
        <Stack.Screen
          name="People"
          component={PeopleScreen}
          options={{ headerShown: true, title: 'People' }}
        />
        <Stack.Screen
          name="EntityDetail"
          component={EntityDetailScreen}
          options={{ headerShown: true, title: 'Details' }}
        />
      </Stack.Navigator>
    </NavigationContainer>
  );
}

export default function App() {
  return (
    <ErrorBoundary>
      <ThemeProvider>
        <RootNavigator />
      </ThemeProvider>
    </ErrorBoundary>
  );
}
