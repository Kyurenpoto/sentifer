cmake_minimum_required(VERSION 3.11...3.17)

if (${CMAKE_VERSION} VERSION_LESS 3.15)
	cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
else()
	cmake_policy(VERSION 3.15)
endif()

project(sentifer VERSION 0.1.0 DESCRIPTION "Kyurenpoto's mono-repo" LANGUAGES CXX)

# projects
add_subdirectory(projects/sentifer_mtbase)
add_subdirectory(projects/eskada)

# submodules
add_subdirectory(submodule/doctest)
add_subdirectory(submodule/fmt)
add_subdirectory(submodule/ut)

# dependencies
add_dependencies(sentifer_mtbase doctest)
add_dependencies(sentifer_mtbase fmt)
add_dependencies(eskada doctest)
add_dependencies(eskada fmt)
add_dependencies(eskada boost::ut)
