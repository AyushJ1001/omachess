cmake_minimum_required(VERSION 3.24)

project(omachess VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)
enable_testing()

include(cmake/OmachessCore.cmake)
add_subdirectory(app)
add_subdirectory(tests)
