cmake_minimum_required(VERSION 3.20)
project(ARTPICST VERSION 1.2.1 LANGUAGES CXX)

# C++23 en todo el proyecto: el núcleo de imagen (src/image_core.hpp) usa
# constexpr de evaluación obligatoria, [[likely]]/[[unlikely]], [[assume]],
# std::unreachable, alignas/alignof y static_assert, además de SIMD explícito.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# Opciones
# ---------------------------------------------------------------------------
# El instalador autocontenido incrusta el payload (visor + updater + icono +
# version.json + README) desde resources/app/ como recursos RCDATA, así que solo
# puede compilarse DESPUÉS de poblar esa carpeta. Antes esto se comprobaba con
# un FATAL_ERROR incondicional: un `cmake --build` en limpio no podía construir
# NADA. Ahora el instalador es opcional y el payload se genera solo.
option(ARTPICST_PACKAGE
       "Compilar el instalador autocontenido (genera resources/app/ automáticamente)" OFF)
option(ARTPICST_BUILD_TESTS
       "Compilar los ejecutables de test y el banco de kernels" ON)

# ---------------------------------------------------------------------------
# Baseline SIMD: SSE4.2 (NUNCA AVX/AVX2/AVX-512)
# ---------------------------------------------------------------------------
# El baseline NO se escribe aquí: se lee de tools/build-baseline.json, la única
# fuente de verdad del proyecto (la misma que usan build.ps1, MinGW y los
# workflows). Cambiar el baseline es un solo edit, no nueve.
#
# Se aplica por dos vías complementarias:
#   1. el flag del compilador permite EMITIR las instrucciones SSE4.1/SSE4.2;
#   2. la macro de la cabecera (ARTPICST_TARGET_SSE42) hace que image_core.hpp
#      ACTIVE esas rutas: MSVC no define __SSE4_2__, así que la macro explícita
#      es la fuente de verdad para el preprocesador.
# Las dos son necesarias. Si el compilador no soporta el flag se aborta con un
# mensaje accionable en lugar de producir un binario SSE2 en silencio.
set(ARTPICST_BASELINE_JSON "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-baseline.json")
if(NOT EXISTS "${ARTPICST_BASELINE_JSON}")
    message(FATAL_ERROR
        "Falta ${ARTPICST_BASELINE_JSON}: es la única fuente de verdad del baseline SIMD.")
endif()
file(READ "${ARTPICST_BASELINE_JSON}" ARTPICST_BASELINE)
string(JSON ARTPICST_SIMD_MSVC     GET "${ARTPICST_BASELINE}" simd msvc_flag)
string(JSON ARTPICST_SIMD_GNU      GET "${ARTPICST_BASELINE}" simd gnu_flag)
string(JSON ARTPICST_SIMD_DEFINE   GET "${ARTPICST_BASELINE}" simd define)

if(MSVC)
    set(ARTPICST_SIMD_FLAG "${ARTPICST_SIMD_MSVC}")
else()
    set(ARTPICST_SIMD_FLAG "${ARTPICST_SIMD_GNU}")
endif()

# No se degrada en silencio: si el JSON no dice SSE4.2, se aborta.
if(NOT ARTPICST_SIMD_FLAG MATCHES "SSE4\\.2|msse4\\.2")
    message(FATAL_ERROR
        "tools/build-baseline.json no define el baseline SSE4.2 del proyecto "
        "(valor leido: '${ARTPICST_SIMD_FLAG}'). AVX/AVX2/AVX-512 estan prohibidos.")
endif()

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("${ARTPICST_SIMD_FLAG}" ARTPICST_HAS_SSE42)
if(NOT ARTPICST_HAS_SSE42)
    message(FATAL_ERROR
        "El compilador no acepta el baseline SIMD del proyecto (SSE4.2: '${ARTPICST_SIMD_FLAG}').\n"
        "  MSVC: actualiza Visual Studio 2022 a 17.x\n"
        "  GCC/Clang: usa GCC 4.9+ / Clang 3.4+\n"
        "AVX/AVX2/AVX-512 están prohibidos por diseño; no se degrada a SSE2 en silencio.")
endif()
message(STATUS "ARTPICST baseline SIMD: ${ARTPICST_SIMD_FLAG} + ${ARTPICST_SIMD_DEFINE}")

