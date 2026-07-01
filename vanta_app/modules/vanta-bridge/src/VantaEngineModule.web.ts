/**
 * Web stub for the VantaEngine native module.
 *
 * The Vanta engine depends on Android MediaStore, JNI, and ONNX Runtime, so it
 * cannot run in a browser. This stub implements the same shape with safe
 * fallbacks so the app can still be rendered with `expo start --web` for UI
 * development. `Record<string, never>` is used because the module emits no
 * native events.
 */
import { registerWebModule, NativeModule } from 'expo';

class VantaEngineModule extends NativeModule<Record<string, never>> {
  // Returns a friendly message explaining that the engine is native-only.
  helloFromKotlin(): string {
    return 'VantaEngine is not supported on web';
  }

  helloFromCpp(): string {
    return 'VantaEngine is not supported on web';
  }
}

// Registers the stub so imports of `VantaEngine` resolve on web.
export default registerWebModule(VantaEngineModule, 'VantaEngineModule');
