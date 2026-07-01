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
  Modal,
  TextInput,
  Button,
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

  // Naming Modal State
  const [modalVisible, setModalVisible] = useState(false);
  const [inputValue, setInputValue] = useState(entity.display_name === 'Unnamed' ? '' : entity.display_name);
  const [relationValue, setRelationValue] = useState('');
  const [ageValue, setAgeValue] = useState('');
  const [locationValue, setLocationValue] = useState('');
  const [currentName, setCurrentName] = useState(entity.display_name);

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
    currentName && currentName.trim() !== '' ? currentName : 'Unnamed';
  const isOwner = ownerEntityId === entity.entity_id;

  const handleConfirmName = async () => {
    if (inputValue.trim() === '' || relationValue.trim() === '') return;
    try {
      const ageNum = ageValue.trim() !== '' ? parseInt(ageValue, 10) : 0;
      const success = await VantaEngine.setEntityMetadata(
        entity.entity_id,
        inputValue.trim(),
        relationValue.trim(),
        isNaN(ageNum) ? 0 : ageNum,
        locationValue.trim()
      );
      if (success) {
        setCurrentName(inputValue.trim());
        setModalVisible(false);
      } else {
        Alert.alert('Error', 'Failed to save details.');
      }
    } catch (error) {
      console.error('Error setting entity metadata', error);
      Alert.alert('Error', 'An error occurred.');
    }
  };

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
        <Pressable onPress={() => setModalVisible(true)} style={[styles.editButton, { backgroundColor: colors.surface }]}>
          <Text style={{ color: colors.primary, fontWeight: 'bold' }}>Edit</Text>
        </Pressable>
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

      <Modal visible={modalVisible} transparent={true} animationType="fade">
        <View style={styles.modalOverlay}>
          <View style={[styles.modalContent, { backgroundColor: colors.surface }]}>
            <Text style={[styles.modalTitle, { color: colors.text }]}>Name this person</Text>
            <TextInput
              style={[styles.input, { color: colors.text, borderColor: colors.border }]}
              placeholder="Name (required)"
              placeholderTextColor={colors.text + '80'}
              value={inputValue}
              onChangeText={setInputValue}
              autoFocus
            />
            <TextInput
              style={[styles.input, { color: colors.text, borderColor: colors.border }]}
              placeholder="Relation (e.g. father, friend, sister)"
              placeholderTextColor={colors.text + '80'}
              value={relationValue}
              onChangeText={setRelationValue}
            />
            <TextInput
              style={[styles.input, { color: colors.text, borderColor: colors.border }]}
              placeholder="Age (optional)"
              placeholderTextColor={colors.text + '80'}
              value={ageValue}
              onChangeText={setAgeValue}
              keyboardType="numeric"
            />
            <TextInput
              style={[styles.input, { color: colors.text, borderColor: colors.border }]}
              placeholder="Location (optional)"
              placeholderTextColor={colors.text + '80'}
              value={locationValue}
              onChangeText={setLocationValue}
            />
            <View style={styles.modalButtons}>
              <Button title="Cancel" color={colors.text} onPress={() => setModalVisible(false)} />
              <Button title="Confirm" color={colors.primary} onPress={handleConfirmName} />
            </View>
          </View>
        </View>
      </Modal>
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
  editButton: {
    paddingHorizontal: 12,
    paddingVertical: 6,
    borderRadius: 16,
    marginLeft: 'auto',
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
  modalOverlay: {
    flex: 1,
    backgroundColor: 'rgba(0,0,0,0.5)',
    justifyContent: 'center',
    alignItems: 'center',
  },
  modalContent: {
    width: 300,
    padding: 20,
    borderRadius: 12,
    alignItems: 'center',
  },
  modalTitle: {
    fontSize: 18,
    fontWeight: 'bold',
    marginBottom: 16,
  },
  input: {
    width: '100%',
    height: 44,
    borderWidth: 1,
    borderRadius: 8,
    paddingHorizontal: 12,
    marginTop: 16,
    marginBottom: 20,
  },
  modalButtons: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    width: '100%',
  },
});

