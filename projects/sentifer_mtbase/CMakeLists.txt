cmake_minimum_required(VERSION 3.11...3.17)

if (${CMAKE_VERSION} VERSION_LESS 3.15)
	cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
else()
	cmake_policy(VERSION 3.15)
endif()

project(sentifer_mtbase VERSION 0.1.0 DESCRIPTION "Nice code base for multi-thread programming, header-only." LANGUAGES CXX)

add_library(sentifer_mtbase INTERFACE)
target_link_libraries(sentifer_mtbase INTERFACE mimalloc)
target_include_directories(sentifer_mtbase INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../submodules/mimalloc/include>)
target_compile_features(sentifer_mtbase INTERFACE cxx_std_20)

add_subdirectory(examples)
add_subdirectory(tests)