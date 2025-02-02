#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "unity" for configuration "Debug"
set_property(TARGET unity APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(unity PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "/home/matt/Documents/Education/Graduate/AdvancedEmbeddedSoftware/assignment-1-HartnettMatt/assignment-autotest/Unity/src/libunity.a"
  )

list(APPEND _cmake_import_check_targets unity )
list(APPEND _cmake_import_check_files_for_unity "/home/matt/Documents/Education/Graduate/AdvancedEmbeddedSoftware/assignment-1-HartnettMatt/assignment-autotest/Unity/src/libunity.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
