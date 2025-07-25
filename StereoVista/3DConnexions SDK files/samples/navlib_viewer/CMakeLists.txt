cmake_minimum_required(VERSION 3.11)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
    cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project(navlib_viewer VERSION 1.0
                  DESCRIPTION "Navigation Library Viewer Sample"
                  LANGUAGES CXX)

                  #
# build options should be added below.
#

option( USE_NLPROXY_CLIENT
    "Build the sample using the NlProxy client websocket interface (default OFF)."
    OFF )

if (MSVC)
    add_compile_options( /MP ) 

    # Disallow implicit linking for Boost
    add_compile_definitions( BOOST_ALL_NO_LIB )

    # Hide Windows's min() and max() macros
    add_definitions( -DNOMINMAX )
endif()

add_subdirectory( src )







