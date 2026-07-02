import React, { useState, useEffect } from 'react';
import { View, Text, StyleSheet, Switch } from 'react-native';
import { useTheme } from '../ThemeContext';
import AsyncStorage from '@react-native-async-storage/async-storage';
import { setSearchOptions } from '../modules/vanta-bridge';

/**
 * AdvancedOptionScreen
 * Provides users with granular toggles for the underlying search engine features
 * such as Graph Search (entity relationships), Spell Check, and Intent Extraction.
 * These settings are persisted locally via AsyncStorage and passed to the C++ engine.
 */
export default function AdvancedOptionScreen() {
  const { colors } = useTheme();

  const [useGraph, setUseGraph] = useState(true);
  const [useSpellCheck, setUseSpellCheck] = useState(true);
  const [useIntent, setUseIntent] = useState(true);

  useEffect(() => {
    const loadOptions = async () => {
      try {
        const ug = await AsyncStorage.getItem('use_graph');
        const us = await AsyncStorage.getItem('use_spellcheck');
        const ui = await AsyncStorage.getItem('use_intent');
        
        const graphVal = ug !== null ? JSON.parse(ug) : true;
        const spellVal = us !== null ? JSON.parse(us) : true;
        const intentVal = ui !== null ? JSON.parse(ui) : true;
        
        setUseGraph(graphVal);
        setUseSpellCheck(spellVal);
        setUseIntent(intentVal);
      } catch(e) {
        console.error(e);
      }
    };
    loadOptions();
  }, []);

  const updateOption = async (key: string, value: boolean) => {
    try {
      await AsyncStorage.setItem(key, JSON.stringify(value));
      
      let finalGraph = useGraph;
      let finalSpell = useSpellCheck;
      let finalIntent = useIntent;
      
      if (key === 'use_graph') { setUseGraph(value); finalGraph = value; }
      if (key === 'use_spellcheck') { setUseSpellCheck(value); finalSpell = value; }
      if (key === 'use_intent') { setUseIntent(value); finalIntent = value; }
      
      await setSearchOptions(finalGraph, finalSpell, finalIntent);
    } catch (e) {
      console.error(e);
    }
  };

  return (
    <View style={[styles.container, { backgroundColor: colors.background }]}>
      <View style={[styles.settingRow, { paddingVertical: 10 }]}>
        <View style={{ flex: 1, paddingRight: 10 }}>
          <Text style={[styles.settingText, { color: colors.text }]}>Graph Search</Text>
          <Text style={{ color: colors.text, opacity: 0.5, fontSize: 12, marginTop: 2 }}>Extract people and validate face relationships.</Text>
        </View>
        <Switch
          value={useGraph}
          onValueChange={(val) => updateOption('use_graph', val)}
          trackColor={{ false: '#767577', true: '#34c759' }}
          thumbColor={'#ffffff'}
        />
      </View>
      
      <View style={[styles.settingRow, { paddingVertical: 10 }]}>
        <View style={{ flex: 1, paddingRight: 10 }}>
          <Text style={[styles.settingText, { color: colors.text }]}>Spell Check</Text>
          <Text style={{ color: colors.text, opacity: 0.5, fontSize: 12, marginTop: 2 }}>Automatically correct typos in search queries.</Text>
        </View>
        <Switch
          value={useSpellCheck}
          onValueChange={(val) => updateOption('use_spellcheck', val)}
          trackColor={{ false: '#767577', true: '#34c759' }}
          thumbColor={'#ffffff'}
        />
      </View>

      <View style={[styles.settingRow, { paddingVertical: 10 }]}>
        <View style={{ flex: 1, paddingRight: 10 }}>
          <Text style={[styles.settingText, { color: colors.text }]}>Intent Extraction</Text>
          <Text style={{ color: colors.text, opacity: 0.5, fontSize: 12, marginTop: 2 }}>Re-format sentences into visual descriptions for CLIP.</Text>
        </View>
        <Switch
          value={useIntent}
          onValueChange={(val) => updateOption('use_intent', val)}
          trackColor={{ false: '#767577', true: '#34c759' }}
          thumbColor={'#ffffff'}
        />
      </View>
      
      <View style={{ marginTop: 'auto', marginBottom: 40 }}>
        <Text style={{ color: colors.text, opacity: 0.6, fontSize: 14, textAlign: 'center', lineHeight: 20 }}>
          Toggling the switch can lock or unlock a feature that may or may not help in searching. Find what fits you well.
        </Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    paddingTop: 20,
    paddingHorizontal: 20,
  },
  settingRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingVertical: 15,
  },
  settingText: {
    fontSize: 16,
    fontWeight: '400',
  },
});
