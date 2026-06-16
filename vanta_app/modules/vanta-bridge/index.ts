import VantaEngineModule from './src/VantaEngineModule';

export function helloFromKotlin(): string {
  return VantaEngineModule.helloFromKotlin();
}

export function helloFromCpp(): string {
  return VantaEngineModule.helloFromCpp();
}
