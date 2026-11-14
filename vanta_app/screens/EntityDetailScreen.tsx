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
  Dimensions,
} from 'react-native';
import VantaEngine from '../modules/vanta-bridge/src/VantaEngineModule';
import {
  EntityMeta,
  EntityFile,
  NeighborResult,
  FaceCrop,
} from '../modules/vanta-bridge/src/VantaEngine.types';
import { useTheme } from '../ThemeContext';
import { LinearGradient } from 'expo-linear-gradient';
import { BlurView } from 'expo-blur';
import { Ionicons } from '@expo/vector-icons';

const { width } = Dimensions.get('window');

interface RouteProps {
  params: {
    entity: EntityMeta;
  };
}

/**
 * NeighborRow
 * A specialized sub-component that represents a single "neighbor" or associated person
 * who frequently appears in photos alongside the main entity. It lazily loads the
 * neighbor's face crop image to avoid bogging down the initial render.
 */
const NeighborRow = ({ neighbor, colors }: { neighbor: NeighborResult; colors: any }) => {
  const [crop, setCrop] = useState<FaceCrop | null>(null);

  useEffect(() => {
    let mounted = true;
    const fetchCrop = async () => {
      try {
        const cropJson = await VantaEngine.getBestFaceCrop(neighbor.neighbor_id);
        if (cropJson && cropJson !== '{}') {
          const parsedCrop = JSON.parse(cropJson) as FaceCrop;
          if (parsedCrop.file_id !== -1 && mounted) {
            setCrop(parsedCrop);
          }
        }
      } catch (error) {
        // Ignore fetch errors for neighbors
      }
    };
    fetchCrop();
    return () => { mounted = false; };
  }, [neighbor.neighbor_id]);

  const neighborName = neighbor.display_name && neighbor.display_name.trim() !== '' ? neighbor.display_name : 'Unnamed';

  return (
    <View style={[styles.neighborRow, { backgroundColor: colors.surface }]}>
      <View style={[styles.neighborIcon, { backgroundColor: colors.primary + '20', overflow: 'hidden' }]}>
        {crop && crop.aligned_crop_path ? (
          <Image source={{ uri: `file://${crop.aligned_crop_path}` }} style={{ width: '100%', height: '100%' }} />
        ) : (
          <Ionicons name="person" size={20} color={colors.primary} />
        )}
      </View>
      <View style={{ flex: 1 }}>
        <Text style={[styles.neighborName, { color: colors.text }]}>{neighborName}</Text>
        <Text style={[styles.neighborCount, { color: colors.text + '80' }]}>
          {neighbor.co_occurrence_count} shared memories
        </Text>
      </View>
      <Ionicons name="chevron-forward" size={20} color={colors.text + '40'} />
    </View>
  );
};

