#include <jni.h>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_expo_modules_vantaengine_VantaEngineModule_getHelloFromCpp(JNIEnv* env, jobject /* this */) {
    std::string hello = "hi from C++ code";
    return env->NewStringUTF(hello.c_str());
}