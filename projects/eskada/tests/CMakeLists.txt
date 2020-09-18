cmake_minimum_required(VERSION 3.11...3.17)

if (${CMAKE_VERSION} VERSION_LESS 3.15)
	cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
else()
	cmake_policy(VERSION 3.15)
endif()

project(test_eskada VERSION 0.1.0 DESCRIPTION "Tests for eskada" LANGUAGES CXX)

add_executable(test_eskada
	"main.cpp"
)
target_link_libraries(test_eskada PUBLIC eskada)
target_link_libraries(test_eskada PUBLIC doctest)
target_include_directories(test_eskada PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)
set_property(TARGET test_eskada PROPERTY CXX_STANDARD_REQUIRED 20)
set_property(TARGET test_eskada PROPERTY CXX_STANDARD 20)
