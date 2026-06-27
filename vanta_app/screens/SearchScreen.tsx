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
  Animated,
  Keyboard } from 'react-native';
import { useTheme } from '../ThemeContext';
import { useNavigation } from '@react-navigation/native';
import { Ionicons } from '@expo/vector-icons';
import { searchImages } from '../modules/vanta-bridge';

const StaggeredResultItem = ({ item, index, colors }: any) => {
  const [displayItem, setDisplayItem] = useState(item);
  const rotateAnim = useRef(new Animated.Value(0)).current; 

  const col = index % 3;
  const row = Math.floor(index / 3);
  const diagonalIndex = row + col;
  const staggerDelay = Math.min(diagonalIndex * 40, 800); // Cap delay for large grids

  React.useEffect(() => {
    if (item?.abs_path !== displayItem?.abs_path) {
      Animated.timing(rotateAnim, {
        toValue: 90,
        duration: 150,
        delay: staggerDelay,
        useNativeDriver: true,
      }).start(() => {
        setDisplayItem(item);
        rotateAnim.setValue(-90);
        Animated.timing(rotateAnim, {
          toValue: 0,
          duration: 150,
          useNativeDriver: true,
        }).start();
      });
    } else {
      setDisplayItem(item); // sync just in case
    }
  }, [item]);

  const rotateY = rotateAnim.interpolate({
    inputRange: [-90, 0, 90],
    outputRange: ['-90deg', '0deg', '90deg']
  });

  const rotateX = rotateAnim.interpolate({
    inputRange: [-90, 0, 90],
    outputRange: ['-90deg', '0deg', '90deg']
  });

  return (
    <Animated.View style={{ width: '33.33%', aspectRatio: 1, padding: 1, transform: [{ scaleY: -1 }, { rotateY }, { rotateX }] }}>
      {displayItem?.abs_path ? (
        <Image source={{ uri: 'file://' + displayItem.abs_path }} style={{ width: '100%', height: '100%' }} />
      ) : (
        <View style={{ width: '100%', height: '100%', backgroundColor: colors.chipBackground, justifyContent: 'center', alignItems: 'center' }}>
          <Ionicons name="image-outline" size={24} color={colors.text} />
        </View>
      )}
    </Animated.View>
  );
};

