/**
 * Full-screen, swipeable viewer for a list of media files, plus the share
 * helper used by every screen that shows media.
 */
import React, { useEffect, useState } from 'react';
import {
  Alert,
  FlatList,
  Image,
  Modal,
  Pressable,
  StyleSheet,
  Text,
  View,
  useWindowDimensions,
} from 'react-native';
import { Ionicons } from '@expo/vector-icons';
import { openFile, shareFiles } from './modules/vanta-bridge';

export const isVideo = (path: string) => /\.(mp4|webm|mov|avi|mkv)$/i.test(path);

/** Opens the system share sheet; tells the user if nothing could be shared. */
export async function share(paths: string[]): Promise<boolean> {
  const ok = await shareFiles(paths).catch(() => false);
  if (!ok) Alert.alert('Share failed', 'These files could not be shared.');
  return ok;
}

/**
 * `index` is the item to open on, or null while the viewer is closed.
 * Images are shown in place; videos show their thumbnail with a play button
 * that hands the file to the device's video player.
 */
export function MediaViewer({
  paths,
  index,
  onClose,
}: {
  paths: string[];
  index: number | null;
  onClose: () => void;
}) {
  const { width, height } = useWindowDimensions();
  const [current, setCurrent] = useState(0);

  useEffect(() => {
    if (index !== null) setCurrent(index);
  }, [index]);

  return (
    <Modal
      visible={index !== null}
      animationType="fade"
      statusBarTranslucent
      onRequestClose={onClose}
    >
      <View style={styles.backdrop}>
        {index !== null && (
          <FlatList
            horizontal
            pagingEnabled
            data={paths}
            keyExtractor={(path, i) => `${i}:${path}`}
            initialScrollIndex={index}
            getItemLayout={(_, i) => ({ length: width, offset: width * i, index: i })}
            initialNumToRender={1}
            windowSize={3}
            showsHorizontalScrollIndicator={false}
            onMomentumScrollEnd={(e) =>
              setCurrent(Math.round(e.nativeEvent.contentOffset.x / width))
            }
            renderItem={({ item }) => (
              <View style={{ width, height, justifyContent: 'center', alignItems: 'center' }}>
                <Image
                  source={{ uri: 'file://' + item }}
                  style={StyleSheet.absoluteFill}
                  resizeMode="contain"
                />
                {isVideo(item) && (
                  <Pressable
                    accessibilityLabel="Play video"
                    style={styles.play}
                    onPress={async () => {
                      const ok = await openFile(item).catch(() => false);
                      if (!ok) Alert.alert('Cannot play', 'No app could open this video.');
                    }}
                  >
                    <Ionicons name="play" size={36} color="#ffffff" />
                  </Pressable>
                )}
              </View>
            )}
          />
        )}

        <View style={styles.topBar}>
          <Pressable accessibilityLabel="Close" style={styles.button} onPress={onClose}>
            <Ionicons name="close" size={24} color="#ffffff" />
          </Pressable>
          <Text style={styles.counter}>
            {current + 1} / {paths.length}
          </Text>
          <Pressable
            accessibilityLabel="Share"
            style={styles.button}
            onPress={() => paths[current] && share([paths[current]])}
          >
            <Ionicons name="share-social" size={22} color="#ffffff" />
          </Pressable>
        </View>
      </View>
    </Modal>
  );
}

const styles = StyleSheet.create({
  backdrop: { flex: 1, backgroundColor: '#000000' },
  topBar: {
    position: 'absolute',
    top: 50,
    left: 20,
    right: 20,
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  button: {
    width: 44,
    height: 44,
    borderRadius: 22,
    justifyContent: 'center',
    alignItems: 'center',
    backgroundColor: 'rgba(0,0,0,0.6)',
  },
  counter: { color: '#ffffff', fontSize: 16, fontWeight: '600' },
  play: {
    width: 72,
    height: 72,
    borderRadius: 36,
    justifyContent: 'center',
    alignItems: 'center',
    backgroundColor: 'rgba(0,0,0,0.6)',
  },
});
