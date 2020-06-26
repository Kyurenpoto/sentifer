cmake_minimum_required(VERSION 3.11...3.17)

if (${CMAKE_VERSION} VERSION_LESS 3.15)
	cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
else()
	cmake_policy(VERSION 3.15)
endif()

project(test_sentifer_mtbase VERSION 0.1.0 DESCRIPTION "Tests for sentifer_mtbase" LANGUAGES CXX)

add_executable(test_sentifer_mtbase
	"main.cpp"
)
target_link_libraries(test_sentifer_mtbase PUBLIC sentifer_mtbase)
target_link_libraries(test_sentifer_mtbase PUBLIC doctest)
target_include_directories(test_sentifer_mtbase PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)
set_property(TARGET test_sentifer_mtbase PROPERTY CXX_STANDARD_REQUIRED 20)
set_property(TARGET test_sentifer_mtbase PROPERTY CXX_STANDARD 20)
