cmake_minimum_required(VERSION 3.22.1)
project(VANTA) 

file(GLOB CLIP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/CLIP/*.cpp"
)

file(GLOB SEGEREGATION_SOURCES 
        "${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/Segregation/*.cpp"
)

file(GLOB GRAPH_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Include/Graph/*.cpp"
)

file(GLOB DB_CODE
        "${CMAKE_CURRENT_SOURCE_DIR}/DB/*.cpp"
)

# 1. Define the library first
add_library(vanta SHARED
        # JNI
        vanta.cpp

        # DB
        ${DB_CODE}
        
        # CLIP 
        #${CLIP_SOURCES}

        # Segregation
        #${SEGEREGATION_SOURCES}

        # Graph 
        #${GRAPH_SOURCES}

        # SQLITE & SQLITE-VEC 
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite/sqlite3.c
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite_vec/sqlite-vec.c
)

# 2. NOW apply compile definitions to the 'vanta' target
target_compile_definitions(vanta PRIVATE
    CLIP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/CLIP"
    SEGREGATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Include/Preprocessing/Segregation"
)

# 3. Include directories
target_include_directories(vanta PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite_vec
        ${CMAKE_CURRENT_SOURCE_DIR}/DB
        ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

find_library(log-lib log)

target_link_libraries(vanta
        ${log-lib}
)