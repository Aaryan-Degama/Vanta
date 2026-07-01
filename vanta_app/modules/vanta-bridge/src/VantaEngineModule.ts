/**
 * TypeScript declaration and loader for the VantaEngine native module.
 *
 * `NativeModule` is the Expo type that represents a module implemented in
 * platform-native code (Kotlin + C++ on Android, Objective-C/Swift on iOS).
 * `Record<string, never>` is used for the module's event shape because this
 * module does not emit any native events.
 */
import { NativeModule, requireNativeModule } from 'expo';

// Type-only declaration of the native module's API surface.
// The methods declared here must match the AsyncFunction/Function definitions
// in the Kotlin VantaEngineModule exactly.
declare class VantaEngineModule extends NativeModule<Record<string, never>> {
  // Test helpers that bridge to Kotlin/C++ hello-world functions.
  helloFromKotlin(): string;
  helloFromCpp(): string;

  // Scans device media and inserts metadata into the SQLite database.
  startStoring(): Promise<unknown>;

  // Debug helper: returns the most recently stored file rows.
  getStoredFiles(): Promise<unknown[]>;

  // Starts the foreground-service indexing process.
  generateEmbeddings(): Promise<boolean>;

  // Returns current indexing progress as a JSON string.
  getIndexProgress(): Promise<string>;

  // Pauses the indexing foreground service.
  pauseEmbeddings(): Promise<void>;

  // Returns database statistics as a JSON string.
  getDatabaseStats(): Promise<string>;

  // Searches indexed images with a natural-language query.
  searchImages(query: string): Promise<string>;

  // Face-clustering API: returns the top entities as a JSON string.
  getTopEntities(): Promise<string>;

  // Returns the best thumbnail crop for the given entity.
  getBestFaceCrop(entityId: number): Promise<string>;

  // Assigns a display name to a face entity.
  setEntityName(entityId: number, name: string): Promise<boolean>;

  // Returns co-occurring people for the given entity as a JSON string.
  getEntityNeighbors(entityId: number): Promise<string>;

  // Returns files associated with the entity as a JSON string.
  getEntityFiles(entityId: number): Promise<string>;

  // Sets metadata (name, relation, age, location) on a face entity.
  setEntityMetadata(
    entityId: number,
    name: string,
    relation: string,
    age: number,
    location: string
  ): Promise<boolean>;

  // Returns full metadata for a face entity as a JSON string.
  getEntityMetadata(entityId: number): Promise<string>;

  // Sets the owner entity ID (the user's own face).
  setOwnerEntityId(entityId: number): Promise<void>;

  // Returns the current owner entity ID (-1 if not set).
  getOwnerEntityId(): Promise<number>;
}

// `requireNativeModule` resolves the platform-specific implementation at runtime.
export default requireNativeModule<VantaEngineModule>('VantaEngine');
