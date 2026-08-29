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

add_executable(artpicst WIN32 ${SOURCES} ${HEADERS})

target_include_directories(artpicst PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_compile_definitions(artpicst PRIVATE UNICODE _UNICODE)
    target_link_libraries(artpicst PRIVATE gdiplus user32 kernel32 shell32)
endif()

if(MSVC)
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
    )
endif()

install(TARGETS artpicst
    RUNTIME DESTINATION bin
)

install(FILES README.md
    DESTINATION .
)

