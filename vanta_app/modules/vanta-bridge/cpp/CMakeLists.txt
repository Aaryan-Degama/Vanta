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

add_library(vanta SHARED
        // JNI
        vanta.cpp

        // DB Operations
        DB/DBoperations.cpp
        
        // CLIP 
        ${CLIP_SOURCES}

        // SQLITE & SQLITE-VEC 
        sqlite/sqlite3.c
        sqlite_vec/sqlite-vec.c
)

target_include_directories(vanta PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/sqlite
        ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_vec
)

find_library(log-lib log)

target_link_libraries(vanta
        ${log-lib}
)