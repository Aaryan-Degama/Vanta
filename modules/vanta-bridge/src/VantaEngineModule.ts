import { NativeModule, requireNativeModule } from 'expo';

declare class VantaEngineModule extends NativeModule<{}> {
  helloFromKotlin(): string;
  helloFromCpp(): string;
}

export default requireNativeModule<VantaEngineModule>('VantaEngine');
