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
  Pressable,
  BackHandler,
  Alert,
} from 'react-native';
import { useTheme } from '../ThemeContext';
import { useNavigation, NavigationProp } from '@react-navigation/native';
import { Ionicons } from '@expo/vector-icons';
import type { RootStackParamList } from '../App';
import { searchImages, searchDocuments, openFile } from '../modules/vanta-bridge';
import { useVantaError } from '../hooks/useVantaError';
import { MediaViewer, isVideo, share } from '../MediaViewer';

/**
 * Shape of a single search result returned by the native engine.
 */
interface SearchResultItem {
  abs_path: string;
  file_type?: string;
  display_name?: string;
  size_bytes?: number;
  mtime_unix?: number;
}

/** A document matched by full-text search. */
interface DocumentResultItem {
  abs_path: string;
  display_name?: string;
  snippet?: string;
}

/**
 * Animated grid cell that flips when its item changes.
 *
 * `item` is the current search result. `index` is used to compute a diagonal
 * stagger delay so the grid animates in a wave. `colors` comes from the theme.
 * Tap and long-press are reported to the parent, which decides between
 * opening the viewer and toggling selection.
 */
const StaggeredResultItem = ({
  item,
  index,
  colors,
  selected,
  onPress,
  onLongPress,
}: {
  item: SearchResultItem;
  index: number;
  colors: ReturnType<typeof useTheme>['colors'];
  selected: boolean;
  onPress: () => void;
  onLongPress: () => void;
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
      <Pressable
        testID={`result-${index}`}
        style={{ flex: 1 }}
        onPress={onPress}
        onLongPress={onLongPress}
      >
        {displayItem?.abs_path ? (
          <>
            <Image
              source={{ uri: 'file://' + displayItem.abs_path }}
              style={{ width: '100%', height: '100%', opacity: selected ? 0.5 : 1 }}
            />
            {isVideo(displayItem.abs_path) && (
              <Ionicons name="play-circle" size={22} color="#ffffff" style={styles.cellBadgeLeft} />
            )}
            {selected && (
              <Ionicons
                name="checkmark-circle"
                size={24}
                color={colors.primary}
                style={styles.cellBadgeRight}
              />
            )}
          </>
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
      </Pressable>
    </Animated.View>
  );
};

/**
 * SearchScreen
 * The main landing page of the application where users can input text queries to search their media.
 * It handles the keyboard animations smoothly and displays search results in an animated, staggered grid.
 */
export default function SearchScreen() {
  const { colors } = useTheme();
  const navigation = useNavigation<NavigationProp<RootStackParamList>>();
  const [searchQuery, setSearchQuery] = useState('');
  const [results, setResults] = useState<SearchResultItem[]>([]);
  const [docs, setDocs] = useState<DocumentResultItem[]>([]);
  const [isSearching, setIsSearching] = useState(false);
  // True once a search has completed, so an empty result set can be told apart
  // from the idle "not searched yet" state.
  const [hasSearched, setHasSearched] = useState(false);
  // Ref to the results scroll view so we can scroll to top on new searches.
  const scrollViewRef = useRef<ScrollView | null>(null);

  // Gallery-style selection: long-press a photo to start selecting, then tap
  // to toggle. Keyed by abs_path. While nothing is selected, a tap opens the
  // full-screen viewer at that photo.
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [viewerIndex, setViewerIndex] = useState<number | null>(null);
  const selecting = selected.size > 0;
  const clearSelection = () => setSelected(new Set());

  const toggleSelected = (path: string) =>
    setSelected((prev) => {
      const next = new Set(prev);
      if (!next.delete(path)) next.add(path);
      return next;
    });

  // Android back button leaves selection mode instead of leaving the app.
  React.useEffect(() => {
    if (!selecting) return;
    const sub = BackHandler.addEventListener('hardwareBackPress', () => {
      clearSelection();
      return true;
    });
    return () => sub.remove();
  }, [selecting]);

  const handleShare = async () => {
    // Share in the order shown, not the order tapped.
    const paths = results.map((r) => r.abs_path).filter((p) => selected.has(p));
    if (await share(paths)) clearSelection();
  };

  const handleError = useVantaError('Search failed');
  const total = results.length + docs.length;

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

  /**
   * handleSearch
   * Asynchronously calls the native C++ engine to perform a semantic search.
   * It sets the UI into a loading state, issues the query, and parses the JSON response
   * to display the results in the grid. If an error occurs, it is handled via the Vanta error boundary.
   */
  const handleSearch = async () => {
    if (!searchQuery.trim()) return;

    // Jump back to the bottom (origin) for the new search
    scrollViewRef.current?.scrollTo({ y: 0, animated: true });

    clearSelection();
    setIsSearching(true);
    try {
      // A failed document search must not hide the photo results.
      const [jsonRes, docsRes] = await Promise.all([
        searchImages(searchQuery),
        searchDocuments(searchQuery).catch(() => '[]'),
      ]);
      const parsedResults = JSON.parse(jsonRes);
      const parsedDocs = JSON.parse(docsRes);
      setResults(Array.isArray(parsedResults) ? parsedResults : []);
      setDocs(Array.isArray(parsedDocs) ? parsedDocs : []);
      setHasSearched(true);
    } catch (err) {
      // A failed search is not "no results"; fall back to the idle view.
      setHasSearched(false);
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
          {(total > 0 || isSearching || hasSearched) && (
            <TouchableOpacity
              accessibilityLabel={selecting ? 'Cancel selection' : 'Clear search'}
              style={[styles.iconButton, { backgroundColor: 'rgba(0,0,0,0.6)' }]}
              onPress={() => {
                if (selecting) return clearSelection();
                setSearchQuery('');
                setResults([]);
                setDocs([]);
                setHasSearched(false);
              }}
            >
              <Ionicons name={selecting ? 'close' : 'chevron-back'} size={24} color="#ffffff" />
            </TouchableOpacity>
          )}
        </View>

        <Text style={{ color: '#ffffff', fontSize: 20, fontWeight: 'bold' }}>
          {selecting ? `${selected.size} selected` : total > 0 ? `${total} Results` : 'Search'}
        </Text>

        <View style={{ width: 90, flexDirection: 'row', justifyContent: 'flex-end' }}>
          {selecting ? (
            <TouchableOpacity
              accessibilityLabel="Share"
              style={[styles.iconButton, { backgroundColor: 'rgba(0,0,0,0.6)' }]}
              onPress={handleShare}
            >
              <Ionicons name="share-social" size={20} color="#ffffff" />
            </TouchableOpacity>
          ) : (
            <>
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
            </>
          )}
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
          {total === 0 && !isSearching ? (
            <View
              style={{
                flex: 1,
                justifyContent: 'center',
                alignItems: 'center',
                marginTop: '40%',
                transform: [{ scaleY: -1 }],
              }}
            >
              <Ionicons
                name={hasSearched ? 'search-outline' : 'images-outline'}
                size={64}
                color="#8e8e93"
              />
              <Text style={{ color: colors.text, fontSize: 20, fontWeight: 'bold', marginTop: 20 }}>
                {hasSearched ? 'No results' : 'Search your photos'}
              </Text>
              <Text style={{ color: '#8e8e93', fontSize: 14, marginTop: 8 }}>
                {hasSearched
                  ? 'Nothing matched your search. Try different words.'
                  : 'Results will appear here in a grid'}
              </Text>
            </View>
          ) : (
            <>
              {/* First child of the inverted scroll view, so it sits nearest the search bar. */}
              {docs.length > 0 && (
                <View
                  style={{ transform: [{ scaleY: -1 }], paddingHorizontal: 8, paddingBottom: 8 }}
                >
                  {docs.map((doc, i) => (
                    <Pressable
                      key={doc.abs_path}
                      testID={`doc-${i}`}
                      style={[styles.docRow, { backgroundColor: colors.chipBackground }]}
                      onPress={async () => {
                        if (!(await openFile(doc.abs_path))) {
                          Alert.alert(
                            'Cannot open file',
                            'No installed app can open this document.'
                          );
                        }
                      }}
                    >
                      <Ionicons name="document-text-outline" size={26} color={colors.text} />
                      <View style={{ flex: 1, marginLeft: 10 }}>
                        <Text numberOfLines={1} style={{ color: colors.text, fontWeight: '600' }}>
                          {doc.display_name || doc.abs_path.split('/').pop()}
                        </Text>
                        {!!doc.snippet && (
                          <Text numberOfLines={2} style={{ color: '#8e8e93', fontSize: 12 }}>
                            {doc.snippet}
                          </Text>
                        )}
                      </View>
                    </Pressable>
                  ))}
                </View>
              )}
              <View style={{ flexDirection: 'row', flexWrap: 'wrap' }}>
                {results.map((item, index) => (
                  <StaggeredResultItem
                    key={index}
                    item={item}
                    index={index}
                    colors={colors}
                    selected={selected.has(item.abs_path)}
                    onPress={() =>
                      selecting ? toggleSelected(item.abs_path) : setViewerIndex(index)
                    }
                    onLongPress={() => toggleSelected(item.abs_path)}
                  />
                ))}
              </View>
            </>
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
            onChangeText={(text) => {
              setSearchQuery(text);
              // Clearing the query returns an empty result view to idle.
              if (!text.trim()) setHasSearched(false);
            }}
            onSubmitEditing={handleSearch}
            returnKeyType="search"
          />
        </View>
      </Animated.View>

      <MediaViewer
        paths={results.map((r) => r.abs_path)}
        index={viewerIndex}
        onClose={() => setViewerIndex(null)}
      />
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
  docRow: {
    flexDirection: 'row',
    alignItems: 'center',
    borderRadius: 12,
    padding: 10,
    marginTop: 6,
  },
  cellBadgeLeft: { position: 'absolute', left: 4, bottom: 4 },
  cellBadgeRight: { position: 'absolute', right: 4, top: 4 },
  searchInput: {
    flex: 1,
    fontSize: 15,
  },
});
