import React, { useEffect, useState } from 'react';
import {
  View,
  Text,
  FlatList,
  ActivityIndicator,
  Image,
  StyleSheet,
  ScrollView,
  Pressable,
  Alert,
} from 'react-native';
import VantaEngine from '../modules/vanta-bridge/src/VantaEngineModule';
import {
  EntityMeta,
  EntityFile,
  NeighborResult,
} from '../modules/vanta-bridge/src/VantaEngine.types';
import { useTheme } from '../ThemeContext';

interface RouteProps {
  params: {
    entity: EntityMeta;
  };
}

/**
 * Screen that shows all photos for a face entity and the people who often
 * appear alongside them.
 *
 * The entity is normally passed via React Navigation route params. A fallback
 * mock entity is provided so the screen can be rendered in isolation (e.g. in
 * Jest smoke tests) without crashing.
 */
export const EntityDetailScreen = ({ route }: { route?: RouteProps }) => {
  const { entity } = route?.params ?? {
    entity: {
      entity_id: -1,
      display_name: 'Unknown',
      sample_count: 0,
      confidence: 0,
    } as EntityMeta,
  };
  const { colors } = useTheme();

  const [files, setFiles] = useState<EntityFile[]>([]);
  const [neighbors, setNeighbors] = useState<NeighborResult[]>([]);
  const [loading, setLoading] = useState(true);
  const [ownerEntityId, setOwnerEntityId] = useState<number>(-1);

  useEffect(() => {
    let mounted = true;

    const fetchDetails = async () => {
      try {
        // Load owner entity ID from native SharedPreferences
        const ownerId = await VantaEngine.getOwnerEntityId();
        if (mounted && ownerId > 0) {
          setOwnerEntityId(ownerId);
        }

        const [filesJson, neighborsJson] = await Promise.all([
          VantaEngine.getEntityFiles(entity.entity_id),
          VantaEngine.getEntityNeighbors(entity.entity_id),
        ]);

        if (!mounted) return;

        const parsedFiles = JSON.parse(filesJson) as EntityFile[];
        const parsedNeighbors = JSON.parse(neighborsJson) as NeighborResult[];

        setFiles(parsedFiles);
        setNeighbors(parsedNeighbors);
      } catch (error) {
        console.error('Failed to load entity details:', error);
      } finally {
        if (mounted) setLoading(false);
      }
    };

    fetchDetails();
    return () => {
      mounted = false;
    };
  }, [entity.entity_id]);

  const handleSetOwner = async () => {
    try {
      await VantaEngine.setOwnerEntityId(entity.entity_id);
      setOwnerEntityId(entity.entity_id);
    } catch (error) {
      console.error('Failed to set owner entity:', error);
      Alert.alert('Error', 'Could not set owner entity.');
    }
  };

  if (loading) {
    return (
      <View style={[styles.centerContainer, { backgroundColor: colors.background }]}>
        <ActivityIndicator size="large" color={colors.primary} />
      </View>
    );
  }

  const displayName =
    entity.display_name && entity.display_name.trim() !== '' ? entity.display_name : 'Unnamed';
  const isOwner = ownerEntityId === entity.entity_id;

  const renderFileItem = ({ item }: { item: EntityFile }) => (
    <Image
      source={{ uri: `file://${item.abs_path}` }}
      style={styles.photoItem}
      resizeMode="cover"
    />
  );

  return (
    <ScrollView style={[styles.container, { backgroundColor: colors.background }]}>
      <View style={styles.titleRow}>
        <Text style={[styles.title, { color: colors.text }]}>{displayName}</Text>
        {isOwner && (
          <View style={[styles.ownerBadge, { backgroundColor: colors.primary + '22' }]}>
            <Text style={[styles.ownerBadgeText, { color: colors.primary }]}>★ You</Text>
          </View>
        )}
      </View>

      {!isOwner && (
        <Pressable
          style={[styles.thisIsMeButton, { backgroundColor: colors.primary }]}
          onPress={handleSetOwner}
        >
          <Text style={styles.thisIsMeText}>This is me</Text>
        </Pressable>
      )}

      <Text style={[styles.sectionHeader, { color: colors.text }]}>Photos</Text>
      <FlatList
        horizontal
        data={files}
        keyExtractor={(item) => item.file_id.toString()}
        renderItem={renderFileItem}
        contentContainerStyle={styles.horizontalList}
        showsHorizontalScrollIndicator={false}
      />

      <Text style={[styles.sectionHeader, { color: colors.text }]}>People</Text>
      <View style={styles.neighborsContainer}>
        {neighbors.map((neighbor) => {
          const neighborName =
            neighbor.display_name && neighbor.display_name.trim() !== ''
              ? neighbor.display_name
              : 'Unnamed';
          return (
            <View
              key={neighbor.neighbor_id}
              style={[styles.neighborRow, { borderBottomColor: colors.border }]}
            >
              <Text style={[styles.neighborName, { color: colors.text }]}>{neighborName}</Text>
              <Text style={[styles.neighborCount, { color: colors.text + '99' }]}>
                {neighbor.co_occurrence_count} photos together
              </Text>
            </View>
          );
        })}
        {neighbors.length === 0 && (
          <Text style={[styles.emptyText, { color: colors.text + '99' }]}>No people found</Text>
        )}
      </View>
    </ScrollView>
  );
};

const styles = StyleSheet.create({
  container: {
    flex: 1,
  },
  centerContainer: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
  },
  titleRow: {
    flexDirection: 'row',
    alignItems: 'center',
    marginHorizontal: 16,
    marginVertical: 16,
    gap: 10,
  },
  title: {
    fontSize: 34,
    fontWeight: 'bold',
  },
  ownerBadge: {
    paddingHorizontal: 10,
    paddingVertical: 4,
    borderRadius: 12,
  },
  ownerBadgeText: {
    fontSize: 14,
    fontWeight: '600',
  },
  thisIsMeButton: {
    marginHorizontal: 16,
    marginBottom: 8,
    paddingVertical: 10,
    borderRadius: 8,
    alignItems: 'center',
  },
  thisIsMeText: {
    color: '#FFFFFF',
    fontSize: 16,
    fontWeight: '600',
  },
  sectionHeader: {
    fontSize: 22,
    fontWeight: '600',
    marginHorizontal: 16,
    marginTop: 24,
    marginBottom: 12,
  },
  horizontalList: {
    paddingHorizontal: 16,
  },
  photoItem: {
    width: 120,
    height: 120,
    borderRadius: 8,
    marginRight: 12,
  },
  neighborsContainer: {
    paddingHorizontal: 16,
    paddingBottom: 40,
  },
  neighborRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingVertical: 12,
    borderBottomWidth: StyleSheet.hairlineWidth,
  },
  neighborName: {
    fontSize: 16,
    fontWeight: '500',
  },
  neighborCount: {
    fontSize: 14,
  },
  emptyText: {
    fontSize: 14,
    fontStyle: 'italic',
    marginTop: 8,
  },
});

