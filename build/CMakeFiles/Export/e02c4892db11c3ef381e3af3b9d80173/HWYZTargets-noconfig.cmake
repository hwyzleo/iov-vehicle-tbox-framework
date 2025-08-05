#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "HWYZ::hwyz" for configuration ""
set_property(TARGET HWYZ::hwyz APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(HWYZ::hwyz PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libhwyz.dylib"
  IMPORTED_SONAME_NOCONFIG "@rpath/libhwyz.dylib"
  )

list(APPEND _cmake_import_check_targets HWYZ::hwyz )
list(APPEND _cmake_import_check_files_for_HWYZ::hwyz "${_IMPORT_PREFIX}/lib/libhwyz.dylib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
