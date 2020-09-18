cmake_minimum_required(VERSION 3.11...3.17)

if (${CMAKE_VERSION} VERSION_LESS 3.15)
	cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
else()
	cmake_policy(VERSION 3.15)
endif()

project(eskada VERSION 0.1.0 DESCRIPTION "Code base for event loop." LANGUAGES CXX)

add_library(eskada INTERFACE
)
target_compile_features(eskada INTERFACE cxx_std_20)

add_subdirectory(tests)