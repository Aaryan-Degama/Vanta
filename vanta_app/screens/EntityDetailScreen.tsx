import React, { useEffect, useState } from 'react';
import { View, Text, FlatList, ActivityIndicator, Image, StyleSheet, ScrollView } from 'react-native';
import VantaEngine from '../modules/vanta-bridge/src/VantaEngineModule';
import { EntityMeta, EntityFile, NeighborResult } from '../modules/vanta-bridge/src/VantaEngine.types';
import { useTheme } from '../ThemeContext';

interface RouteProps {
  params: {
    entity: EntityMeta;
  };
}

export const EntityDetailScreen = ({ route }: { route: RouteProps }) => {
  const { entity } = route.params;
  const { colors } = useTheme();
  
  const [files, setFiles] = useState<EntityFile[]>([]);
  const [neighbors, setNeighbors] = useState<NeighborResult[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let mounted = true;
    
    const fetchDetails = async () => {
      try {
        const [filesJson, neighborsJson] = await Promise.all([
          VantaEngine.getEntityFiles(entity.entity_id),
          VantaEngine.getEntityNeighbors(entity.entity_id)
        ]);
        
        if (!mounted) return;
        
        const parsedFiles = JSON.parse(filesJson) as EntityFile[];
        const parsedNeighbors = JSON.parse(neighborsJson) as NeighborResult[];
        
        setFiles(parsedFiles);
        setNeighbors(parsedNeighbors);
      } catch (error) {
        console.error("Failed to load entity details:", error);
      } finally {
        if (mounted) setLoading(false);
      }
    };

    fetchDetails();
    return () => { mounted = false; };
  }, [entity.entity_id]);

  if (loading) {
    return (
      <View style={[styles.centerContainer, { backgroundColor: colors.background }]}>
        <ActivityIndicator size="large" color={colors.primary} />
      </View>
    );
  }
  
  const displayName = entity.display_name && entity.display_name.trim() !== '' ? entity.display_name : 'Unnamed';

  const renderFileItem = ({ item }: { item: EntityFile }) => (
    <Image 
      source={{ uri: `file://${item.abs_path}` }} 
      style={styles.photoItem} 
      resizeMode="cover"
    />
  );

  return (
    <ScrollView style={[styles.container, { backgroundColor: colors.background }]}>
      <Text style={[styles.title, { color: colors.text }]}>{displayName}</Text>
      
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
          const neighborName = neighbor.display_name && neighbor.display_name.trim() !== '' ? neighbor.display_name : 'Unnamed';
          return (
            <View key={neighbor.neighbor_id} style={[styles.neighborRow, { borderBottomColor: colors.border }]}>
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
  title: {
    fontSize: 34,
    fontWeight: 'bold',
    marginHorizontal: 16,
    marginVertical: 16,
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
  }
});
