# - Config file for the TDxWare package
# It defines the following variables
#  TDxWare_INCLUDE_DIRS - include directory for the 3DxWare SDK
#  TDxWare_LIBRARIES    - libraries to link against

include(FindPackageHandleStandardArgs)
set(${CMAKE_FIND_PACKAGE_NAME}_CONFIG ${CMAKE_CURRENT_LIST_FILE})
find_package_handle_standard_args(TDxWare_SDK CONFIG_MODE)


# Compute paths
get_filename_component(TDxWare_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)

# Our library dependencies (contains definitions for IMPORTED targets)
if(NOT TARGET TDxWare_SDK)
  include("${TDxWare_CMAKE_DIR}/TDxWare_SDKTargets.cmake")
endif()

