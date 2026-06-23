import { NativeModule, requireNativeModule } from 'expo';

declare class VantaEngineModule extends NativeModule<{}> {
  helloFromKotlin(): string;
  helloFromCpp(): string;
  startStoring(): Promise<any>;
  getStoredFiles(): Promise<any[]>;
}

export default requireNativeModule<VantaEngineModule>('VantaEngine');
