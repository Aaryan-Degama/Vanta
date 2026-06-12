import { registerWebModule, NativeModule } from 'expo';

// VantaEngineModule is not available on the web platform.
class VantaEngineModule extends NativeModule<{}> {
  helloFromKotlin(): string {
    return 'VantaEngine is not supported on web';
  }
  helloFromCpp(): string {
    return 'VantaEngine is not supported on web';
  }
}

export default registerWebModule(VantaEngineModule, 'VantaEngineModule');