# Aplica el baseline SIMD a un objetivo (compilador + cabecera).
function(artpicst_enable_sse42 target)
    target_compile_options(${target} PRIVATE ${ARTPICST_SIMD_FLAG})
    target_compile_definitions(${target} PRIVATE ${ARTPICST_SIMD_DEFINE})
endfunction()

# ---------------------------------------------------------------------------
# artpicst: visor (GDI+ / Direct2D / WIC / stb_image)
# ---------------------------------------------------------------------------
add_executable(artpicst WIN32
    src/main.cpp
    src/image_core.hpp
    src/updater_client.hpp
    artpicst.manifest
    artpicst.rc
)

# ---------------------------------------------------------------------------
# artpicst_updater: Módulo Inteligente de Actualización Automática (Release
# Sync). Consulta la API de GitHub, notifica, descarga y encadena el
# instalador. Ejecutable independiente con manifiesto asInvoker.
# ---------------------------------------------------------------------------
add_executable(artpicst_updater WIN32
    updater/artpicst_updater.cpp
    updater/artpicst_updater.rc
)
target_include_directories(artpicst_updater PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/installer)

target_include_directories(artpicst PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

artpicst_enable_sse42(artpicst)
artpicst_enable_sse42(artpicst_updater)

# STBIR_NO_AVX / STBIR_NO_AVX2 son el cinturón de seguridad de stb_image_resize2:
# aunque alguien active AVX por error, la cabecera de terceros no lo usará.
target_compile_definitions(artpicst PRIVATE STBIR_NO_AVX STBIR_NO_AVX2)

if(WIN32)
    target_compile_definitions(artpicst PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        STBI_WINDOWS_UTF8
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
    target_link_libraries(artpicst_updater PRIVATE
        gdiplus shell32 shlwapi user32 advapi32 gdi32 ole32 uuid dwmapi winhttp
    )
endif()

if(MSVC)
    set_property(TARGET artpicst PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET artpicst_updater PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(artpicst PRIVATE
        /W4
        /utf-8
        /MP
        /permissive-
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ob3>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Debug>:/Od>
    )
    target_compile_options(artpicst_updater PRIVATE
        /W4
        /utf-8
        /MP
        /permissive-
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/guard:cf>
        $<$<CONFIG:Debug>:/Od>
    )
    # Endurecimiento del binario. NO se usa /fp:fast: rompería la exactitud de
    # las identidades enteras (LUT, /255, BT.601) que fijan los tests.
    set(ARTPICST_HARDENING_LINK /GUARD:CF /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT)
    target_link_options(artpicst PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        ${ARTPICST_HARDENING_LINK}
        "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    )
    target_link_options(artpicst_updater PRIVATE
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        ${ARTPICST_HARDENING_LINK}
        "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
    )
else()
    # MinGW / GCC / Clang: -funroll-loops ayuda al núcleo de imagen; el baseline
    # SIMD lo añade artpicst_enable_sse42(). No se anaden flags de AVX de ningun
    # tipo ni optimizaciones atadas a una microarquitectura concreta de la CPU.
    target_compile_options(artpicst PRIVATE -O3 -funroll-loops -fno-math-errno -Wall -Wextra)
    target_compile_options(artpicst_updater PRIVATE -O2 -Wall -Wextra)
    target_link_options(artpicst_updater PRIVATE -municode)
endif()

# ---------------------------------------------------------------------------
# Tests, fuzzing y banco de kernels
# ---------------------------------------------------------------------------
if(ARTPICST_BUILD_TESTS)
    enable_testing()

    add_executable(image_core_test tests/image_core_test.cpp)
    target_include_directories(image_core_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
    artpicst_enable_sse42(image_core_test)
    target_compile_definitions(image_core_test PRIVATE STBIR_NO_AVX STBIR_NO_AVX2)

    add_executable(kernel_bench tests/kernel_bench.cpp)
    target_include_directories(kernel_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
    artpicst_enable_sse42(kernel_bench)
    target_compile_definitions(kernel_bench PRIVATE STBIR_NO_AVX STBIR_NO_AVX2)

    add_executable(release_json_fuzzer tests/release_json_fuzzer.cpp)
    target_include_directories(release_json_fuzzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(jpeg_exif_test tests/jpeg_exif_test.cpp)
    target_include_directories(jpeg_exif_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    foreach(t image_core_test kernel_bench release_json_fuzzer jpeg_exif_test)
        if(WIN32)
            target_link_libraries(${t} PRIVATE shell32)
        endif()
    endforeach()

    if(MSVC)
        foreach(t image_core_test kernel_bench release_json_fuzzer jpeg_exif_test)
            target_compile_options(${t} PRIVATE /W4 /utf-8 /permissive-)
        endforeach()
    else()
        foreach(t image_core_test kernel_bench release_json_fuzzer jpeg_exif_test)
            target_compile_options(${t} PRIVATE -Wall -Wextra)
        endforeach()
    endif()

    add_test(NAME image_core COMMAND image_core_test)
    add_test(NAME jpeg_exif COMMAND jpeg_exif_test)
    add_test(NAME release_json COMMAND release_json_fuzzer)
endif()

# ---------------------------------------------------------------------------
# Instalador autocontenido (opcional: ARTPICST_PACKAGE=ON)
# ---------------------------------------------------------------------------
if(ARTPICST_PACKAGE)
    set(ARTPICST_PAYLOAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/app")
    set(ARTPICST_PAYLOAD_FILES artpicst.exe artpicst.ico version.json README.md artpicst_updater.exe)

    # Genera resources/app/ a partir de los binarios recién compilados. Antes
    # esto era un FATAL_ERROR de configuración que impedía cualquier build limpio.
    add_custom_command(
        OUTPUT "${ARTPICST_PAYLOAD_DIR}/.payload-stamp"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ARTPICST_PAYLOAD_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:artpicst>" "${ARTPICST_PAYLOAD_DIR}/artpicst.exe"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:artpicst_updater>" "${ARTPICST_PAYLOAD_DIR}/artpicst_updater.exe"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources/artpicst.ico" "${ARTPICST_PAYLOAD_DIR}/artpicst.ico"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/version.json" "${ARTPICST_PAYLOAD_DIR}/version.json"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${ARTPICST_PAYLOAD_DIR}/README.md"
        COMMAND ${CMAKE_COMMAND} -E touch "${ARTPICST_PAYLOAD_DIR}/.payload-stamp"
        DEPENDS artpicst artpicst_updater
        COMMENT "Poblando resources/app/ (payload autocontenido)"
        VERBATIM
    )
    add_custom_target(artpicst_payload DEPENDS "${ARTPICST_PAYLOAD_DIR}/.payload-stamp")

    add_executable(artpicst_installer WIN32
        installer/artpicst_installer.cpp
        installer/version.hpp
        installer/artpicst_installer.rc
    )
    target_include_directories(artpicst_installer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/installer)
    artpicst_enable_sse42(artpicst_installer)
    # El .rc incrusta el payload: el instalador no puede compilarse antes que él.
    add_dependencies(artpicst_installer artpicst_payload)

    if(WIN32)
        target_compile_definitions(artpicst_installer PRIVATE
            UNICODE
            _UNICODE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            _WIN32_WINNT=0x0601
        )
        target_link_libraries(artpicst_installer PRIVATE
            gdiplus shlwapi shell32 comctl32 dwmapi user32 advapi32 gdi32 ole32 uuid
        )
    endif()

    if(MSVC)
        set_property(TARGET artpicst_installer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(artpicst_installer PRIVATE
            /W4 /utf-8 /MP /permissive-
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Release>:/Oi>
            $<$<CONFIG:Release>:/Gy>
            $<$<CONFIG:Release>:/guard:cf>
            $<$<CONFIG:Debug>:/Od>
        )
        # El instalador es TRADICIONAL (Program Files + HKLM): requiere elevación.
        target_link_options(artpicst_installer PRIVATE
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
            /GUARD:CF /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT
            "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
        )
    else()
        target_compile_options(artpicst_installer PRIVATE -O2 -Wall -Wextra)
        target_link_options(artpicst_installer PRIVATE -municode)
    endif()

    install(TARGETS artpicst artpicst_installer artpicst_updater RUNTIME DESTINATION .)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION .)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/artpicst.ico" DESTINATION .)
else()
    message(STATUS
        "ARTPICST_PACKAGE=OFF: el instalador autocontenido no se compila. "
        "Activalo con -DARTPICST_PACKAGE=ON (genera y usa resources/app/).")
    install(TARGETS artpicst artpicst_updater RUNTIME DESTINATION .)
    install(FILES README.md DESTINATION .)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/artpicst.ico" DESTINATION .)
endif()

install(FILES "docs/REFACTOR_C23.md" DESTINATION . OPTIONAL)
