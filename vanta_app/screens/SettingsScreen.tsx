import React, { useState } from 'react';
import { View, Text, StyleSheet, Switch, TouchableOpacity, ActivityIndicator, PermissionsAndroid, Platform } from 'react-native';
import { useTheme } from '../ThemeContext';
import { useNavigation } from '@react-navigation/native';
import { Ionicons } from '@expo/vector-icons';
import { startStoring, generateEmbeddings, getIndexProgress, getDatabaseStats, pauseEmbeddings } from '../modules/vanta-bridge';

export default function SettingsScreen() {
  const { isDarkMode, toggleDarkMode, colors } = useTheme();
  const navigation = useNavigation<any>();
  const [isIndexing, setIsIndexing] = useState(false);
  const [indexProgress, setIndexProgress] = useState(0);
  const [dbStats, setDbStats] = useState<any>({});

  React.useEffect(() => {
    fetchStats();
  }, []);

  const fetchStats = async () => {
    try {
      const statsJson = await getDatabaseStats();
      console.log("RAW DB STATS FROM C++:", statsJson);
      setDbStats(JSON.parse(statsJson));
    } catch (e) {
      console.error(e);
    }
  };

  const handleStartIndexing = async () => {
    if (Platform.OS === 'android') {
      try {
        const permissions = await PermissionsAndroid.requestMultiple([
          PermissionsAndroid.PERMISSIONS.READ_MEDIA_IMAGES,
          PermissionsAndroid.PERMISSIONS.READ_MEDIA_VIDEO,
          PermissionsAndroid.PERMISSIONS.READ_MEDIA_AUDIO,
        ]);
        if (
          permissions[PermissionsAndroid.PERMISSIONS.READ_MEDIA_IMAGES] !== PermissionsAndroid.RESULTS.GRANTED &&
          permissions[PermissionsAndroid.PERMISSIONS.READ_MEDIA_VIDEO] !== PermissionsAndroid.RESULTS.GRANTED &&
          permissions[PermissionsAndroid.PERMISSIONS.READ_MEDIA_AUDIO] !== PermissionsAndroid.RESULTS.GRANTED
        ) {
          console.warn("Permissions not granted");
          // Continue anyway, maybe it's Android 12 or below
        }
      } catch (err) {
        console.warn(err);
      }
    }

    setIsIndexing(true);
    let intervalId: NodeJS.Timeout;

    try {
      console.log('Starting scan...');
      const scanResult = await startStoring();
      console.log('Scan result:', scanResult);
      
      // Start polling
      let lastStatus = "";
      intervalId = setInterval(async () => {
        try {
          const progressString = await getIndexProgress();
          if (progressString) {
            const data = JSON.parse(progressString);
            
            if (data.status !== lastStatus) {
              if (data.status === 'loading_models') {
                console.log("Extracting and loading AI models (this may take a few seconds)...");
              } else if (data.status === 'processing') {
                console.log("Models loaded successfully. Starting embedding generation...");
              } else if (data.status === 'finished') {
                console.log("Finished embedding generation.");
              }
              lastStatus = data.status;
            }

            if (data.total > 0 && data.status === 'processing') {
              const percent = Math.floor((data.processed / data.total) * 100);
              setIndexProgress(percent);
              console.log(`[${data.processed}/${data.total}] Embedding: ${data.currentFile}`);
            }
          }
        } catch (err) {
          // Ignore JSON parse errors if any
        }
      }, 500);

      console.log('Generating embeddings...');
      const success = await generateEmbeddings();
      if (!success) {
        alert("Failed to load AI Models! Did you adb push them?");
      }
      console.log('Embeddings generation finished.');
      setIndexProgress(100);
    } catch (e) {
      console.error(e);
    } finally {
      setIsIndexing(false);
      if (intervalId) clearInterval(intervalId);
      fetchStats();
    }
  };

  const handlePauseIndexing = async () => {
    try {
      await pauseEmbeddings();
    } catch (e) {
      console.error("Failed to pause", e);
    }
  };

  const LegendItem = ({ color, label, value }: { color: string, label: string, value: string }) => (
    <View style={styles.legendRow}>
      <View style={styles.legendLeft}>
        <View style={[styles.dot, { backgroundColor: color }]} />
        <Text style={[styles.legendLabel, { color: isDarkMode ? '#e5e5ea' : '#3a3a3c' }]}>{label}</Text>
      </View>
      <View style={styles.legendRight}>
        <Text style={[styles.legendValue, { color: isDarkMode ? '#e5e5ea' : '#3a3a3c' }]}>{value}</Text>
        <Ionicons name="chevron-forward" size={16} color={isDarkMode ? '#8e8e93' : '#c7c7cc'} />
      </View>
    </View>
  );

  const formatBytes = (bytes: number) => {
    if (!bytes || bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const imagesData = dbStats['image'] || dbStats['picture'] || { count: 0, size: 0 };
  const videosData = dbStats['video'] || { count: 0, size: 0 };
  const audioData = dbStats['audio'] || { count: 0, size: 0 };
  const docsData = dbStats['document'] || { count: 0, size: 0 };

  const totalSize = imagesData.size + videosData.size + audioData.size + docsData.size;
  const getFlex = (size: number) => totalSize > 0 ? (size / totalSize) * 100 : 0;

  return (
    <View style={[styles.container, { backgroundColor: colors.background }]}>
      <TouchableOpacity onPress={() => navigation.goBack()} style={[styles.iconButton, { backgroundColor: colors.iconBackground }]}>
        <Ionicons name="chevron-back" size={24} color={colors.iconColor} />
      </TouchableOpacity>
      <Text style={[styles.title, { color: colors.text }]}>Settings</Text>

      <View style={styles.indexStatusRow}>
        <Text style={[styles.percentageText, { color: colors.text }]}>{indexProgress}% <Text style={[styles.indexedLabel, { color: colors.text }]}>indexed</Text></Text>
        <TouchableOpacity 
          style={[styles.startButton, isIndexing ? { backgroundColor: '#ff9500' } : {}]} 
          onPress={isIndexing ? handlePauseIndexing : handleStartIndexing}
        >
          {isIndexing ? (
            <View style={{ flexDirection: 'row', alignItems: 'center' }}>
              <ActivityIndicator color="#fff" size="small" style={{ marginRight: 8 }} />
              <Text style={styles.startButtonText}>Pause</Text>
            </View>
          ) : (
            <Text style={styles.startButtonText}>{indexProgress > 0 && indexProgress < 100 ? "Resume" : "Start Indexing"}</Text>
          )}
        </TouchableOpacity>
      </View>

      <View style={[styles.card, { backgroundColor: isDarkMode ? '#3a3a3c' : '#f2f2f7' }]}>
        <View style={styles.stackedBarContainer}>
          <View style={[styles.barSegment, { flex: getFlex(imagesData.size) || 1, backgroundColor: '#ff4d6d', borderTopLeftRadius: 8, borderBottomLeftRadius: 8 }]} />
          <View style={[styles.barSegment, { flex: getFlex(videosData.size), backgroundColor: '#7b2cbf' }]} />
          <View style={[styles.barSegment, { flex: getFlex(audioData.size), backgroundColor: '#00b4d8' }]} />
          <View style={[styles.barSegment, { flex: getFlex(docsData.size), backgroundColor: '#fca311' }]} />
          <View style={[styles.barSegment, { flex: totalSize === 0 ? 100 : 0, backgroundColor: isDarkMode ? '#48484a' : '#d1d1d6', borderTopRightRadius: 8, borderBottomRightRadius: 8 }]} />
        </View>

        <LegendItem color="#ff4d6d" label={`Images (${imagesData.count})`} value={formatBytes(imagesData.size)} />
        <LegendItem color="#7b2cbf" label={`Videos (${videosData.count})`} value={formatBytes(videosData.size)} />
        <LegendItem color="#00b4d8" label={`Audio files (${audioData.count})`} value={formatBytes(audioData.size)} />
        <LegendItem color="#fca311" label={`Documents (${docsData.count})`} value={formatBytes(docsData.size)} />
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
