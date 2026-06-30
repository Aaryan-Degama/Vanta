cmake_minimum_required(VERSION 3.22.1)

project(VANTA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source Files

file(GLOB_RECURSE CLIP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/CLIP/*.cpp"
)

file(GLOB_RECURSE SEGREGATION_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/Segregation/*.cpp"
)

file(GLOB_RECURSE GRAPH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/Graph/*.cpp"
)

file(GLOB_RECURSE QUERY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/Query/*.cpp"
)

file(GLOB_RECURSE DB_CODE CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/DB/*.cpp"
)

# ONNX Runtime

add_library(onnxruntime SHARED IMPORTED)

set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/onnxruntime/android/libonnxruntime.so"
)

# OpenCV

set(OpenCV_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opencv/android/sdk/native/jni"
)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# Main Library

add_library(vanta SHARED
    vanta.cpp

    ${DB_CODE}


    ${CLIP_SOURCES}
    ${SEGREGATION_SOURCES}
    ${GRAPH_SOURCES}
    ${QUERY_SOURCES}

    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite/sqlite3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite_vec/sqlite-vec.c
)

# Compile Definitions

target_compile_definitions(vanta PRIVATE
    CLIP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/CLIP"
    SEGREGATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/Segregation"
)

# Include Directories

target_include_directories(vanta PRIVATE

    # Project
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/Segregation
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/CLIP
    ${CMAKE_CURRENT_SOURCE_DIR}/DB

    # SQLite
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite_vec

    # ONNX Runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/onnxruntime/include/headers

    # OpenCV
    ${OpenCV_INCLUDE_DIRS}
)

# Android System Libraries

find_library(log-lib log)
find_library(android-lib android)
find_library(jnigraphics-lib jnigraphics)

# Linking

target_link_libraries(vanta

    ${log-lib}
    ${android-lib}
    ${jnigraphics-lib}

    ${OpenCV_LIBS}

    onnxruntime
)