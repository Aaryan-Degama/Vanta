import React, { useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  Switch,
  TouchableOpacity,
  ActivityIndicator,
  PermissionsAndroid,
  Platform,
  Animated,
} from 'react-native';
import { useTheme } from '../ThemeContext';
import { useNavigation } from '@react-navigation/native';
import { Ionicons } from '@expo/vector-icons';
import {
  startStoring,
  generateEmbeddings,
  getIndexProgress,
  getDatabaseStats,
  pauseEmbeddings,
} from '../modules/vanta-bridge';
import { formatBytes } from '../utils/formatting';

/**
 * Single row in the storage-usage legend.
 *
 * Declared outside the screen component so it is not recreated on every render.
 * The label/value/text colors depend on the current theme, which is passed in
 * as a prop.
 */
function LegendItem({
  color,
  label,
  value,
  textColor,
  chevronColor,
}: {
  color: string;
  label: string;
  value: string;
  textColor: string;
  chevronColor: string;
}) {
  return (
    <View style={styles.legendRow}>
      <View style={styles.legendLeft}>
        <View style={[styles.dot, { backgroundColor: color }]} />
        <Text style={[styles.legendLabel, { color: textColor }]}>{label}</Text>
      </View>
      <View style={styles.legendRight}>
        <Text style={[styles.legendValue, { color: textColor }]}>{value}</Text>
        <Ionicons name="chevron-forward" size={16} color={chevronColor} />
      </View>
    </View>
  );
}

