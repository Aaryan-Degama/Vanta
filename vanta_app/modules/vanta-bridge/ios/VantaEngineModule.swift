import ExpoModulesCore

public class VantaEngineModule: Module {
  public func definition() -> ModuleDefinition {
    Name("VantaEngine")

    Function("helloFromKotlin") {
      return "hi from swift"
    }

    Function("helloFromCpp") {
      return "hi from swift (C++ not implemented on iOS yet)"
    }
  }
}
