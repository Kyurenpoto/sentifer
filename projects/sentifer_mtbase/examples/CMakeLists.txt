cmake_minimum_required(VERSION 3.11...3.17)

if (${CMAKE_VERSION} VERSION_LESS 3.15)
	cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
else()
	cmake_policy(VERSION 3.15)
endif()

project(example_sentifer_mtbase VERSION 0.1.0 DESCRIPTION "Example for sentifer_mtbase" LANGUAGES CXX)

find_package(mimalloc 1.0 REQUIRED)

add_executable(example_sentifer_mtbase main.cpp)
target_link_libraries(example_sentifer_mtbase INTERFACE sentifer_mtbase)
set_property(TARGET example_sentifer_mtbase PROPERTY CXX_STANDARD_REQUIRED 20)
set_property(TARGET example_sentifer_mtbase PROPERTY CXX_STANDARD 20)
