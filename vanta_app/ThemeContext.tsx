import React, { createContext, useState, useEffect, useContext } from 'react';
import { useColorScheme } from 'react-native';

interface ThemeContextType {
  isDarkMode: boolean;
  toggleDarkMode: () => void;
  colors: {
    background: string;
    text: string;
    surface: string;
    primary: string;
    border: string;
    chipBackground: string;
    chipText: string;
    chipActiveBackground: string;
    chipActiveText: string;
    searchBackground: string;
    iconBackground: string;
    iconColor: string;
  };
}

const lightColors = {
  background: '#f2f2f6',
  text: '#000000',
  surface: '#ffffff',
  primary: '#34c759', // Green for switch
  border: '#d1d1d6',
  chipBackground: '#e5e5ea',
  chipText: '#000000',
  chipActiveBackground: '#d1d1d6',
  chipActiveText: '#000000',
  searchBackground: '#e3e3e8',
  iconBackground: '#ffffff',
  iconColor: '#000000',
};

const darkColors = {
  background: '#000000',
  text: '#ffffff',
  surface: '#1c1c1e',
  primary: '#32d74b',
  border: '#38383a',
  chipBackground: '#2c2c2e',
  chipText: '#ffffff',
  chipActiveBackground: '#2c2c2e',
  chipActiveText: '#ffffff',
  searchBackground: '#1c1c1e',
  iconBackground: '#2c2c2e',
  iconColor: '#ffffff',
};

const ThemeContext = createContext<ThemeContextType>({
  isDarkMode: false,
  toggleDarkMode: () => {},
  colors: lightColors,
});

export const ThemeProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const systemColorScheme = useColorScheme();
  const [isDarkMode, setIsDarkMode] = useState(systemColorScheme === 'dark');

  const toggleDarkMode = () => {
    setIsDarkMode((prev) => !prev);
  };

  const colors = isDarkMode ? darkColors : lightColors;

  return (
    <ThemeContext.Provider value={{ isDarkMode, toggleDarkMode, colors }}>
      {children}
    </ThemeContext.Provider>
  );
};

export const useTheme = () => useContext(ThemeContext);
