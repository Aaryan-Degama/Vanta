/**
 * Public JavaScript API for the Vanta native engine.
 *
 * This module re-exports the typed native functions in a convenient shape for
 * the React Native screens. Return values that come from C++ as opaque JSON
 * objects are typed as `unknown` so callers are forced to validate them.
 */
import VantaEngineModule from './src/VantaEngineModule';

/**
 * Returns a hello-world string from the Kotlin side.
 * Useful for verifying the native bridge is wired correctly.
 */
export function helloFromKotlin(): string {
  return VantaEngineModule.helloFromKotlin();
}

/**
 * Returns a hello-world string from the C++ side.
 */
export function helloFromCpp(): string {
  return VantaEngineModule.helloFromCpp();
}

/**
 * Scans device media and inserts file metadata into the SQLite database.
 * Returns an opaque result object from the native layer.
 */
export async function startStoring(): Promise<unknown> {
  return await VantaEngineModule.startStoring();
}

/**
 * Returns the most recently stored file rows from the database.
 */
export async function getStoredFiles(): Promise<unknown[]> {
  return await VantaEngineModule.getStoredFiles();
}

/**
 * Starts the foreground-service indexing process that generates CLIP and face
 * embeddings for all unindexed media.
 */
export async function generateEmbeddings(): Promise<boolean> {
  return await VantaEngineModule.generateEmbeddings();
}

/**
 * Returns the current indexing progress as a JSON string.
 */
export async function getIndexProgress(): Promise<string> {
  return await VantaEngineModule.getIndexProgress();
}

/**
 * Pauses the indexing foreground service.
 */
export async function pauseEmbeddings(): Promise<void> {
  return await VantaEngineModule.pauseEmbeddings();
}

/**
 * Returns database statistics as a JSON string.
 */
export async function getDatabaseStats(): Promise<string> {
  return await VantaEngineModule.getDatabaseStats();
}

/**
 * Searches indexed images using the provided natural-language query.
 * Results are returned as a JSON string.
 */
export async function searchImages(query: string): Promise<string> {
  return await VantaEngineModule.searchImages(query);
}
