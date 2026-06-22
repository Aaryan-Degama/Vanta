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
