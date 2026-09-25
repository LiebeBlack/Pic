# ARTPICST - Guía de Compilación

## Requisitos

- Windows 10/11
- Visual Studio 2019/2022 con C++ (Build Tools o Community/Professional/Enterprise)
- Git (para clonar el repositorio)

## Compilación Rápida

### Opción 1: PowerShell (Recomendado)
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

### Opción 2: Batch
```cmd
build.bat
```

### Opción 3: Manual
Si prefieres compilar cada componente por separado:

#### 1. Programa Principal
```cmd
cd C:\Users\Admin\Documents\GitHub\Pic
rc /nologo /fo build\artpicst.res artpicst.rc
cd installer
rc /nologo /fo ..\build\artpicst_installer.res artpicst_installer.rc
cd ..
cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /GL /Gy /utf-8 /W4 /I. /Iinclude /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSTBI_WINDOWS_UTF8 /D_WIN32_WINNT=0x0601 /Fe:"build\artpicst.exe" src\main.cpp build\artpicst.res /link gdiplus.lib user32.lib kernel32.lib shell32.lib shlwapi.lib gdi32.lib msimg32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowscodecs.lib comdlg32.lib d2d1.lib dwrite.lib /MANIFESTINPUT:artpicst.manifest /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF
```

#### 2. Instalador
```cmd
cd installer
cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /utf-8 /W4 /I. /I..\include /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /Fe:"build\artpicst_installer.exe" artpicst_installer.cpp ..\build\artpicst_installer.res /link gdiplus.lib shlwapi.lib shell32.lib comctl32.lib dwmapi.lib user32.lib advapi32.lib gdi32.lib ole32.lib uuid.lib /SUBSYSTEM:WINDOWS "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'" /OPT:REF /OPT:ICF
```

## Icono multirresolución

El `.ico` del repositorio ya está generado con todas las resoluciones (16 a 256 px),
así que **no hace falta regenerarlo para compilar**. Si cambias el diseño, vuelve a
generarlo (sólo necesita Python; no usa Pillow ni ninguna dependencia externa):

```cmd
python scripts\generate_icon.py            # regenera resources\artpicst.ico
python scripts\generate_icon.py --preview  # + vista previa PNG 256x256
```

## Compilación con MinGW-W64 (alternativa a MSVC)

```cmd
build_mingw.bat
```

Compila con `g++ -std=c++23 -O3 -funroll-loops`. Si `windres` falla con
`gcc: fatal error: cannot execute 'cc1'`, indica el preprocesador explícitamente:

```cmd
windres -i artpicst.rc -o build\artpicst.res -O coff -I. ^
        --preprocessor="gcc -E -xc -DRC_INVOKED"
```

## Archivos Generados

Después de la compilación exitosa, encontrarás los siguientes archivos en el directorio `dist\`:

- `artpicst.exe` - Programa principal (visuales premium y rendimiento optimizado)
- `artpicst_installer.exe` - Instalador con interfaz moderna
- `artpicst.ico` - Icono de la aplicación
- `version.json` - Información de versión

## Mejoras Implementadas

### Interfaz del Programa Principal
- ✅ Efectos glassmorphism/acrylic mejorados
- ✅ Mayor transparencia y blur premium
- ✅ Colores más vibrantes y modernos
- ✅ Zoom de máxima calidad con auto-snap 100%
- ✅ Calidad de renderizado bicúbico fotográfico
- ✅ Navegación por teclado optimizada (↑↓ zoom, ←→ imágenes)
- ✅ Consumo ultra-ligero de memoria (RAM ≤ 80 MB, CPU ~0%)

### Instalador
- ✅ Interfaz gráfica premium con GDI+ (antialiasing, doble búfer, sin parpadeo)
- ✅ Diseño moderno con efectos acrílicos y disposición única de diálogos
- ✅ Asistente de instalación paso a paso, con teclado (Intro/Escape) y progreso visual
- ✅ Instalación **tradicional para todos los usuarios** en `%ProgramFiles%\ARTPICST`
- ✅ Elevación por UAC (manifiesto `requireAdministrator` + relanzado con `runas`),
  con degradación a instalación por usuario si se rechaza
- ✅ Accesos directos en el Menú Inicio (todos los usuarios) y en el Escritorio
- ✅ Detección e instalación silenciosa del **Microsoft Visual C++ Redistributable**
  y registro de las DLL que acompañen al instalador
- ✅ Registro completo: `Uninstall\ARTPICST` (HKLM/HKCU), `App Paths`, ProgID,
  `RegisteredApplications` y asociaciones por `OpenWithProgids` **sin secuestrar**
  el programa predeterminado del usuario (las miniaturas del Explorador se conservan)
- ✅ Desinstalador integrado (`artpicst_installer.exe --uninstall`) con limpieza de
  accesos directos, claves y carpeta, y borrado diferido del propio ejecutable
- ✅ Reversión automática (rollback) si la instalación falla a medias

## Solución de Problemas

### Error: "Visual Studio not found"
Instala Visual Studio Build Tools con C++:
https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022

### Error: "cl not recognized"
Abre "Developer Command Prompt for VS" desde el menú de inicio y ejecuta el script de compilación desde allí.

### Error de compilación
Asegúrate de tener todas las dependencias:
- Windows SDK
- C++ tools
- GDI+ libraries (incluidas en Windows)

## Preparación para GitHub

1. Compila el proyecto usando build.ps1 o build.bat
2. Verifica que los archivos estén en `dist\`
3. Crea un release en GitHub
4. Sube `artpicst_installer.exe` y `artpicst.exe` como assets principales