export default function SearchScreen() {
  const { colors } = useTheme();
  const navigation = useNavigation<any>();
  const [searchQuery, setSearchQuery] = useState('');
  const [isExclusive, setIsExclusive] = useState(true);
  const [results, setResults] = useState<any[]>([]);
  const [isSearching, setIsSearching] = useState(false);
  const scrollViewRef = useRef<any>(null);
  
  const keyboardOffset = useRef(new Animated.Value(0)).current;

  React.useEffect(() => {
    const showSub = Keyboard.addListener(
      Platform.OS === 'ios' ? 'keyboardWillShow' : 'keyboardDidShow',
      (e) => {
        Animated.timing(keyboardOffset, {
          toValue: e.endCoordinates.height,
          duration: 250,
          useNativeDriver: false,
        }).start();
      }
    );
    const hideSub = Keyboard.addListener(
      Platform.OS === 'ios' ? 'keyboardWillHide' : 'keyboardDidHide',
      (e) => {
        Animated.timing(keyboardOffset, {
          toValue: 0,
          duration: 250,
          useNativeDriver: false,
        }).start();
      }
    );
    return () => {
      showSub.remove();
      hideSub.remove();
    };
  }, []);

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
    
    // Jump back to the bottom (origin) for the new search
    scrollViewRef.current?.scrollTo({ y: 0, animated: true });
    
    setIsSearching(true);
    try {
      const jsonRes = await searchImages(searchQuery);
      const parsedResults = JSON.parse(jsonRes);
      setResults(Array.isArray(parsedResults) ? parsedResults : []);
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
      <View style={[styles.header, { position: 'absolute', top: 50, left: 20, right: 20, zIndex: 10 }]}>
        {results.length > 0 || isSearching ? (
          <TouchableOpacity style={[styles.iconButton, { backgroundColor: 'rgba(0,0,0,0.6)' }]} onPress={() => {
            setSearchQuery('');
            setResults([]);
          }}>
            <Ionicons name="chevron-back" size={24} color="#ffffff" />
          </TouchableOpacity>
        ) : (
          <View style={{ width: 40, height: 40 }} />
        )}
        
        <Text style={{ color: '#ffffff', fontSize: 20, fontWeight: 'bold' }}>
          {results.length > 0 ? `${results.length} Results` : 'Search'}
        </Text>

        <TouchableOpacity style={[styles.iconButton, { backgroundColor: 'rgba(0,0,0,0.6)' }]} onPress={() => navigation.navigate('Settings')}>
          <Ionicons name="settings-sharp" size={20} color="#ffffff" />
        </TouchableOpacity>
      </View>

      <Animated.ScrollView
        ref={scrollViewRef}
        style={{ transform: [{ scaleY: -1 }] }}
        contentContainerStyle={{ flexGrow: 1, paddingTop: 180, paddingBottom: 105 }}
        keyboardShouldPersistTaps="handled"
        showsVerticalScrollIndicator={false}
        onScroll={Animated.event(
          [{ nativeEvent: { contentOffset: { y: scrollY } } }],
          { useNativeDriver: true }
        )}
        scrollEventThrottle={16}
      >
        {/* Search Results List */}
        <View style={{ flex: 1 }}>
          {results.length === 0 && !isSearching ? (
            <View style={{ flex: 1, justifyContent: 'center', alignItems: 'center', marginTop: '40%', transform: [{ scaleY: -1 }] }}>
              <Ionicons name="images-outline" size={64} color="#8e8e93" />
              <Text style={{ color: colors.text, fontSize: 20, fontWeight: 'bold', marginTop: 20 }}>Search your photos</Text>
              <Text style={{ color: '#8e8e93', fontSize: 14, marginTop: 8 }}>Results will appear here in a grid</Text>
            </View>
          ) : (
            <View style={{ flexDirection: 'row', flexWrap: 'wrap' }}>
              {results.map((item, index) => (
                <StaggeredResultItem key={index} item={item} index={index} colors={colors} />
              ))}
            </View>
          )}

          {isSearching && (
            <View style={{ position: 'absolute', top: 0, left: 0, right: 0, bottom: 0, justifyContent: 'center', alignItems: 'center', backgroundColor: 'rgba(0,0,0,0.3)', transform: [{ scaleY: -1 }] }}>
              <ActivityIndicator size="large" color={colors.primary} />
            </View>
          )}
        </View>
      </Animated.ScrollView>

      {/* Filters Section */}
      <Animated.View style={[styles.bottomSection, { 
        position: 'absolute', 
        bottom: keyboardOffset, 
        left: 0, 
        right: 0, 
        zIndex: 10,
        backgroundColor: 'transparent', 
        paddingHorizontal: 20, 
        paddingBottom: Platform.OS === 'ios' ? 30 : 20 
      }]}>
        {/* Row 1 */}
        <View style={styles.filterRow}>
          {filters.slice(0, 2).map((filter) => (
            <TouchableOpacity
              key={filter.id}
              style={[
                styles.chip,
                { backgroundColor: filter.active ? '#e5e5ea' : colors.chipBackground }
              ]}
              onPress={() => toggleFilter(filter.id)}
            >
              <Text style={[
                styles.chipText,
                { color: filter.active ? '#1c1c1e' : colors.chipText }
              ]}>
                {filter.label}
              </Text>
              {filter.active ? (
                <Ionicons name="checkmark" size={14} color="#1c1c1e" style={styles.chipIconRight} />
              ) : (
                <Ionicons name="close" size={14} color={colors.chipText} style={styles.chipIconRight} />
              )}
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
                  { backgroundColor: filter.active ? '#e5e5ea' : colors.chipBackground }
                ]}
                onPress={() => toggleFilter(filter.id)}
              >
                <Text style={[
                  styles.chipText,
                  { color: filter.active ? '#1c1c1e' : colors.chipText }
                ]}>
                  {filter.label}
                </Text>
                {filter.active ? (
                  <Ionicons name="checkmark" size={14} color="#1c1c1e" style={styles.chipIconRight} />
                ) : (
                  <Ionicons name="close" size={14} color={colors.chipText} style={styles.chipIconRight} />
                )}
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
      </Animated.View>
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
    borderRadius: 20,
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
    borderRadius: 30,
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
