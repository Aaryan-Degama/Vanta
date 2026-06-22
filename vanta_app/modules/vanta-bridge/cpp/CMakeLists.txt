cmake_minimum_required(VERSION 3.22.1)
project(VANTA) 

add_library(vanta SHARED
        vanta.cpp
        DB/DBoperations.cpp
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