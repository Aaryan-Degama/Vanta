import React, { useEffect } from 'react';
import {
  NavigationContainer,
  DefaultTheme,
  DarkTheme as NavigationDarkTheme,
} from '@react-navigation/native';
import { createNativeStackNavigator } from '@react-navigation/native-stack';
import AsyncStorage from '@react-native-async-storage/async-storage';
import { ThemeProvider, useTheme } from './ThemeContext';
import { setSearchOptions } from './modules/vanta-bridge';

import SearchScreen from './screens/SearchScreen';
import SettingsScreen from './screens/SettingsScreen';
import DebugScreen from './screens/DebugScreen';
import { PeopleScreen } from './screens/PeopleScreen';
import { EntityDetailScreen } from './screens/EntityDetailScreen';
import AdvancedOptionScreen from './screens/AdvancedOptionScreen';
import { ErrorBoundary } from './ErrorBoundary';

// Type-safe route parameter list for the root navigator.
export type RootStackParamList = {
  Search: undefined;
  Settings: undefined;
  AdvancedOption: undefined;
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
          name="AdvancedOption"
          component={AdvancedOptionScreen}
          options={{ headerShown: true, title: 'Advanced Options' }}
        />
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
  // The native engine starts with every search option on. Push the toggles
  // saved by AdvancedOptionScreen (same keys and JSON encoding, default true)
  // once at startup so they take effect without revisiting that screen.
  useEffect(() => {
    AsyncStorage.multiGet(['use_graph', 'use_spellcheck', 'use_intent'])
      .then((pairs) => {
        const [useGraph, useSpellCheck, useIntent] = pairs.map(([, value]): boolean =>
          value !== null ? JSON.parse(value) : true
        );
        return setSearchOptions(useGraph, useSpellCheck, useIntent);
      })
      .catch((e) => console.error('Failed to apply saved search options', e));
  }, []);

  return (
    <ErrorBoundary>
      <ThemeProvider>
        <RootNavigator />
      </ThemeProvider>
    </ErrorBoundary>
  );
}
