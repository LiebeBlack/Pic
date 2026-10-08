# ============================================================================
# ARTPICST — Makefile (GNU make · Git Bash / MSYS2 / MinGW-w64)
# ----------------------------------------------------------------------------
# Alternativa portable a build.ps1 / build.bat para entornos con MinGW-w64.
# Orden obligatorio: recursos -> visor -> updater -> selftest -> tests ->
#                    payload -> instalador -> dist
# (El instalador incrusta el payload como RCDATA, así que va al final.)
#
# Uso:
#   make            # pipeline completo (equivale a build.ps1 con MinGW)
#   make test       # solo los ejecutables de test + gate de política SIMD
#   make bench      # banco de kernels (baseline SSE4.2)
#   make policy     # gate de política SIMD: sin AVX + identidades enteras
#   make package    # genera resources/app/ y compila el instalador autocontenido
#   make dist       # copia los artefactos a dist/
#   make msvc       # delega en build.ps1 (Visual Studio 2022)
#   make clean
#
# Requisitos: g++ (GCC 13+ con C++23), windres, python3 (solo para pasos
# opcionales).  AVX está PROHIBIDO en todo el proyecto: el baseline es SSE4.2.
# ============================================================================

CXX        ?= g++
RC         ?= windres
PYTHON     ?= python
WINDRES_PP ?= gcc -E -xc -DRC_INVOKED

# ---------------------------------------------------------------------------
# Baseline SIMD del proyecto: SSE4.2 (SSE2/SSSE3/SSE4.1 incluidos).
# No anadas flags de AVX ni de microarquitectura: image_core.hpp y
# tests/check_simd_policy.py los rechazan.
# ---------------------------------------------------------------------------
SIMD := -msse4.2

WARN       := -Wall -Wextra
LINKSTATIC := -static -static-libgcc -static-libstdc++
SYSDEFS    := -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601
STBDEFS    := -DSTBI_WINDOWS_UTF8 -DSTBIR_NO_AVX -DSTBIR_NO_AVX2
SIMDDEF    := -DARTPICST_TARGET_SSE42=1

VIEWER_FLAGS   := -std=c++23 -O3 -funroll-loops -fno-math-errno $(SIMD) $(SIMDDEF) $(WARN) $(LINKSTATIC) -municode $(SYSDEFS) $(STBDEFS) -I. -Iinclude
# OJO con las rutas de inclusión: las reglas 'updater' e 'installer' compilan
# DESDE SU PROPIO DIRECTORIO, así que las rutas de cabeceras son relativas a él
# (-I../installer y -I../include), igual que en build_mingw.bat.
UPDATER_FLAGS  := -std=c++23 -O2 $(SIMD) $(SIMDDEF) $(WARN) $(LINKSTATIC) -municode $(SYSDEFS) -I. -I../installer
INSTALLER_FLAGS:= -std=c++23 -O2 $(SIMD) $(SIMDDEF) $(WARN) $(LINKSTATIC) -municode $(SYSDEFS) -I. -I../include
TEST_FLAGS     := -std=c++20 -O2 $(SIMD) $(SIMDDEF) $(WARN) -I. -Iinclude

VIEWER_LIBS  := -lgdiplus -luser32 -lkernel32 -lshell32 -lshlwapi -lgdi32 -lmsimg32 \
                -lole32 -loleaut32 -luuid -ldwmapi -lwindowscodecs -lcomdlg32 -ld2d1 -ldwrite
UPDATER_LIBS := -lwinhttp -lgdiplus -lshell32 -lshlwapi -luser32 -ladvapi32 -lgdi32 \
                -lole32 -luuid -ldwmapi
INSTALLER_LIBS := -lgdiplus -lshlwapi -lshell32 -lcomctl32 -ldwmapi -luser32 -ladvapi32 \
                  -lgdi32 -lole32 -luuid

BUILD := build
DIST  := dist

.PHONY: all resources viewer updater selftest test policy bench payload installer dist clean msvc help

# ---------------------------------------------------------------------------
# Pipeline completo
# ---------------------------------------------------------------------------
all: resources viewer updater selftest test payload installer dist
	@echo ""
	@echo "========================================"
	@echo "BUILD SUCCESSFUL (MinGW / make)"
	@echo "========================================"
	@ls -l $(DIST) 2>/dev/null || true

$(BUILD) $(DIST) installer/build updater/build:
	@mkdir -p $@

# ---------------------------------------------------------------------------
# Recursos
# ---------------------------------------------------------------------------
resources: | $(BUILD) updater/build
	@echo "[1/6] Recursos (windres)"
	@$(RC) artpicst.rc -O coff -o $(BUILD)/artpicst_res.o -I. --preprocessor="$(WINDRES_PP)"
	@cd updater && $(RC) artpicst_updater.rc -O coff -o ../$(BUILD)/artpicst_updater_res.o -I. --preprocessor="$(WINDRES_PP)"
	@touch $(BUILD)/artpicst_res.o

