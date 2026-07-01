/**
 * Mock implementation of the Vanta native bridge for Jest tests.
 *
 * The real module depends on Android MediaStore, JNI, and ONNX Runtime, so it
 * cannot run in Node. This mock returns realistic JSON strings and dummy data
 * so screens can render and unit tests can assert on behavior.
 */

/**
 * Returns a deterministic hello-world string for the Kotlin bridge.
 */
export function helloFromKotlin(): string {
  return 'Hello from Kotlin (mock)';
}

/**
 * Returns a deterministic hello-world string for the C++ bridge.
 */
export function helloFromCpp(): string {
  return 'Hello from C++ (mock)';
}

/**
 * Simulates a media scan. Returns an object matching the native result shape.
 */
export async function startStoring(): Promise<unknown> {
  return { scannedFileCount: 12, status: 'ok' };
}

/**
 * Returns dummy stored file rows.
 */
export async function getStoredFiles(): Promise<unknown[]> {
  return [
    { uri: 'file:///mock/1.jpg', type: 'image', mime: 'image/jpeg', size: 1024 },
    { uri: 'file:///mock/2.mp4', type: 'video', mime: 'video/mp4', size: 2048 },
  ];
}

/**
 * Simulates starting the indexing foreground service.
 */
export async function generateEmbeddings(): Promise<boolean> {
  return true;
}

/**
 * Returns dummy indexing progress as a JSON string.
 */
export async function getIndexProgress(): Promise<string> {
  return JSON.stringify({ processed: 5, total: 12, status: 'idle', currentFile: '' });
}

/**
 * Simulates pausing indexing.
 */
export async function pauseEmbeddings(): Promise<void> {
  // No-op in tests.
}

/**
 * Returns dummy database statistics as a JSON string.
 */
export async function getDatabaseStats(): Promise<string> {
  return JSON.stringify({
    target_files_count: 12,
    clip_vec_count: 5,
    face_vec_count: 3,
  });
}

/**
 * Returns dummy image search results as a JSON string.
 */
export async function searchImages(query: string): Promise<string> {
  return JSON.stringify([
    { abs_path: '/mock/photo1.jpg', display_name: `${query} result 1` },
    { abs_path: '/mock/photo2.jpg', display_name: `${query} result 2` },
  ]);
}
