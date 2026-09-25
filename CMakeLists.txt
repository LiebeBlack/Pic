cmake_minimum_required(VERSION 3.20)
project(ARTPICST VERSION 1.2.0 LANGUAGES CXX)

# C++23 en todo el proyecto: el núcleo de imagen (src/image_core.hpp) usa
# constexpr de evaluación obligatoria, [[likely]]/[[unlikely]], [[assume]],
# std::unreachable, alignas/alignof y static_assert, además de SSE2 explícito.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin")

add_executable(artpicst WIN32
    src/main.cpp
    src/image_core.hpp
    src/updater_client.hpp
    artpicst.manifest
    artpicst.rc
)

# ----------------------------------------------------------------------------
# artpicst_updater: Módulo Inteligente de Actualización Automática (Release
# Sync). Consulta la API de GitHub, notifica, descarga y encadena el
# instalador. Ejecutable independiente con manifiesto asInvoker.
# ----------------------------------------------------------------------------
add_executable(artpicst_updater WIN32
    updater/artpicst_updater.cpp
    updater/artpicst_updater.rc
)
target_include_directories(artpicst_updater PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/installer)

# ----------------------------------------------------------------------------
# Instalador autocontenido: incrusta el payload (visor, icono, version.json,
# README y updater) desde resources/app/. La carpeta debe estar poblada por
# scripts/collect_app_payload.py (o manualmente) ANTES de compilar.
# ----------------------------------------------------------------------------
set(ARTPICST_PAYLOAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/app")
foreach(payload_file artpicst.exe artpicst.ico version.json README.md artpicst_updater.exe)
    if(NOT EXISTS "${ARTPICST_PAYLOAD_DIR}/${payload_file}")
        message(FATAL_ERROR
            "Falta ${ARTPICST_PAYLOAD_DIR}/${payload_file}. "
            "Ejecuta primero scripts/collect_app_payload.py tras compilar el visor y el updater.")
    endif()
endforeach()

add_executable(artpicst_installer WIN32
    installer/artpicst_installer.cpp
    installer/version.hpp
    installer/artpicst_installer.rc
)
target_include_directories(artpicst_installer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/installer)

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
    target_compile_definitions(artpicst_installer PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0601
    )
    target_compile_definitions(artpicst_updater PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0601
    )
    target_link_libraries(artpicst PRIVATE
        gdiplus user32 kernel32 shell32 shlwapi gdi32 msimg32 ole32 oleaut32 uuid dwmapi windowscodecs comdlg32 d2d1 dwrite
    )
    target_link_libraries(artpicst_installer PRIVATE
        gdiplus shlwapi shell32 comctl32 dwmapi user32 advapi32 gdi32 ole32 uuid
    )
    target_link_libraries(artpicst_updater PRIVATE
        gdiplus shell32 shlwapi user32 advapi32 gdi32 ole32 uuid dwmapi winhttp
    )
endif()

if(MSVC)
    set_property(TARGET artpicst PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET artpicst_installer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET artpicst_updater PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(artpicst PRIVATE
        /W4
        /utf-8
        /MP
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ob3>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Debug>:/Od>
    )
    target_compile_options(artpicst_installer PRIVATE
        /W4
        /utf-8
        /MP
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Debug>:/Od>
    )
    target_compile_options(artpicst_updater PRIVATE
        /W4
        /utf-8
        /MP
        $<$<CONFIG:Release>:/O2>
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
    # El instalador es TRADICIONAL (Program Files + HKLM): requiere elevación.
    target_link_options(artpicst_installer PRIVATE
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    )
    target_link_options(artpicst_updater PRIVATE
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    )
else()
    # MinGW / GCC / Clang: vectorización agresiva del núcleo de imagen.
    target_compile_options(artpicst PRIVATE -O3 -funroll-loops -fno-math-errno)
    target_compile_options(artpicst_installer PRIVATE -O2)
    target_compile_options(artpicst_updater PRIVATE -O2)
    target_link_options(artpicst_updater PRIVATE -municode)
endif()

install(TARGETS artpicst artpicst_installer artpicst_updater RUNTIME DESTINATION .)
install(FILES README.md DESTINATION .)
install(FILES "docs/REFACTOR_C23.md" DESTINATION . OPTIONAL)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/artpicst.ico" DESTINATION .)
