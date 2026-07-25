#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "TBoxFramework::tbox-framework" for configuration "Release"
set_property(TARGET TBoxFramework::tbox-framework APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(TBoxFramework::tbox-framework PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libtbox-framework.dylib"
  IMPORTED_SONAME_RELEASE "@rpath/libtbox-framework.dylib"
  )

list(APPEND _cmake_import_check_targets TBoxFramework::tbox-framework )
list(APPEND _cmake_import_check_files_for_TBoxFramework::tbox-framework "${_IMPORT_PREFIX}/lib/libtbox-framework.dylib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
