# 10 — React Native Bridge

> How the TypeScript/JavaScript layer communicates with the native C++ engine through Expo Modules.

---

## Architecture

![Vanta Logo](../vanta_logo.png)

---

## Module API (TypeScript)

```typescript
// vanta_app/modules/vanta-bridge/src/index.ts

export interface FileMeta {
  mediaId: number;
  contentUri: string;
  absPath: string;
  displayName: string;
  fileDtype: 'image' | 'video' | 'audio' | 'document';
  mimeType: string;
  sizeBytes: number;
  mtimeUnix: number;
  lastIndexedAt: number;
  widthPx: number;
  heightPx: number;
  durationMs: number;
  status: 'pending' | 'indexed' | 'failed' | 'skipped';
  retryCount: number;
}

export interface SearchResult {
  file_id: number;
  abs_path: string;
  display_name: string;
  size_bytes: number;
  mtime_unix: number;
  distance: number;
}

export interface EntityResult {
  entity_id: number;
  display_name: string;
  relation: string;
  sample_count: number;
  best_face_crop: FaceCrop | null;
}

export interface FaceCrop {
  file_id: number;
  abs_path: string;
  bbox_x: number;
  bbox_y: number;
  bbox_w: number;
  bbox_h: number;
}

export interface EntityDetail {
  entity_id: number;
  display_name: string;
  relation: string;
  sample_count: number;
  best_face_crop: FaceCrop;
  photos: SearchResult[];
  co_occurring_entities: {
    entity_id: number;
    display_name: string;
    count: number;
  }[];
}

export interface IndexProgress {
  processed: number;
  total: number;
  status: 'idle' | 'loading_models' | 'processing' | 'paused' | 'finished';
  currentFile: string;
}

// Main module interface
declare const VantaEngineModule: {
  // Search
  searchImagesNative(dbPath: string, query: string): Promise<string>;
  
  // Indexing
  generateEmbeddingsNative(dbPath: string): Promise<string>;
  pauseEmbeddingsNative(): Promise<string>;
  getIndexProgressNative(): Promise<string>;
  
  // Storage
  startStoringNative(dbPath: string, files: FileMeta[]): Promise<string>;
  
  // Entities
  getPeopleNative(dbPath: string): Promise<string>;
  getEntityDetailNative(dbPath: string, entityId: number): Promise<string>;
  setEntityMetadataNative(
    dbPath: string,
    entityId: number,
    displayName: string,
    relation: string,
    age: string,
    location: string
  ): Promise<string>;
  getBestFaceCropNative(dbPath: string, entityId: number): Promise<string>;
  
  // Assets
  extractModelsNative(): Promise<string>;
  getModelsStatusNative(): Promise<string>;
};
```

---

## Hook Patterns

### useSearch
```typescript
function useSearch(dbPath: string) {
  const [results, setResults] = useState<SearchResult[]>([]);
  const [loading, setLoading] = useState(false);
  
  const search = useCallback(async (query: string) => {
    setLoading(true);
    try {
      const json = await VantaEngineModule.searchImagesNative(dbPath, query);
      const data: SearchResult[] = JSON.parse(json);
      setResults(data);
    } catch (err) {
      console.error('Search failed:', err);
      setResults([]);
    } finally {
      setLoading(false);
    }
  }, [dbPath]);
  
  return { results, loading, search };
}
```

### useIndexing
```typescript
function useIndexing(dbPath: string) {
  const [progress, setProgress] = useState<IndexProgress>({
    processed: 0, total: 0, status: 'idle', currentFile: ''
  });
  
  const start = useCallback(async () => {
    await VantaEngineModule.generateEmbeddingsNative(dbPath);
    // Poll progress
    const interval = setInterval(async () => {
      const json = await VantaEngineModule.getIndexProgressNative();
      const p = JSON.parse(json);
      setProgress(p);
      if (p.status === 'finished') clearInterval(interval);
    }, 1000);
  }, [dbPath]);
  
  const pause = useCallback(() => {
    VantaEngineModule.pauseEmbeddingsNative();
  }, []);
  
  return { progress, start, pause };
}
```

---

## Screen Data Flow

### SearchScreen

![Vanta Logo](../vanta_logo.png)

### PeopleScreen

![Vanta Logo](../vanta_logo.png)

### EntityDetailScreen

![Vanta Logo](../vanta_logo.png)

---

## Error Handling Strategy

| Layer | Error Type | Handling |
|-------|-----------|----------|
| Native (C++) | Exception | Catch, log, return empty JSON |
| Native (Kotlin) | Exception | Catch, return error string |
| TypeScript | JSON parse fail | Return empty array, log error |
| UI | Empty results | Show "No results found" |
| UI | Network (future) | N/A — all on-device |

---

## Performance Considerations

| Operation | JS Overhead | Native Time | Total |
|-----------|-------------|-------------|-------|
| Search | ~2 ms | ~60-150 ms | ~65-155 ms |
| Get People | ~2 ms | ~10-30 ms | ~12-35 ms |
| Get Entity Detail | ~2 ms | ~20-50 ms | ~22-55 ms |
| Start Indexing | ~5 ms | ~1-10 min | Async |
| Poll Progress | ~1 ms | ~1 ms | ~2 ms |

> **Note:** All native calls are async (Promises). The UI remains responsive during long-running operations.

---

## Future Improvements

1. **Streaming results** — Return results as they're found instead of all-at-once
2. **Cancellation tokens** — Allow aborting long searches
3. **Prefetching** — Load next page of results before user scrolls
4. **Background sync** — Auto-index new photos when app is backgrounded
5. **iOS parity** — Implement Swift version of VantaEngineModule
