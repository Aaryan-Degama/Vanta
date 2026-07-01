/**
 * Mock for the `expo` package used by Jest tests.
 *
 * Screens and native module declarations import only `NativeModule`,
 * `requireNativeModule`, and `registerWebModule` from expo. This mock provides
 * minimal implementations so tests can run in Node without loading the full
 * Expo runtime.
 */

/**
 * Base class for native module declarations.
 */
export class NativeModule<T = Record<string, never>> {
  // NativeModule is used as a type base; tests never instantiate it directly.
}

/**
 * Stub that resolves a native module by name.
 *
 * Returns an object with all methods returning safe defaults so that call sites
 * do not crash when the module is invoked.
 */
export function requireNativeModule(name: string): any {
  return new Proxy(
    {},
    {
      get(_target, prop) {
        // eslint-disable-next-line @typescript-eslint/no-unused-vars
        return (..._args: unknown[]) => {
          const propName = String(prop);
          if (propName === 'helloFromKotlin' || propName === 'helloFromCpp') {
            return `Hello from ${name} (mock)`;
          }
          if (
            propName === 'startStoring' ||
            propName === 'getTopEntities' ||
            propName === 'getBestFaceCrop' ||
            propName === 'getEntityNeighbors' ||
            propName === 'getEntityFiles'
          ) {
            return Promise.resolve('[]');
          }
          if (propName === 'setEntityName') {
            return Promise.resolve(true);
          }
          return Promise.resolve(null);
        };
      },
    }
  );
}

/**
 * Registers a web module stub. Not used by native tests.
 */
export function registerWebModule(module: any, _name?: string): any {
  return module;
}
