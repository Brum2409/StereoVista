
function(copy_images_to_target_dir target)
    add_custom_command(
        TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_PROPERTY:${target},SOURCE_DIR>/Images"
            "$<TARGET_PROPERTY:${target},BINARY_DIR>/Images"
    )
endfunction()

add_executable (${CMAKE_PROJECT_NAME} 3DxTraceNL.cpp CApplication3D.cpp CNavigationModel.cpp CViewportViewModel.cpp)

target_compile_features(${CMAKE_PROJECT_NAME} PUBLIC cxx_std_17)

target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
                           YOUR_PROGRAM_NAME_GOES_HERE="${CMAKE_PROJECT_NAME}"
                           _UNICODE
                           UNICODE)

set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if (USE_NLPROXY_CLIENT)
    #
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

    target_link_libraries(${CMAKE_PROJECT_NAME}
        $<TARGET_PROPERTY:3DxWare::Navlib,INTERFACE_LINK_LIBRARIES>
        )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9")

    target_link_libraries(${CMAKE_PROJECT_NAME} stdc++fs)

endif()

copy_images_to_target_dir(${CMAKE_PROJECT_NAME})