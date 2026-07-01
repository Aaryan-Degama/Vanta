import React, { useEffect, useState } from 'react';
import {
  View,
  Text,
  FlatList,
  ActivityIndicator,
  Pressable,
  StyleSheet,
  Dimensions,
  Image,
  Modal,
  TextInput,
  Button,
} from 'react-native';
import { useNavigation } from '@react-navigation/native';
import VantaEngine from '../modules/vanta-bridge/src/VantaEngineModule';
import { EntityMeta, FaceCrop } from '../modules/vanta-bridge/src/VantaEngine.types';
import { useTheme } from '../ThemeContext';

const SCREEN_WIDTH = Dimensions.get('window').width;
const NUM_COLUMNS = 3;
const CELL_WIDTH = SCREEN_WIDTH / NUM_COLUMNS;

const FaceCell = ({
  entity,
  colors,
  onNamed,
}: {
  entity: EntityMeta;
  colors: any;
  onNamed: (id: number, name: string) => void;
}) => {
  const [crop, setCrop] = useState<FaceCrop | null>(null);
  const [imgSize, setImgSize] = useState<{ width: number; height: number } | null>(null);

  useEffect(() => {
    let mounted = true;
    const fetchCrop = async () => {
      try {
        const jsonStr = await VantaEngine.getBestFaceCrop(entity.entity_id);
        if (jsonStr && jsonStr !== '{}') {
          const parsed = JSON.parse(jsonStr) as FaceCrop;
          if (parsed.file_id !== -1 && mounted) {
            setCrop(parsed);
            Image.getSize(
              `file://${parsed.abs_path}`,
              (width, height) => {
                if (mounted) setImgSize({ width, height });
              },
              (error) => {
                console.error('Failed to get image size', error);
              }
            );
          }
        }
      } catch (error) {
        console.error('Failed to fetch crop', error);
      }
    };

    fetchCrop();
    return () => {
      mounted = false;
    };
  }, [entity.entity_id]);

  const displayName =
    entity.display_name && entity.display_name.trim() !== '' ? entity.display_name : 'Unnamed';
  const IMAGE_SIZE = CELL_WIDTH - 12;

  const navigation = useNavigation<any>();

  const handlePress = () => {
    navigation.navigate('EntityDetail', { entity });
  };

  let imageContent;
  if (!crop || !imgSize) {
    imageContent = <View style={[styles.placeholderImage, { backgroundColor: colors.surface }]} />;
  } else {
    const scale = IMAGE_SIZE / Math.max(crop.bbox_w, crop.bbox_h);
    const translateX = -(crop.bbox_x + crop.bbox_w / 2 - imgSize.width / 2) * scale;
    const translateY = -(crop.bbox_y + crop.bbox_h / 2 - imgSize.height / 2) * scale;
    imageContent = (
      <View
        style={[
          styles.placeholderImage,
          {
            backgroundColor: colors.surface,
            overflow: 'hidden',
            justifyContent: 'center',
            alignItems: 'center',
          },
        ]}
      >
        <Image
          source={{ uri: `file://${crop.abs_path}` }}
          style={{
            width: imgSize.width * scale,
            height: imgSize.height * scale,
            transform: [{ translateX }, { translateY }],
          }}
        />
      </View>
    );
  }

  return (
    <>
      <Pressable style={styles.cellContainer} onPress={handlePress}>
        {imageContent}
        <Text style={[styles.nameText, { color: colors.text }]} numberOfLines={1}>
          {displayName}
        </Text>
      </Pressable>
    </>
  );
};

export const PeopleScreen = () => {
  const { colors } = useTheme();
  const navigation = useNavigation<any>();
  const [entities, setEntities] = useState<EntityMeta[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    const fetchEntities = async () => {
      try {
        const jsonStr = await VantaEngine.getTopEntities();
        const parsed = JSON.parse(jsonStr) as EntityMeta[];
        setEntities(parsed);
      } catch (error) {
        console.error('Failed to load entities:', error);
      } finally {
        setLoading(false);
      }
    };

    fetchEntities();
  }, []);

  React.useEffect(() => {
    const unsubscribe = navigation.addListener('focus', () => {
      const fetchEntities = async () => {
        try {
          const jsonStr = await VantaEngine.getTopEntities();
          const parsed = JSON.parse(jsonStr) as EntityMeta[];
          setEntities(parsed);
        } catch (error) {
          console.error('Failed to load entities:', error);
        }
      };
      fetchEntities();
    });
    return unsubscribe;
  }, [navigation]);

  if (loading) {
    return (
      <View style={[styles.centerContainer, { backgroundColor: colors.background }]}>
        <ActivityIndicator size="large" color={colors.primary} />
      </View>
    );
  }

  const renderItem = ({ item }: { item: EntityMeta }) => {
    return <FaceCell entity={item} colors={colors} onNamed={() => {}} />;
  };

  return (
    <View style={[styles.container, { backgroundColor: colors.background }]}>
      <Text style={[styles.title, { color: colors.text }]}>People</Text>
      <FlatList
        data={entities}
        keyExtractor={(item) => item.entity_id.toString()}
        numColumns={NUM_COLUMNS}
        renderItem={renderItem}
        contentContainerStyle={styles.listContainer}
      />
    </View>
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
  listContainer: {
    paddingBottom: 20,
  },
  cellContainer: {
    width: CELL_WIDTH,
    alignItems: 'center',
    marginBottom: 16,
  },
  placeholderImage: {
    width: CELL_WIDTH - 12,
    height: CELL_WIDTH - 12,
    borderRadius: 8,
  },
  nameText: {
    marginTop: 6,
    fontSize: 14,
    fontWeight: '500',
    textAlign: 'center',
    width: CELL_WIDTH - 12,
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
