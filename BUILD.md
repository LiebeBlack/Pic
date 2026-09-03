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
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /I. /Iinclude /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSTBI_WINDOWS_UTF8 /D_WIN32_WINNT=0x0601 /Fe:"build\artpicst.exe" src\main.cpp artpicst.manifest artpicst.rc /link gdiplus.lib user32.lib kernel32.lib shell32.lib shlwapi.lib gdi32.lib msimg32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowscodecs.lib comdlg32.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF
```

#### 2. Instalador
```cmd
cd installer
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /I. /I..\include /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /Fe:"build\artpicst_installer.exe" artpicst_installer.cpp /link gdiplus.lib shlwapi.lib shell32.lib comctl32.lib dwmapi.lib user32.lib advapi32.lib gdi32.lib ole32.lib uuid.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF
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
- ✅ Interfaz gráfica premium con GDI+
- ✅ Diseño moderno con efectos acrílicos
- ✅ Asistente de instalación paso a paso
- ✅ Progreso visual
- ✅ Instalación real por usuario (sin administrador) en `%LOCALAPPDATA%\Programs\ARTPICST`
- ✅ Copia de archivos, accesos directos en Escritorio y Menú Inicio
- ✅ Registro de asociaciones de archivo (HKCU) y entrada de desinstalación
- ✅ Desinstalador integrado (`artpicst_installer.exe --uninstall`)

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