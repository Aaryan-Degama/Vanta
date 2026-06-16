package expo.modules.vantaengine // This must match the folder structure

import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition

class VantaEngineModule : Module() { // This must match the filename
  // Load the C++ library compiled by CMake
  companion object {
    init {
      System.loadLibrary("vanta")
    }
  }

  // Declare the JNI external C++ function
  private external fun getHelloFromCpp(): String

  override fun definition() = ModuleDefinition {
    Name("VantaEngine") // Update the name Expo uses internally

    // Button 1 logic: Just Kotlin
    Function("helloFromKotlin") {
      return@Function "hi from kotlin"
    }

    // Button 2 logic: Kotlin -> C++
    Function("helloFromCpp") {
      val kotlinPrefix = "hi from kotlin and then "
      val cppString = getHelloFromCpp()
      return@Function kotlinPrefix + cppString
    }
  }
}