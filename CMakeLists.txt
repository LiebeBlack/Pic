cmake_minimum_required(VERSION 3.10)
project(ARTPICST LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(SOURCES
    src/main.cpp
)

set(HEADERS
    include/stb_image.h
    include/stb_image_resize2.h
)

add_executable(artpicst WIN32 ${SOURCES} ${HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/artpicst.manifest)

target_include_directories(artpicst PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_compile_definitions(artpicst PRIVATE
        UNICODE
        _UNICODE
        STBI_WINDOWS_UTF8
        STBI_INCLUDE_JPEG
        STBI_INCLUDE_PNG
        STBI_INCLUDE_GIF
        STBI_INCLUDE_BMP
        STBI_INCLUDE_TGA
        STBI_INCLUDE_TIFF
        STBI_INCLUDE_WEBP
        STBI_INCLUDE_PSD
        STBI_INCLUDE_HDR
        STBI_INCLUDE_PNM
    )
    target_link_libraries(artpicst PRIVATE gdiplus user32 kernel32 shell32 gdi32 msimg32 ole32)
endif()

if(MSVC)
    set_property(TARGET artpicst PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(artpicst PRIVATE
        /W4
        /O2
        /GL
        /Oi
        /Ot
        /Oy
        /GF
    )
    target_link_options(artpicst PRIVATE
        "/LTCG"
        "/OPT:REF"
        "/OPT:ICF"
        "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    )
endif()

install(TARGETS artpicst
    RUNTIME DESTINATION bin
)

install(FILES README.md
    DESTINATION .
)