export default function SettingsScreen() {
  const { isDarkMode, toggleDarkMode, colors } = useTheme();
  const navigation = useNavigation<any>();
  const [isIndexing, setIsIndexing] = useState(false);
  const [isPaused, setIsPaused] = useState(false);
  const [indexProgress, setIndexProgress] = useState(0);
  const [currentFile, setCurrentFile] = useState<string>('');
  const [prevFile, setPrevFile] = useState<string>('');
  const [dbStats, setDbStats] = useState<any>({});

  const fadeOutAnim = React.useRef(new Animated.Value(1)).current;
  const translateOutYAnim = React.useRef(new Animated.Value(0)).current;
  const fadeInAnim = React.useRef(new Animated.Value(0)).current;
  const translateInYAnim = React.useRef(new Animated.Value(10)).current;

  React.useEffect(() => {
    if (currentFile) {
      fadeOutAnim.setValue(1);
      translateOutYAnim.setValue(0);
      fadeInAnim.setValue(0);
      translateInYAnim.setValue(10);
      Animated.parallel([
        Animated.timing(fadeOutAnim, {
          toValue: 0,
          duration: 300,
          useNativeDriver: true,
        }),
        Animated.timing(translateOutYAnim, {
          toValue: -10,
          duration: 300,
          useNativeDriver: true,
        }),
        Animated.timing(fadeInAnim, {
          toValue: 1,
          duration: 300,
          useNativeDriver: true,
        }),
        Animated.timing(translateInYAnim, {
          toValue: 0,
          duration: 300,
          useNativeDriver: true,
        }),
      ]).start();
    }
  }, [currentFile]);

  // Holds the interval ID for the index-progress polling loop.
  const pollIntervalRef = React.useRef<ReturnType<typeof setInterval> | null>(null);

  /**
   * Fetches database statistics from the native engine and updates the UI.
   *
   * Declared as a function (not a const) so it is hoisted and can be called
   * from the initial useEffect below without triggering the immutability lint
   * rule.
   */
  async function fetchStats() {
    try {
      const statsJson = await getDatabaseStats();
      console.log('RAW DB STATS FROM C++:', statsJson);
      const parsed = JSON.parse(statsJson);
      setDbStats(parsed);

      if (parsed.target_files_count && parsed.target_files_count > 0) {
        let percent = Math.ceil(((parsed.clip_vec_count || 0) / parsed.target_files_count) * 100);
        if (percent > 100) percent = 100;
        setIndexProgress(percent);
      }
    } catch (e) {
      console.error(e);
    }
  }

  /**
   * Starts a 1-second polling loop that reads native indexing progress.
   *
   * Like fetchStats, this is a function declaration so it can be referenced
   * from the status-check effect before its textual definition.
   */
  function startPolling() {
    // Clear any existing interval before starting a new one.
    if (pollIntervalRef.current) clearInterval(pollIntervalRef.current);
    pollIntervalRef.current = setInterval(async () => {
      try {
        const progressString = await getIndexProgress();
        if (progressString) {
          const data = JSON.parse(progressString);

          if (data.total && data.total > 0 && data.status === 'processing') {
            let percent = Math.ceil((data.processed / data.total) * 100);
            if (percent > 100) percent = 100;
            setIndexProgress(percent);
          }

          if (data.status === 'finished' || data.status === 'paused') {
            if (pollIntervalRef.current) clearInterval(pollIntervalRef.current);
            setIsIndexing(false);
            if (data.status === 'paused') setIsPaused(true);
            setCurrentFile('');
            fetchStats();
          } else if (data.status === 'processing' || data.status === 'loading_models') {
            setIsIndexing(true);
            setIsPaused(false);
            if (data.currentFile) {
              setCurrentFile((prev) => {
                if (prev !== data.currentFile) {
                  setPrevFile(prev);
                  return data.currentFile;
                }
                return prev;
              });
            }
          }
        }
      } catch (err) {
        // Ignore
      }
    }, 1000);
  }

  // On mount: load stats once and check whether indexing is already running.
  React.useEffect(() => {
    fetchStats();

    const checkStatus = async () => {
      try {
        const progressString = await getIndexProgress();
        if (progressString) {
          const data = JSON.parse(progressString);
          if (data.status === 'processing' || data.status === 'loading_models') {
            setIsIndexing(true);
            setIsPaused(false);
            if (data.currentFile) {
              setCurrentFile((prev) => {
                if (prev !== data.currentFile) {
                  setPrevFile(prev);
                  return data.currentFile;
                }
                return prev;
              });
            }
            startPolling();
          } else if (data.status === 'paused') {
            setIsPaused(true);
          }
        }
      } catch (e) {
        // Index progress may be unavailable before the first scan; ignore.
      }
    };
    checkStatus();

    return () => {
      if (pollIntervalRef.current) clearInterval(pollIntervalRef.current);
    };
  }, []);

  const handleStartIndexing = async () => {
    if (isIndexing) return;

    setIsIndexing(true);
    setIsPaused(false);

    if (Platform.OS === 'android') {
      try {
        const permissions = await PermissionsAndroid.requestMultiple([
          PermissionsAndroid.PERMISSIONS.READ_MEDIA_IMAGES,
          PermissionsAndroid.PERMISSIONS.READ_MEDIA_VIDEO,
          PermissionsAndroid.PERMISSIONS.READ_MEDIA_AUDIO,
        ]);
        if (
          permissions[PermissionsAndroid.PERMISSIONS.READ_MEDIA_IMAGES] !==
            PermissionsAndroid.RESULTS.GRANTED &&
          permissions[PermissionsAndroid.PERMISSIONS.READ_MEDIA_VIDEO] !==
            PermissionsAndroid.RESULTS.GRANTED &&
          permissions[PermissionsAndroid.PERMISSIONS.READ_MEDIA_AUDIO] !==
            PermissionsAndroid.RESULTS.GRANTED
        ) {
          console.warn('Permissions not granted');
        }
      } catch (err) {
        console.warn(err);
      }
    }

    try {
      console.log('Starting scan...');
      await startStoring();

      startPolling();

      console.log('Generating embeddings...');
      const success = await generateEmbeddings();
      if (!success) {
        alert('Failed to load AI Models! Did you adb push them?');
        setIsIndexing(false);
        if (pollIntervalRef.current) clearInterval(pollIntervalRef.current);
      }
    } catch (e) {
      console.error(e);
      setIsIndexing(false);
      if (pollIntervalRef.current) clearInterval(pollIntervalRef.current);
    }
  };

  const handlePauseIndexing = async () => {
    try {
      await pauseEmbeddings();
      setIsPaused(true);
      setIsIndexing(false);
      if (pollIntervalRef.current) clearInterval(pollIntervalRef.current);
    } catch (e) {
      console.error('Failed to pause', e);
    }
  };

  const imagesData = dbStats['image'] || dbStats['picture'] || { count: 0, size: 0 };
  const videosData = dbStats['video'] || { count: 0, size: 0 };
  const audioData = dbStats['audio'] || { count: 0, size: 0 };
  const docsData = dbStats['document'] || { count: 0, size: 0 };

  const totalSize = imagesData.size + videosData.size + audioData.size + docsData.size;
  const getFlex = (size: number) => (totalSize > 0 ? (size / totalSize) * 100 : 0);

  return (
    <View style={[styles.container, { backgroundColor: colors.background }]}>
      <TouchableOpacity
        onPress={() => navigation.goBack()}
        style={[styles.iconButton, { backgroundColor: colors.iconBackground }]}
      >
        <Ionicons name="chevron-back" size={24} color={colors.iconColor} />
      </TouchableOpacity>
      <Text style={[styles.title, { color: colors.text }]}>Settings</Text>

      <View style={styles.indexStatusRow}>
        <View style={{ flex: 1, paddingRight: 10 }}>
          <Text style={[styles.percentageText, { color: colors.text }]}>
            {indexProgress}%{' '}
            <Text style={[styles.indexedLabel, { color: colors.text }]}>indexed</Text>
          </Text>
          {isIndexing && (
            <View style={{ flexDirection: 'row', alignItems: 'center', marginTop: 4 }}>
              <Text style={{ color: colors.text, opacity: 0.6, fontSize: 13 }}>
                Status: {currentFile ? 'Indexing -> ' : 'Loading AI models...'}
              </Text>
              <View style={{ flex: 1, height: 18, overflow: 'hidden', justifyContent: 'center' }}>
                {prevFile ? (
                  <Animated.Text
                    style={{
                      position: 'absolute',
                      color: colors.text,
                      opacity: fadeOutAnim.interpolate({
                        inputRange: [0, 1],
                        outputRange: [0, 0.6],
                      }),
                      transform: [{ translateY: translateOutYAnim }],
                      fontSize: 13,
                      width: '100%',
                    }}
                    numberOfLines={1}
                    ellipsizeMode="middle"
                  >
                    {prevFile.split('/').pop()}
                  </Animated.Text>
                ) : null}

                {currentFile ? (
                  <Animated.Text
                    style={{
                      position: 'absolute',
                      color: colors.text,
                      opacity: fadeInAnim.interpolate({
                        inputRange: [0, 1],
                        outputRange: [0, 0.6],
                      }),
                      transform: [{ translateY: translateInYAnim }],
                      fontSize: 13,
                      width: '100%',
                    }}
                    numberOfLines={1}
                    ellipsizeMode="middle"
                  >
                    {currentFile.split('/').pop()}
                  </Animated.Text>
                ) : null}
              </View>
            </View>
          )}
        </View>
        <TouchableOpacity
          style={[
            styles.startButton,
            isIndexing
              ? { backgroundColor: '#34c759', borderRadius: 30 }
              : { backgroundColor: '#ffffff', borderRadius: 30 },
          ]}
          onPress={isIndexing ? handlePauseIndexing : handleStartIndexing}
        >
          {isIndexing ? (
            <View style={{ flexDirection: 'row', alignItems: 'center' }}>
              <ActivityIndicator color="#fff" size="small" style={{ marginRight: 8 }} />
              <Text style={styles.startButtonText}>Pause</Text>
            </View>
          ) : (
            <Text style={[styles.startButtonText, { color: '#000000', fontWeight: 'bold' }]}>
              {(indexProgress > 0 && indexProgress < 100) || isPaused ? 'Resume' : 'Start Indexing'}
            </Text>
          )}
        </TouchableOpacity>
      </View>

      <View style={[styles.card, { backgroundColor: isDarkMode ? '#3a3a3c' : '#f2f2f7' }]}>
        <View style={styles.stackedBarContainer}>
          <View
            style={[
              styles.barSegment,
              { flex: getFlex(imagesData.size) || 1, backgroundColor: '#ff4d6d' },
            ]}
          />
          <View
            style={[
              styles.barSegment,
              { flex: getFlex(videosData.size), backgroundColor: '#7b2cbf' },
            ]}
          />
          <View
            style={[
              styles.barSegment,
              { flex: getFlex(audioData.size), backgroundColor: '#00b4d8' },
            ]}
          />
          <View
            style={[
              styles.barSegment,
              { flex: getFlex(docsData.size), backgroundColor: '#fca311' },
            ]}
          />
          <View
            style={[
              styles.barSegment,
              {
                flex: totalSize === 0 ? 100 : 0,
                backgroundColor: isDarkMode ? '#48484a' : '#d1d1d6',
              },
            ]}
          />
        </View>

        <LegendItem
          color="#ff4d6d"
          label={`Images (${imagesData.count})`}
          value={formatBytes(imagesData.size)}
          textColor={isDarkMode ? '#e5e5ea' : '#3a3a3c'}
          chevronColor={isDarkMode ? '#8e8e93' : '#c7c7cc'}
        />
        <LegendItem
          color="#7b2cbf"
          label={`Videos (${videosData.count})`}
          value={formatBytes(videosData.size)}
          textColor={isDarkMode ? '#e5e5ea' : '#3a3a3c'}
          chevronColor={isDarkMode ? '#8e8e93' : '#c7c7cc'}
        />
        <LegendItem
          color="#00b4d8"
          label={`Audio files (${audioData.count})`}
          value={formatBytes(audioData.size)}
          textColor={isDarkMode ? '#e5e5ea' : '#3a3a3c'}
          chevronColor={isDarkMode ? '#8e8e93' : '#c7c7cc'}
        />
        <LegendItem
          color="#fca311"
          label={`Documents (${docsData.count})`}
          value={formatBytes(docsData.size)}
          textColor={isDarkMode ? '#e5e5ea' : '#3a3a3c'}
          chevronColor={isDarkMode ? '#8e8e93' : '#c7c7cc'}
        />
      </View>

      <View style={[styles.settingRow, { marginTop: 30 }]}>
        <Text style={[styles.settingText, { color: colors.text }]}>Dark Mode</Text>
        <Switch
          value={isDarkMode}
          onValueChange={toggleDarkMode}
          trackColor={{ false: '#767577', true: '#34c759' }}
          thumbColor={'#ffffff'}
        />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    paddingTop: 60,
    paddingHorizontal: 20,
  },
  iconButton: {
    width: 40,
    height: 40,
    borderRadius: 20,
    justifyContent: 'center',
    alignItems: 'center',
    marginBottom: 20,
  },
  title: {
    fontSize: 34,
    fontWeight: 'bold',
    marginBottom: 30,
  },
  indexStatusRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 20,
  },
  percentageText: {
    fontSize: 32,
    fontWeight: 'bold',
  },
  indexedLabel: {
    fontSize: 16,
    fontWeight: 'normal',
    opacity: 0.8,
  },
  startButton: {
    backgroundColor: '#3a3a3c',
    paddingVertical: 12,
    paddingHorizontal: 20,
    borderRadius: 8,
  },
  startButtonText: {
    color: '#ffffff',
    fontSize: 16,
    fontWeight: '500',
  },
  card: {
    borderRadius: 16,
    padding: 20,
    marginBottom: 20,
  },
  stackedBarContainer: {
    flexDirection: 'row',
    height: 16,
    marginBottom: 20,
    borderRadius: 8,
    overflow: 'hidden',
  },
  barSegment: {
    height: '100%',
  },
  legendRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingVertical: 8,
  },
  legendLeft: {
    flexDirection: 'row',
    alignItems: 'center',
  },
  dot: {
    width: 8,
    height: 8,
    borderRadius: 4,
    marginRight: 10,
  },
  legendLabel: {
    fontSize: 14,
    fontWeight: '500',
  },
  legendRight: {
    flexDirection: 'row',
    alignItems: 'center',
  },
  legendValue: {
    fontSize: 14,
    marginRight: 5,
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