# ---------------------------------------------------------------------------
# Visor
# ---------------------------------------------------------------------------
viewer: resources
	@echo "[2/6] Visor (artpicst.exe)"
	$(CXX) $(VIEWER_FLAGS) -o $(BUILD)/artpicst.exe src/main.cpp $(BUILD)/artpicst_res.o $(VIEWER_LIBS) -mwindows

# ---------------------------------------------------------------------------
# Updater
# ---------------------------------------------------------------------------
updater: resources
	@echo "[3/6] Updater (artpicst_updater.exe)"
	@cd updater && $(CXX) $(UPDATER_FLAGS) -o ../$(BUILD)/artpicst_updater.exe artpicst_updater.cpp ../$(BUILD)/artpicst_updater_res.o $(UPDATER_LIBS) -mwindows

selftest: updater
	@echo "[3a/6] Selftest del updater"
	@./$(BUILD)/artpicst_updater.exe --selftest

# ---------------------------------------------------------------------------
# Tests, política SIMD y banco de kernels
# ---------------------------------------------------------------------------
$(BUILD)/image_core_test.exe: tests/image_core_test.cpp src/image_core.hpp | $(BUILD)
	$(CXX) $(TEST_FLAGS) -o $@ tests/image_core_test.cpp -lkernel32

$(BUILD)/jpeg_exif_test.exe: tests/jpeg_exif_test.cpp src/jpeg_exif.hpp | $(BUILD)
	$(CXX) $(TEST_FLAGS) -o $@ tests/jpeg_exif_test.cpp -lkernel32 -lshell32

$(BUILD)/release_json_fuzzer.exe: tests/release_json_fuzzer.cpp installer/release_json.hpp | $(BUILD)
	$(CXX) -std=c++20 -O2 $(WARN) -I. -o $@ tests/release_json_fuzzer.cpp -lkernel32

$(BUILD)/kernel_bench.exe: tests/kernel_bench.cpp src/image_core.hpp | $(BUILD)
	$(CXX) $(TEST_FLAGS) -o $@ tests/kernel_bench.cpp -lkernel32

policy:
	@echo "[3b/6] Gate de política SIMD (sin AVX)"
	@$(PYTHON) tests/check_simd_policy.py --root .

test: $(BUILD)/image_core_test.exe $(BUILD)/jpeg_exif_test.exe $(BUILD)/release_json_fuzzer.exe policy
	@echo "[3c/6] Ejecutando tests"
	@./$(BUILD)/image_core_test.exe
	@./$(BUILD)/jpeg_exif_test.exe
	@./$(BUILD)/release_json_fuzzer.exe

bench: $(BUILD)/kernel_bench.exe
	@echo "[bench] Banco de kernels (baseline SSE4.2)"
	@./$(BUILD)/kernel_bench.exe

# ---------------------------------------------------------------------------
# Payload + instalador autocontenido
# ---------------------------------------------------------------------------
payload: viewer updater | $(BUILD)
	@echo "[4/6] Payload autocontenido (resources/app/)"
	@$(PYTHON) scripts/collect_app_payload.py --root .

installer: payload | installer/build
	@echo "[5/6] Instalador autocontenido (artpicst_installer.exe)"
	@cd installer && $(RC) artpicst_installer.rc -O coff -o ../$(BUILD)/artpicst_installer_res.o -I. --preprocessor="$(WINDRES_PP)"
	@cd installer && $(CXX) $(INSTALLER_FLAGS) -o build/artpicst_installer.exe artpicst_installer.cpp ../$(BUILD)/artpicst_installer_res.o $(INSTALLER_LIBS) -mwindows

package: installer

# ---------------------------------------------------------------------------
# Distribución
# ---------------------------------------------------------------------------
dist: installer | $(DIST)
	@echo "[6/6] Copiando artefactos a dist/"
	@cp -f $(BUILD)/artpicst.exe $(DIST)/artpicst.exe
	@cp -f $(BUILD)/artpicst_updater.exe $(DIST)/artpicst_updater.exe
	@cp -f installer/build/artpicst_installer.exe $(DIST)/artpicst_installer.exe
	@cp -f resources/artpicst.ico $(DIST)/artpicst.ico
	@cp -f version.json $(DIST)/version.json
	@cp -f README.md $(DIST)/README.md
	@echo "El asset oficial de release es 'artpicst-installer.exe'."

# ---------------------------------------------------------------------------
# Delegación a MSVC
# ---------------------------------------------------------------------------
msvc:
	@echo "Delegando en build.ps1 (Visual Studio 2022, baseline SSE4.2)..."
	powershell -ExecutionPolicy Bypass -File build.ps1

clean:
	@echo "Limpiando artefactos de build..."
	@rm -rf $(BUILD) $(DIST) installer/build updater/build resources/app
	@rm -f installer/build/artpicst_installer.exe

help:
	@echo "Objetivos: all · test · bench · policy · package · dist · msvc · clean"
	@echo "Baseline SIMD: $(SIMD)  (AVX prohibido en todo el proyecto)"
