import React, { useState, useEffect } from 'react';
import {
  StyleSheet,
  Text,
  View,
  Button,
  FlatList,
  ActivityIndicator,
  Alert,
  PermissionsAndroid,
  Platform
} from 'react-native';
import * as VantaEngine from '../modules/vanta-bridge';
import { useTheme } from '../ThemeContext';

interface FileEntry {
  uri: string;
  type: string;
  mime: string;
  size: number;
}

export default function DebugScreen() {
  const { colors } = useTheme();
  const [files, setFiles] = useState<FileEntry[]>([]);
  const [loading, setLoading] = useState(false);
  const [stats, setStats] = useState<any>(null);

  const requestMediaPermissions = async () => {
    if (Platform.OS !== 'android') {
      return true;
    }

    const permissions = await PermissionsAndroid.requestMultiple([
      PermissionsAndroid.PERMISSIONS.READ_MEDIA_IMAGES,
      PermissionsAndroid.PERMISSIONS.READ_MEDIA_VIDEO,
      PermissionsAndroid.PERMISSIONS.READ_MEDIA_AUDIO,
    ]);

    return (
      permissions[PermissionsAndroid.PERMISSIONS.READ_MEDIA_IMAGES] === PermissionsAndroid.RESULTS.GRANTED &&
      permissions[PermissionsAndroid.PERMISSIONS.READ_MEDIA_VIDEO] === PermissionsAndroid.RESULTS.GRANTED &&
      permissions[PermissionsAndroid.PERMISSIONS.READ_MEDIA_AUDIO] === PermissionsAndroid.RESULTS.GRANTED
    );
  };

  const refreshFiles = async () => {
    try {
      const data = await VantaEngine.getStoredFiles();
      setFiles(data);
    } catch (e) {
      console.error(e);
    }
  };

  useEffect(() => {
    refreshFiles();
  }, []);

  const handleStartScan = async () => {
    const granted = await requestMediaPermissions();

    if (!granted) {
      Alert.alert(
        "Permission Required",
        "Please grant Photos, Videos and Audio permissions."
      );
      return;
    }

    setLoading(true);

    try {
      const result = await VantaEngine.startStoring();
      setStats(result);
      Alert.alert("Scan Complete", `Stored ${result.scannedFileCount} files.`);
      await refreshFiles();
    } catch (e: any) {
      Alert.alert("Error", e.message);
    } finally {
      setLoading(false);
    }
  };

  return (
    <View style={[styles.container, { backgroundColor: colors.background }]}>
      <Text style={[styles.title, { color: colors.text }]}>Vanta Engine DB Test</Text>

      <View style={styles.buttonContainer}>
        <Button
          title={loading ? "Scanning..." : "Start Media Scan & Store"}
          onPress={handleStartScan}
          disabled={loading}
          color="#0066cc"
        />
      </View>

      {stats && (
        <View style={[styles.statsBox, { backgroundColor: colors.surface }]}>
          <Text style={{ color: colors.text }}>Database Path: {stats.databasePath.split('/').pop()}</Text>
          <Text style={{ color: colors.text }}>Files Scanned: {stats.scannedFileCount}</Text>
          <Text style={{ color: colors.text }}>Success: {stats.success ? "Yes" : "No"}</Text>
        </View>
      )}

      <Text style={[styles.subtitle, { color: colors.text }]}>Latest 10 DB Entries:</Text>

      {loading ? (
        <ActivityIndicator size="large" color="#0000ff" />
      ) : (
        <FlatList
          data={files}
          keyExtractor={(item) => item.uri}
          renderItem={({ item }) => (
            <View style={[styles.fileItem, { backgroundColor: colors.surface, borderColor: colors.border }]}>
              <Text style={[styles.uriText, { color: colors.text }]} numberOfLines={2}>{item.uri}</Text>
              <Text style={styles.detailsText}>{item.type} | {item.mime} | {(item.size / 1024).toFixed(1)} KB</Text>
            </View>
          )}
          ListEmptyComponent={<Text style={styles.emptyText}>No files stored in DB yet.</Text>}
          style={styles.list}
        />
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    paddingTop: 20,
    paddingHorizontal: 20,
  },
  title: {
    fontSize: 22,
    fontWeight: 'bold',
    textAlign: 'center',
    marginBottom: 20,
  },
  subtitle: {
    fontSize: 16,
    fontWeight: '600',
    marginTop: 20,
    marginBottom: 10,
  },
  buttonContainer: {
    marginBottom: 10,
  },
  statsBox: {
    padding: 10,
    borderRadius: 5,
    marginBottom: 10,
  },
  list: {
    flex: 1,
  },
  fileItem: {
    padding: 12,
    marginVertical: 4,
    borderRadius: 8,
    borderWidth: 1,
  },
  uriText: {
    fontSize: 14,
    fontWeight: 'bold',
  },
  detailsText: {
    fontSize: 12,
    color: '#888',
    marginTop: 4,
  },
  emptyText: {
    textAlign: 'center',
    marginTop: 40,
    color: '#999',
  }
});
