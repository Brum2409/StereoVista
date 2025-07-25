cmake_minimum_required(VERSION 3.13)


project(3DxTraceNL VERSION 1.0
                  DESCRIPTION "Tracing sample for the Navlib"
                  LANGUAGES CXX)

#
# build options should be added below.
#

option( USE_NLPROXY_CLIENT
    "Build the sample using the NlProxy client websocket interface (default OFF)."
    OFF )

option( TRACE_NAVLIB
    "Tracing the communication with the navlib (default ON)."
    ON )

if (TRACE_NAVLIB)
    add_compile_definitions(TRACE_NAVLIB)
endif()

if(MSVC)
    add_compile_options( /MP )

    add_link_options( /ignore:4099 )

elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")

    add_link_options( LINKER:/ignore:4099 )

endif()

# Disallow implicit linking for Boost
add_compile_definitions( BOOST_ALL_NO_LIB )

# Hide Windows's min() and max() macros
add_definitions( -DNOMINMAX )

set(Boost_USE_STATIC_LIBS        ON)

add_subdirectory( 3DxTraceNL )







