import React, { useState, useRef } from 'react';
import {
  View,
  Text,
  StyleSheet,
  TextInput,
  TouchableOpacity,
  Platform,
  ScrollView,
  Image,
  ActivityIndicator,
  Animated,
  Keyboard,
} from 'react-native';
import { useTheme } from '../ThemeContext';
import { useNavigation, NavigationProp } from '@react-navigation/native';
import { Ionicons } from '@expo/vector-icons';
import type { RootStackParamList } from '../App';
import { searchImages } from '../modules/vanta-bridge';
import { useVantaError } from '../hooks/useVantaError';

/**
 * Shape of a single search result returned by the native engine.
 */
interface SearchResultItem {
  abs_path: string;
  file_type?: string;
  display_name?: string;
  size?: number;
  date_taken?: number;
}

/**
 * Animated grid cell that flips when its item changes.
 *
 * `item` is the current search result. `index` is used to compute a diagonal
 * stagger delay so the grid animates in a wave. `colors` comes from the theme.
 */
const StaggeredResultItem = ({
  item,
  index,
  colors,
}: {
  item: SearchResultItem;
  index: number;
  colors: ReturnType<typeof useTheme>['colors'];
}) => {
  const [displayItem, setDisplayItem] = useState<SearchResultItem>(item);
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
    outputRange: ['-90deg', '0deg', '90deg'],
  });

  const rotateX = rotateAnim.interpolate({
    inputRange: [-90, 0, 90],
    outputRange: ['-90deg', '0deg', '90deg'],
  });

  return (
    <Animated.View
      style={{
        width: '33.33%',
        aspectRatio: 1,
        padding: 1,
        transform: [{ scaleY: -1 }, { rotateY }, { rotateX }],
      }}
    >
      {displayItem?.abs_path ? (
        <Image
          source={{ uri: 'file://' + displayItem.abs_path }}
          style={{ width: '100%', height: '100%' }}
        />
      ) : (
        <View
          style={{
            width: '100%',
            height: '100%',
            backgroundColor: colors.chipBackground,
            justifyContent: 'center',
            alignItems: 'center',
          }}
        >
          <Ionicons name="image-outline" size={24} color={colors.text} />
        </View>
      )}
    </Animated.View>
  );
};

export default function SearchScreen() {
  const { colors } = useTheme();
  const navigation = useNavigation<NavigationProp<RootStackParamList>>();
  const [searchQuery, setSearchQuery] = useState('');
  const [results, setResults] = useState<SearchResultItem[]>([]);
  const [isSearching, setIsSearching] = useState(false);
  // Ref to the results scroll view so we can scroll to top on new searches.
  const scrollViewRef = useRef<ScrollView | null>(null);

  const handleError = useVantaError('Search failed');

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
      () => {
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

  // Animated value that tracks the scroll position; wired to the search results
  // scroll view so the UI can react to scrolling.
  const scrollY = useRef(new Animated.Value(0)).current;

  const handleSearch = async () => {
    if (!searchQuery.trim()) return;

    // Jump back to the bottom (origin) for the new search
    scrollViewRef.current?.scrollTo({ y: 0, animated: true });

    setIsSearching(true);
    try {
      const jsonRes = await searchImages(searchQuery);
      const parsedResults = JSON.parse(jsonRes);
      setResults(Array.isArray(parsedResults) ? parsedResults : []);
    } catch (err) {
      handleError(err);
    } finally {
      setIsSearching(false);
    }
  };

  return (
    <View style={[styles.container, { backgroundColor: colors.background }]}>
      <View
        style={[styles.header, { position: 'absolute', top: 50, left: 20, right: 20, zIndex: 10 }]}
      >
        <View style={{ width: 90, alignItems: 'flex-start' }}>
          {(results.length > 0 || isSearching) && (
            <TouchableOpacity
              style={[styles.iconButton, { backgroundColor: 'rgba(0,0,0,0.6)' }]}
              onPress={() => {
                setSearchQuery('');
                setResults([]);
              }}
            >
              <Ionicons name="chevron-back" size={24} color="#ffffff" />
            </TouchableOpacity>
          )}
        </View>

        <Text style={{ color: '#ffffff', fontSize: 20, fontWeight: 'bold' }}>
          {results.length > 0 ? `${results.length} Results` : 'Search'}
        </Text>

        <View style={{ width: 90, flexDirection: 'row', justifyContent: 'flex-end' }}>
          <TouchableOpacity
            style={[styles.iconButton, { backgroundColor: 'rgba(0,0,0,0.6)', marginRight: 10 }]}
            onPress={() => navigation.navigate('People')}
          >
            <Ionicons name="people" size={20} color="#ffffff" />
          </TouchableOpacity>
          <TouchableOpacity
            style={[styles.iconButton, { backgroundColor: 'rgba(0,0,0,0.6)' }]}
            onPress={() => navigation.navigate('Settings')}
          >
            <Ionicons name="settings-sharp" size={20} color="#ffffff" />
          </TouchableOpacity>
        </View>
      </View>

      <Animated.ScrollView
        ref={scrollViewRef}
        style={{ transform: [{ scaleY: -1 }] }}
        contentContainerStyle={{ flexGrow: 1, paddingTop: 180, paddingBottom: 105 }}
        keyboardShouldPersistTaps="handled"
        showsVerticalScrollIndicator={false}
        onScroll={Animated.event([{ nativeEvent: { contentOffset: { y: scrollY } } }], {
          useNativeDriver: true,
        })}
        scrollEventThrottle={16}
      >
        {/* Search Results List */}
        <View style={{ flex: 1 }}>
          {results.length === 0 && !isSearching ? (
            <View
              style={{
                flex: 1,
                justifyContent: 'center',
                alignItems: 'center',
                marginTop: '40%',
                transform: [{ scaleY: -1 }],
              }}
            >
              <Ionicons name="images-outline" size={64} color="#8e8e93" />
              <Text style={{ color: colors.text, fontSize: 20, fontWeight: 'bold', marginTop: 20 }}>
                Search your photos
              </Text>
              <Text style={{ color: '#8e8e93', fontSize: 14, marginTop: 8 }}>
                Results will appear here in a grid
              </Text>
            </View>
          ) : (
            <View style={{ flexDirection: 'row', flexWrap: 'wrap' }}>
              {results.map((item, index) => (
                <StaggeredResultItem key={index} item={item} index={index} colors={colors} />
              ))}
            </View>
          )}

          {isSearching && (
            <View
              style={{
                position: 'absolute',
                top: 0,
                left: 0,
                right: 0,
                bottom: 0,
                justifyContent: 'center',
                alignItems: 'center',
                backgroundColor: 'rgba(0,0,0,0.3)',
                transform: [{ scaleY: -1 }],
              }}
            >
              <ActivityIndicator size="large" color={colors.primary} />
            </View>
          )}
        </View>
      </Animated.ScrollView>

      {/* Filters Section */}
      <Animated.View
        style={[
          styles.bottomSection,
          {
            position: 'absolute',
            bottom: keyboardOffset,
            left: 0,
            right: 0,
            zIndex: 10,
            backgroundColor: 'transparent',
            paddingHorizontal: 20,
            paddingBottom: Platform.OS === 'ios' ? 30 : 20,
          },
        ]}
      >
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
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 1 },
    shadowOpacity: 0.1,
    shadowRadius: 2,
    elevation: 2,
  },
  bottomSection: {
    width: '100%',
    marginTop: 10,
    // backgroundColor: "#FFF"
    // marginBottom: -70,
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
});
