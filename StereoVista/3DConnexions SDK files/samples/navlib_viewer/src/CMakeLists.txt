# Use the shared MFC library.
find_package(MFC REQUIRED)
if (NOT MFC_FOUND)
    message(FATAL_ERROR "Cannot find package MFC")
endif ()

set(CMAKE_MFC_FLAG 2)

add_executable (${CMAKE_PROJECT_NAME} WIN32
                    capplicationcommand.cpp
                    GeomObj.cpp
                    mainfrm.cpp
                    Matrix3d.cpp
                    mcaddoc.cpp
                    mcadview.cpp
                    navlib_viewer.cpp
                    navlib_viewer.rc
                    Stdafx.cpp
                    Visitor.cpp
                    )

target_compile_features(${CMAKE_PROJECT_NAME} PUBLIC cxx_std_17)

target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
                           YOUR_PROGRAM_NAME_GOES_HERE="${CMAKE_PROJECT_NAME}"
                           _AFXDLL
                           _UNICODE
                           UNICODE)


set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(Boost_USE_STATIC_LIBS        ON)

find_package(OpenGL REQUIRED COMPONENTS OpenGL)
    target_link_libraries( ${CMAKE_PROJECT_NAME}
                           OpenGL::GL
                           OpenGL::GLU
                           )

if (USE_NLPROXY_CLIENT)
    # Find the 3DxWare SDK component 3DxWare::NlClient.
    find_package(TDxWare_SDK 4.0 REQUIRED COMPONENTS 3DxWare::NlClient)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:3DxWare::NlClient,INTERFACE_COMPILE_DEFINITIONS>
        )
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:3DxWare::NlClient,INTERFACE_COMPILE_OPTIONS>
        )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:3DxWare::NlClient,INTERFACE_INCLUDE_DIRECTORIES>
        )
    target_link_libraries( ${CMAKE_PROJECT_NAME}
                           $<TARGET_PROPERTY:3DxWare::NlClient,INTERFACE_LINK_LIBRARIES>
                           )
else()
    #
    # Find the 3DxWare SDK component 3DxWare::Navlib.
    find_package(TDxWare_SDK 4.0 REQUIRED COMPONENTS 3DxWare::Navlib)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:3DxWare::Navlib,INTERFACE_INCLUDE_DIRECTORIES>
        )

    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "/ignore:4099")
    target_link_libraries(${CMAKE_PROJECT_NAME}
        $<TARGET_PROPERTY:3DxWare::Navlib,INTERFACE_LINK_LIBRARIES>
        )
endif()

# Workaround for unresolved WinMain error in Unicode MFC application built with CMake using Ninja generator.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "/entry:wWinMainCRTStartup")
