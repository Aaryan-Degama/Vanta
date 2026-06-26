import React, { useState, useRef } from 'react';
import { View, 
  Text, 
  StyleSheet, 
  TextInput, 
  TouchableOpacity, 
  Switch, 
  Platform, 
  ScrollView, 
  Image, 
  ActivityIndicator, 
  Animated } from 'react-native';
import { useTheme } from '../ThemeContext';
import { useNavigation } from '@react-navigation/native';
import { Ionicons } from '@expo/vector-icons';
import { searchImages } from '../modules/vanta-bridge';

export default function SearchScreen() {
  const { colors } = useTheme();
  const navigation = useNavigation<any>();
  const [searchQuery, setSearchQuery] = useState('');
  const [isExclusive, setIsExclusive] = useState(true);
  const [results, setResults] = useState<any[]>([]);
  const [isSearching, setIsSearching] = useState(false);

  const scrollY = useRef(new Animated.Value(0)).current;
  const headerHeight = 70; // Distance to scroll up before header fully reveals
  const clampedScrollY = Animated.diffClamp(scrollY, 0, headerHeight);
  const translateY = clampedScrollY.interpolate({
    inputRange: [0, headerHeight],
    outputRange: [0, -headerHeight],
  });



  // Example state for filters
  const [filters, setFilters] = useState([
    { id: 'video', label: 'Video', active: false },
    { id: 'audio', label: 'audio', active: false },
    { id: 'images', label: 'Images', active: true },
    { id: 'documents', label: 'Documents', active: false },
  ]);

  const toggleFilter = (id: string) => {
    setFilters(filters.map(f => f.id === id ? { ...f, active: !f.active } : f));
  };

  const handleSearch = async () => {
    if (!searchQuery.trim()) return;
    setIsSearching(true);
    try {
      const jsonRes = await searchImages(searchQuery);
      console.log('RAW SEARCH RESULT:', jsonRes);
      setResults(JSON.parse(jsonRes));
    } catch (e) {
      console.error('SEARCH ERROR:', e);
    } finally {
      setIsSearching(false);
    }
  };

  const formatBytes = (bytes: number) => {
    if (!bytes || bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const formatDate = (unix: number) => {
    if (!unix) return '';
    const d = new Date(unix * 1000);
    return d.toLocaleDateString('en-GB', { day: 'numeric', month: 'short', year: 'numeric' }) + ' ' +
      d.toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit', hour12: true });
  };

  return (
    <View
      style={[styles.container, { backgroundColor: colors.background }]}
    >
      <Animated.View style={[styles.header, { position: 'absolute', top: 50, left: 20, right: 20, zIndex: 10, transform: [{ translateY }] }]}>
        {results.length > 0 || isSearching ? (
          <TouchableOpacity style={[styles.iconButton, { backgroundColor: colors.iconBackground }]} onPress={() => {
            setSearchQuery('');
            setResults([]);
          }}>
            <Ionicons name="chevron-back" size={24} color={colors.iconColor} />
          </TouchableOpacity>
        ) : (
          <View style={{ width: 40, height: 40 }} />
        )}
        <TouchableOpacity style={[styles.iconButton, { backgroundColor: colors.iconBackground }]} onPress={() => navigation.navigate('Settings')}>
          <Ionicons name="settings-sharp" size={20} color={colors.iconColor} />
        </TouchableOpacity>
      </Animated.View>

      <Animated.ScrollView
        contentContainerStyle={{ flexGrow: 1, paddingTop: 105, paddingBottom: 20, paddingHorizontal: 20 }}
        keyboardShouldPersistTaps="handled"
        showsVerticalScrollIndicator={false}
        onScroll={Animated.event(
          [{ nativeEvent: { contentOffset: { y: scrollY } } }],
          { useNativeDriver: true }
        )}
        scrollEventThrottle={16}
      >
        <Text style={[styles.title, { color: colors.text }]}>Search</Text>

        {/* Search Results List */}
        <View style={{ flex: 1, marginTop: 10 }}>
          {isSearching ? (
            <View style={{ flex: 1, justifyContent: 'center', alignItems: 'center' }}>
              <ActivityIndicator size="large" color={colors.primary} />
            </View>
          ) : (
            results.map((item, index) => (
              <View key={index} style={styles.resultItem}>
                <View style={styles.resultImageContainer}>
                  {/* Fallback to icon if path is empty, otherwise show image */}
                  {item.abs_path ? (
                    <Image source={{ uri: 'file://' + item.abs_path }} style={styles.resultImage} />
                  ) : (
                    <View style={[styles.resultImagePlaceholder, { backgroundColor: colors.chipBackground }]}>
                      <Ionicons name="image-outline" size={24} color={colors.text} />
                    </View>
                  )}
                </View>
                <View style={styles.resultDetails}>
                  <Text style={[styles.resultTitle, { color: colors.text }]} numberOfLines={1}>{item.display_name || (item.abs_path ? item.abs_path.split('/').pop() : 'Unknown File')}</Text>

                  <View style={styles.resultFooter}>
                    <Text style={styles.resultDate}>{formatDate(item.mtime_unix)}</Text>
                    <Text style={[styles.resultSize, { color: colors.text }]}>{formatBytes(item.size_bytes)}</Text>
                  </View>
                </View>
              </View>
            ))
          )}
        </View>
      </Animated.ScrollView>

      {/* Filters Section */}
      <View style={[styles.bottomSection, { paddingHorizontal: 20, paddingBottom: Platform.OS === 'ios' ? 30 : 20 }]}>
        {/* Row 1 */}
        <View style={styles.filterRow}>
          {filters.slice(0, 2).map((filter) => (
            <TouchableOpacity
              key={filter.id}
              style={[
                styles.chip,
                { backgroundColor: filter.active ? colors.chipActiveBackground : colors.chipBackground }
              ]}
              onPress={() => toggleFilter(filter.id)}
            >
              {filter.active ? (
                <Ionicons name="checkmark" size={14} color={colors.chipActiveText} style={styles.chipIcon} />
              ) : null}
              <Text style={[
                styles.chipText,
                { color: filter.active ? colors.chipActiveText : colors.chipText }
              ]}>
                {filter.label}
              </Text>
              {!filter.active ? (
                <Ionicons name="close" size={14} color={colors.chipText} style={styles.chipIconRight} />
              ) : null}
            </TouchableOpacity>
          ))}
        </View>

        {/* Row 2 */}
        <View style={[styles.filterRow, { justifyContent: 'space-between' }]}>
          <View style={styles.filterRow}>
            {filters.slice(2, 4).map((filter) => (
              <TouchableOpacity
                key={filter.id}
                style={[
                  styles.chip,
                  { backgroundColor: filter.active ? colors.chipActiveBackground : colors.chipBackground }
                ]}
                onPress={() => toggleFilter(filter.id)}
              >
                {filter.active ? (
                  <Ionicons name="checkmark" size={14} color={colors.chipActiveText} style={styles.chipIcon} />
                ) : null}
                <Text style={[
                  styles.chipText,
                  { color: filter.active ? colors.chipActiveText : colors.chipText }
                ]}>
                  {filter.label}
                </Text>
                {!filter.active ? (
                  <Ionicons name="close" size={14} color={colors.chipText} style={styles.chipIconRight} />
                ) : null}
              </TouchableOpacity>
            ))}
          </View>
          <Switch
            value={isExclusive}
            onValueChange={setIsExclusive}
            trackColor={{ false: '#767577', true: colors.primary }}
            thumbColor={'#ffffff'}
            style={styles.switch}
          />
        </View>

        {/* Search Bar */}
        <View style={[styles.searchContainer, { backgroundColor: colors.searchBackground }]}>
          <Ionicons name="search" size={20} color="#8e8e93" style={styles.searchIcon} />
          <TextInput
            style={[styles.searchInput, { color: colors.text }]}
            placeholder="<Query>"
            placeholderTextColor="#8e8e93"
            value={searchQuery}
            onChangeText={setSearchQuery}
            onSubmitEditing={handleSearch}
            returnKeyType="search"
          />
        </View>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
  },
  header: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 15,
  },
  iconButton: {
    width: 40,
    height: 40,
    borderRadius: 20,
    justifyContent: 'center',
    alignItems: 'center',
    shadowColor: "#000",
    shadowOffset: { width: 0, height: 1 },
    shadowOpacity: 0.1,
    shadowRadius: 2,
    elevation: 2,
  },
  title: {
    fontSize: 34,
    fontWeight: 'bold',
  },
  bottomSection: {
    width: '100%',
    marginTop: 10,
    // backgroundColor: "#FFF"
    // marginBottom: -70,
  },
  filterRow: {
    flexDirection: 'row',
    alignItems: 'center',
    marginBottom: 10,
  },
  chip: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 6,
    paddingHorizontal: 12,
    borderRadius: 8,
    marginRight: 10,
  },
  chipIcon: {
    marginRight: 4,
  },
  chipIconRight: {
    marginLeft: 6,
  },
  chipText: {
    fontSize: 12,
    fontWeight: '500',
  },
  switch: {
    marginLeft: 10,
  },
  searchContainer: {
    flexDirection: 'row',
    alignItems: 'center',
    borderRadius: 20,
    paddingHorizontal: 15,
    paddingVertical: 8,
    marginTop: -11,
    marginBottom: 5,
  },
  searchIcon: {
    marginRight: 10,
  },
  searchInput: {
    flex: 1,
    fontSize: 15,
  },
  resultItem: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 12,
    borderBottomWidth: StyleSheet.hairlineWidth,
    borderBottomColor: '#3a3a3c',
  },
  resultImageContainer: {
    marginRight: 15,
  },
  resultImage: {
    width: 60,
    height: 60,
    borderRadius: 8,
  },
  resultImagePlaceholder: {
    width: 60,
    height: 60,
    borderRadius: 8,
    justifyContent: 'center',
    alignItems: 'center',
  },
  resultDetails: {
    flex: 1,
    justifyContent: 'center',
  },
  resultTitle: {
    fontSize: 16,
    fontWeight: '500',
    marginBottom: 4,
  },
  resultApp: {
    fontSize: 14,
    color: '#ff9f0a', // Orange color similar to the screenshot
    marginBottom: 4,
  },
  resultFooter: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  resultDate: {
    fontSize: 12,
    color: '#8e8e93',
  },
  resultSize: {
    fontSize: 13,
    fontWeight: '600',
  },
});
