cmake_minimum_required(VERSION 3.16)
project(ARTPICST VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin")

add_executable(artpicst WIN32
    src/main.cpp
    artpicst.manifest
    artpicst.rc
)

target_include_directories(artpicst PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_compile_definitions(artpicst PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        STBI_WINDOWS_UTF8
        _WIN32_WINNT=0x0601
    )
    target_link_libraries(artpicst PRIVATE
        gdiplus user32 kernel32 shell32 shlwapi gdi32 msimg32 ole32 oleaut32 uuid dwmapi windowscodecs comdlg32
    )
endif()

if(MSVC)
    set_property(TARGET artpicst PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(artpicst PRIVATE
        /W4
        /utf-8
        /MP
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Debug>:/Od>
    )
    target_link_options(artpicst PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    )
endif()

install(TARGETS artpicst RUNTIME DESTINATION .)
install(FILES README.md DESTINATION .)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/artpicst.ico" DESTINATION .)