/**
 * EntityDetailScreen
 * A detailed view for a specific person/entity. It fetches their metadata (name, relation, age, location),
 * the top photos they appear in, and other people they are frequently seen with. 
 * Provides an interface to edit this metadata or mark the person as the "Owner" of the device.
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
  const { colors, isDarkMode } = useTheme();

  const [files, setFiles] = useState<EntityFile[]>([]);
  const [neighbors, setNeighbors] = useState<NeighborResult[]>([]);
  const [loading, setLoading] = useState(true);
  const [ownerEntityId, setOwnerEntityId] = useState<number>(-1);
  const [bestCrop, setBestCrop] = useState<FaceCrop | null>(null);

  // Metadata
  const [relation, setRelation] = useState('');
  const [age, setAge] = useState<number>(0);
  const [location, setLocation] = useState('');
  
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
        const ownerId = await VantaEngine.getOwnerEntityId();
        if (mounted && ownerId > 0) setOwnerEntityId(ownerId);

        const [filesJson, neighborsJson, cropJson, metaJson] = await Promise.all([
          VantaEngine.getEntityFiles(entity.entity_id),
          VantaEngine.getEntityNeighbors(entity.entity_id),
          VantaEngine.getBestFaceCrop(entity.entity_id),
          VantaEngine.getEntityMetadata(entity.entity_id)
        ]);

        if (!mounted) return;

        setFiles(JSON.parse(filesJson) as EntityFile[]);
        setNeighbors(JSON.parse(neighborsJson) as NeighborResult[]);
        
        if (cropJson && cropJson !== '{}') {
          const parsedCrop = JSON.parse(cropJson) as FaceCrop;
          if (parsedCrop.file_id !== -1) setBestCrop(parsedCrop);
        }

        if (metaJson && metaJson !== '{}') {
          const meta = JSON.parse(metaJson);
          if (meta.relation) setRelation(meta.relation);
          if (meta.age) setAge(meta.age);
          if (meta.location) setLocation(meta.location);
          setRelationValue(meta.relation || '');
          setAgeValue(meta.age ? meta.age.toString() : '');
          setLocationValue(meta.location || '');
        }

      } catch (error) {
        console.error('Failed to load entity details:', error);
      } finally {
        if (mounted) setLoading(false);
      }
    };

    fetchDetails();
    return () => { mounted = false; };
  }, [entity.entity_id]);

  const handleSetOwner = async () => {
    try {
      await VantaEngine.setOwnerEntityId(entity.entity_id);
      setOwnerEntityId(entity.entity_id);
      Alert.alert('Success', 'This person is now set as the device owner.');
    } catch (error) {
      Alert.alert('Error', 'Failed to set owner.');
    }
  };

  const handleConfirmName = async () => {
    if (inputValue.trim() === '') return;
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
        setRelation(relationValue.trim());
        setAge(isNaN(ageNum) ? 0 : ageNum);
        setLocation(locationValue.trim());
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
    <View style={styles.photoContainer}>
      <Image
        source={{ uri: `file://${item.abs_path}` }}
        style={styles.photoItem}
        resizeMode="cover"
      />
    </View>
  );

  const displayName = currentName && currentName.trim() !== '' ? currentName : 'Unnamed';
  const isOwner = ownerEntityId === entity.entity_id;

  const headerImageUrl = bestCrop && bestCrop.aligned_crop_path 
    ? `file://${bestCrop.aligned_crop_path}` 
    : files.length > 0 ? `file://${files[0].abs_path}` : null;

  return (
    <View style={[styles.container, { backgroundColor: colors.background }]}>
      <ScrollView bounces={false} contentContainerStyle={{ paddingBottom: 40 }} showsVerticalScrollIndicator={false}>
        
        {/* HERO SECTION */}
        <View style={styles.heroContainer}>
          {headerImageUrl && (
            <Image source={{ uri: headerImageUrl }} style={StyleSheet.absoluteFill} blurRadius={40} />
          )}
          <View style={[StyleSheet.absoluteFill, { backgroundColor: isDarkMode ? 'rgba(0,0,0,0.6)' : 'rgba(255,255,255,0.4)' }]} />
          
          <LinearGradient
            colors={['transparent', colors.background]}
            style={StyleSheet.absoluteFill}
            locations={[0.5, 1]}
          />

          <View style={styles.heroContent}>
            <View style={styles.avatarWrapper}>
              {headerImageUrl ? (
                <Image source={{ uri: headerImageUrl }} style={styles.avatarImage} />
              ) : (
                <View style={[styles.avatarImage, { backgroundColor: colors.surface, justifyContent: 'center', alignItems: 'center' }]}>
                  <Ionicons name="person" size={50} color={colors.text + '50'} />
                </View>
              )}
            </View>

            <BlurView intensity={isDarkMode ? 40 : 80} tint={isDarkMode ? 'dark' : 'light'} style={styles.glassCard}>
              <View style={styles.glassCardHeader}>
                <Text style={[styles.title, { color: colors.text }]} numberOfLines={1}>{displayName}</Text>
                <Pressable onPress={() => setModalVisible(true)} style={[styles.editButton, { backgroundColor: '#ffffff' }]}>
                  <Ionicons name="pencil" size={16} color="#000000" />
                </Pressable>
              </View>

              <View style={styles.badgesRow}>
                {isOwner && (
                  <View style={[styles.badge, { backgroundColor: '#ffcc0030', borderColor: '#ffcc0050', borderWidth: 1 }]}>
                    <Ionicons name="star" size={14} color="#ffcc00" />
                    <Text style={[styles.badgeText, { color: '#ffcc00' }]}> You</Text>
                  </View>
                )}
                {relation !== '' && (
                  <View style={[styles.badge, { backgroundColor: colors.text + '10' }]}>
                    <Ionicons name="people" size={14} color={colors.text + '80'} />
                    <Text style={[styles.badgeText, { color: colors.text }]}> {relation}</Text>
                  </View>
                )}
                {age > 0 && (
                  <View style={[styles.badge, { backgroundColor: colors.text + '10' }]}>
                    <Ionicons name="calendar" size={14} color={colors.text + '80'} />
                    <Text style={[styles.badgeText, { color: colors.text }]}> {age} yrs</Text>
                  </View>
                )}
                {location !== '' && (
                  <View style={[styles.badge, { backgroundColor: colors.text + '10' }]}>
                    <Ionicons name="location" size={14} color={colors.text + '80'} />
                    <Text style={[styles.badgeText, { color: colors.text }]}> {location}</Text>
                  </View>
                )}
              </View>
            </BlurView>
          </View>
        </View>

        {/* The "Mark as Me" Button */}
        {/* Allows the user to specify that this entity represents themselves. */}
        {!isOwner && (
          <Pressable onPress={handleSetOwner} style={[styles.thisIsMeContainer, { backgroundColor: '#ffffff', borderRadius: 30 }]}>
            <Ionicons name="star" size={20} color="#000000" style={{ marginRight: 8 }} />
            <Text style={[styles.thisIsMeText, { color: '#000000', fontWeight: 'bold' }]}>Mark as &quot;Me&quot;</Text>
          </Pressable>
        )}

        {/* Memory Gallery Section */}
        <View style={styles.sectionHeaderRow}>
          <Text style={[styles.sectionHeader, { color: colors.text }]}>Memories</Text>
          <Text style={{ color: colors.text + '70', fontSize: 14 }}>{files.length}</Text>
        </View>
        
        {files.length > 0 ? (
          <FlatList
            horizontal
            data={files}
            keyExtractor={(item) => item.file_id.toString()}
            renderItem={renderFileItem}
            contentContainerStyle={styles.horizontalList}
            showsHorizontalScrollIndicator={false}
            snapToInterval={220 + 16}
            decelerationRate="fast"
          />
        ) : (
          <View style={[styles.emptyBox, { backgroundColor: colors.surface }]}>
            <Ionicons name="image-outline" size={32} color={colors.text + '40'} />
            <Text style={{ color: colors.text + '60', marginTop: 8 }}>No memories found.</Text>
          </View>
        )}

        {/* Social Graph / Neighbors Section */}
        <Text style={[styles.sectionHeader, { color: colors.text, marginTop: 32 }]}>Often appears with</Text>
        <View style={styles.neighborsContainer}>
          {neighbors.map((neighbor) => (
            <NeighborRow key={neighbor.neighbor_id} neighbor={neighbor} colors={colors} />
          ))}
          {neighbors.length === 0 && (
            <View style={[styles.emptyBox, { backgroundColor: colors.surface }]}>
              <Ionicons name="people-outline" size={32} color={colors.text + '40'} />
              <Text style={{ color: colors.text + '60', marginTop: 8 }}>No network found.</Text>
            </View>
          )}
        </View>

        {/* EDIT MODAL */}
        <Modal visible={modalVisible} transparent={true} animationType="slide">
          <View style={styles.modalOverlay}>
            <View style={[styles.modalContent, { backgroundColor: colors.background }]}>
              <View style={styles.modalDragHandle} />
              <Text style={[styles.modalTitle, { color: colors.text }]}>Edit Details</Text>
              
              <Text style={[styles.inputLabel, { color: colors.text + '90' }]}>Display Name</Text>
              <TextInput
                style={[styles.input, { color: colors.text, backgroundColor: colors.text + '10' }]}
                placeholder="Required"
                placeholderTextColor={colors.text + '50'}
                value={inputValue}
                onChangeText={setInputValue}
                autoFocus
              />

              <Text style={[styles.inputLabel, { color: colors.text + '90' }]}>Relation</Text>
              <TextInput
                style={[styles.input, { color: colors.text, backgroundColor: colors.text + '10' }]}
                placeholder="e.g. father, friend, sister"
                placeholderTextColor={colors.text + '50'}
                value={relationValue}
                onChangeText={setRelationValue}
              />

              <View style={{ flexDirection: 'row', gap: 12 }}>
                <View style={{ flex: 1 }}>
                  <Text style={[styles.inputLabel, { color: colors.text + '90' }]}>Age</Text>
                  <TextInput
                    style={[styles.input, { color: colors.text, backgroundColor: colors.text + '10' }]}
                    placeholder="Optional"
                    placeholderTextColor={colors.text + '50'}
                    value={ageValue}
                    onChangeText={setAgeValue}
                    keyboardType="numeric"
                  />
                </View>
                <View style={{ flex: 2 }}>
                  <Text style={[styles.inputLabel, { color: colors.text + '90' }]}>Location</Text>
                  <TextInput
                    style={[styles.input, { color: colors.text, backgroundColor: colors.text + '10' }]}
                    placeholder="Optional"
                    placeholderTextColor={colors.text + '50'}
                    value={locationValue}
                    onChangeText={setLocationValue}
                  />
                </View>
              </View>

              <View style={styles.modalButtons}>
                <Pressable style={styles.modalButtonCancel} onPress={() => setModalVisible(false)}>
                  <Text style={{ color: colors.text, fontWeight: '600' }}>Cancel</Text>
                </Pressable>
                <Pressable style={[styles.modalButtonConfirm, { backgroundColor: '#ffffff', borderRadius: 30 }]} onPress={handleConfirmName}>
                  <Text style={{ color: '#000000', fontWeight: 'bold' }}>Save Changes</Text>
                </Pressable>
              </View>
            </View>
          </View>
        </Modal>
      </ScrollView>
    </View>
  );
};

