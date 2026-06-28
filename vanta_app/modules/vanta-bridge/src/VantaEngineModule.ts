import { NativeModule, requireNativeModule } from 'expo';

declare class VantaEngineModule extends NativeModule<{}> {
  helloFromKotlin(): string;
  helloFromCpp(): string;
  startStoring(): Promise<any>;
  getStoredFiles(): Promise<any[]>;
  getTopEntities(): Promise<string>;
  getBestFaceCrop(entityId: number): Promise<string>;
  setEntityName(entityId: number, name: string): Promise<boolean>;
  getEntityNeighbors(entityId: number): Promise<string>;
  getEntityFiles(entityId: number): Promise<string>;
}

export default requireNativeModule<VantaEngineModule>('VantaEngine');
