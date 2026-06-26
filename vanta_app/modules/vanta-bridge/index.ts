import VantaEngineModule from './src/VantaEngineModule';

export function helloFromKotlin(): string {
  return VantaEngineModule.helloFromKotlin();
}

export function helloFromCpp(): string {
  return VantaEngineModule.helloFromCpp();
}

export async function startStoring(): Promise<any> {
  return await VantaEngineModule.startStoring();
}

export async function getStoredFiles(): Promise<any[]> {
  return await VantaEngineModule.getStoredFiles();
}

export async function generateEmbeddings(): Promise<boolean> {
  return await VantaEngineModule.generateEmbeddings();
}

export async function getIndexProgress(): Promise<string> {
  return await VantaEngineModule.getIndexProgress();
}

export async function pauseEmbeddings(): Promise<void> {
  return await VantaEngineModule.pauseEmbeddings();
}

export async function getDatabaseStats(): Promise<string> {
  return await VantaEngineModule.getDatabaseStats();
}

export async function searchImages(query: string): Promise<string> {
  return await VantaEngineModule.searchImages(query);
}