const styles = StyleSheet.create({
  container: {
    flex: 1,
  },
  heroContainer: {
    height: 380,
    width: '100%',
    justifyContent: 'flex-end',
  },
  heroContent: {
    alignItems: 'center',
    paddingHorizontal: 20,
    paddingBottom: 20,
  },
  avatarWrapper: {
    width: 130,
    height: 130,
    borderRadius: 65,
    backgroundColor: '#fff',
    padding: 4,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 10 },
    shadowOpacity: 0.3,
    shadowRadius: 15,
    elevation: 10,
    marginBottom: 20,
    zIndex: 10,
  },
  avatarImage: {
    width: '100%',
    height: '100%',
    borderRadius: 65,
  },
  glassCard: {
    width: '100%',
    borderRadius: 24,
    padding: 20,
    overflow: 'hidden',
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.1)',
  },
  glassCardHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 12,
  },
  title: {
    fontSize: 28,
    fontWeight: '800',
    flex: 1,
    letterSpacing: -0.5,
  },
  editButton: {
    width: 36,
    height: 36,
    borderRadius: 18,
    justifyContent: 'center',
    alignItems: 'center',
  },
  badgesRow: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 8,
  },
  badge: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingHorizontal: 10,
    paddingVertical: 6,
    borderRadius: 12,
  },
  badgeText: {
    fontSize: 13,
    fontWeight: '600',
  },
  thisIsMeContainer: {
    marginHorizontal: 20,
    marginTop: 12,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    paddingVertical: 14,
    borderRadius: 8,
  },
  thisIsMeText: {
    fontSize: 16,
    fontWeight: '500',
  },
  sectionHeaderRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'baseline',
    marginHorizontal: 20,
    marginTop: 32,
    marginBottom: 16,
  },
  sectionHeader: {
    fontSize: 22,
    fontWeight: '700',
    letterSpacing: -0.5,
    marginHorizontal: 20,
    marginBottom: 16,
  },
  horizontalList: {
    paddingHorizontal: 20,
    paddingBottom: 20,
  },
  photoContainer: {
    width: 220,
    height: 300,
    marginRight: 16,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 8 },
    shadowOpacity: 0.15,
    shadowRadius: 12,
    elevation: 6,
    borderRadius: 24,
    backgroundColor: '#fff',
  },
  photoItem: {
    width: '100%',
    height: '100%',
    borderRadius: 24,
  },
  neighborsContainer: {
    paddingHorizontal: 20,
    gap: 12,
  },
  neighborRow: {
    flexDirection: 'row',
    alignItems: 'center',
    padding: 16,
    borderRadius: 20,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 4 },
    shadowOpacity: 0.05,
    shadowRadius: 8,
    elevation: 2,
  },
  neighborIcon: {
    width: 44,
    height: 44,
    borderRadius: 22,
    justifyContent: 'center',
    alignItems: 'center',
    marginRight: 16,
  },
  neighborName: {
    fontSize: 17,
    fontWeight: '700',
    marginBottom: 4,
  },
  neighborCount: {
    fontSize: 13,
  },
  emptyBox: {
    padding: 30,
    borderRadius: 20,
    alignItems: 'center',
    justifyContent: 'center',
    marginHorizontal: 20,
  },
  modalOverlay: {
    flex: 1,
    justifyContent: 'flex-end',
    backgroundColor: 'rgba(0,0,0,0.4)',
  },
  modalContent: {
    borderTopLeftRadius: 32,
    borderTopRightRadius: 32,
    padding: 24,
    paddingBottom: 40,
    minHeight: 400,
  },
  modalDragHandle: {
    width: 40,
    height: 5,
    backgroundColor: '#888',
    borderRadius: 3,
    alignSelf: 'center',
    marginBottom: 20,
    opacity: 0.5,
  },
  modalTitle: {
    fontSize: 24,
    fontWeight: '800',
    marginBottom: 24,
    letterSpacing: -0.5,
  },
  inputLabel: {
    fontSize: 13,
    fontWeight: '600',
    marginBottom: 8,
    marginLeft: 4,
    textTransform: 'uppercase',
    letterSpacing: 0.5,
  },
  input: {
    height: 52,
    borderRadius: 14,
    paddingHorizontal: 16,
    fontSize: 16,
    marginBottom: 20,
  },
  modalButtons: {
    flexDirection: 'row',
    gap: 12,
    marginTop: 10,
  },
  modalButtonCancel: {
    flex: 1,
    height: 56,
    borderRadius: 16,
    justifyContent: 'center',
    alignItems: 'center',
    backgroundColor: 'rgba(150,150,150,0.1)',
  },
  modalButtonConfirm: {
    flex: 2,
    height: 56,
    borderRadius: 16,
    justifyContent: 'center',
    alignItems: 'center',
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 4 },
    shadowOpacity: 0.2,
    shadowRadius: 8,
    elevation: 4,
  },
});
