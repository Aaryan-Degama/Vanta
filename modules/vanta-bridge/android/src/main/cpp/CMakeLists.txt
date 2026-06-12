cmake_minimum_required(VERSION 3.4.1)
project(vanta) # Add this line

add_library(vanta SHARED vanta.cpp)

find_library(log-lib log)
target_link_libraries(vanta ${log-lib})