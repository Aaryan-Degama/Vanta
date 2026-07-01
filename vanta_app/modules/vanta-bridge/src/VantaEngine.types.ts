// Define your exported module types here.

export type EntityMeta = {
  entity_id: number;
  display_name: string;
  sample_count: number;
  confidence: number;
};

export type FaceCrop = {
  file_id: number;
  abs_path: string;
  bbox_x: number;
  bbox_y: number;
  bbox_w: number;
  bbox_h: number;
  aligned_crop_path: string;
};

export type NeighborResult = {
  neighbor_id: number;
  display_name: string;
  co_occurrence_count: number;
};

export type EntityFile = {
  file_id: number;
  abs_path: string;
};

export type EntityMetadata = {
  entity_id: number;
  display_name: string;
  relation: string;
  age: number;
  location: string;
  sample_count: number;
  confidence: number;
};
