cmake_minimum_required(VERSION 3.22.1)

project(VANTA)

# Use C++20 consistently. The Android Gradle plugin also declares C++20 via
# cppFlags so there is no mismatch between Gradle and CMake.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source file collections.
# GLOB_RECURSE is used because the engine has many small source files grouped
# by feature (CLIP preprocessing, face segregation, query processing, DB).
file(GLOB_RECURSE CLIP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/CLIP/*.cpp"
)

file(GLOB_RECURSE SEGREGATION_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/Segregation/*.cpp"
)

# NOTE: The Graph/ directory does not exist yet. The glob below is kept
# commented so it can be enabled when graph algorithms are added without
# re-remembering the CMake structure.
# file(GLOB_RECURSE GRAPH_SOURCES CONFIGURE_DEPENDS
#     "${CMAKE_CURRENT_SOURCE_DIR}/Include/Graph/*.cpp"
# )
set(GRAPH_SOURCES "")

# Query processing sources, including Intent and NER_BERT pipelines.
file(GLOB QUERY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/Query/*.cpp"
)
file(GLOB_RECURSE QUERY_PROCESSING_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/Query/Query_processing/*.cpp"
)
file(GLOB_RECURSE INTENT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/Query/Intent/*.cpp"
)
file(GLOB_RECURSE NER_BERT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/Query/NER_BERT/*.cpp"
)
set(QUERY_SOURCES ${QUERY_SOURCES} ${QUERY_PROCESSING_SOURCES} ${INTENT_SOURCES} ${NER_BERT_SOURCES})

file(GLOB_RECURSE DB_CODE CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/DB/*.cpp"
)

# ONNX Runtime prebuilt shared library.
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/onnxruntime/android/libonnxruntime.so"
)

# OpenCV Android package.
set(OpenCV_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opencv/android/sdk/native/jni"
)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# Main native library (libvanta.so).
add_library(vanta SHARED
    vanta.cpp

    ${DB_CODE}
    ${CLIP_SOURCES}
    ${SEGREGATION_SOURCES}
    ${GRAPH_SOURCES}
    ${QUERY_SOURCES}

    # SQLite amalgamation files.
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite/sqlite3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite_vec/sqlite-vec.c
)

# Preprocessor definitions used by feature-specific headers.
target_compile_definitions(vanta PRIVATE
    CLIP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/CLIP"
    SEGREGATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/Segregation"
)

# Include directories for project code and third-party dependencies.
target_include_directories(vanta PRIVATE
    # Project headers (includes Config.hpp at the root of Include/).
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/Query
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/Query/Query_processing
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/Query/Intent
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/Query/NER_BERT
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/Segregation
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/CLIP
    ${CMAKE_CURRENT_SOURCE_DIR}/DB

    # SQLite amalgamations.
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite_vec

    # ONNX Runtime headers.
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/onnxruntime/include/headers

    # OpenCV headers.
    ${OpenCV_INCLUDE_DIRS}
)

# Android system libraries required by the engine.
find_library(log-lib log)
find_library(android-lib android)
find_library(jnigraphics-lib jnigraphics)

# Link everything together.
target_link_libraries(vanta
    ${log-lib}
    ${android-lib}
    ${jnigraphics-lib}

    ${OpenCV_LIBS}

    onnxruntime
)
