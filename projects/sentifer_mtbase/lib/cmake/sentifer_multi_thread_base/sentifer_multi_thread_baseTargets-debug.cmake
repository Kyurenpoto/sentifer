#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "sentifer_multi_thread_base::sentifer_multi_thread_base" for configuration "Debug"
set_property(TARGET sentifer_multi_thread_base::sentifer_multi_thread_base APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(sentifer_multi_thread_base::sentifer_multi_thread_base PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/sentifer_multi_thread_base.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS sentifer_multi_thread_base::sentifer_multi_thread_base )
list(APPEND _IMPORT_CHECK_FILES_FOR_sentifer_multi_thread_base::sentifer_multi_thread_base "${_IMPORT_PREFIX}/lib/sentifer_multi_thread_base.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
