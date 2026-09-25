// ============================================================================
// updater_client.hpp — Cliente de actualización para el visor (solo cabecera)
// ----------------------------------------------------------------------------
// Integración quirúrgica con src/main.cpp:
//   · StartBackgroundDailyCheck(): se llama en WM_CREATE. Arranca un hilo
//     secundario que espera 20-30 s (no afecta al arranque) y, si toca
//     (política de 1 comprobación al día), lanza artpicst_updater.exe, que
//     hace la consulta real a GitHub y muestra la notificación flotante.
//   · CheckForUpdatesInteractive(hwnd): entrada de menú "Buscar
//     actualizaciones...". Lanza el updater en modo --forced (avisa con un
//     breve toast si no hay conexión o si ya se está en la última versión).
//
// El hilo termina limpiamente al salir del proceso (hilo daemon de 25 s de
// vida; el cierre del visor no se ve afectado en ningún caso).
// ============================================================================
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>

namespace updater {

constexpr wchar_t kUpdaterExe[] = L"artpicst_updater.exe";
constexpr wchar_t kRegKeyApp[] = L"Software\\ARTPICST";
constexpr wchar_t kRegValLastCheck[] = L"LastUpdateCheck";

inline bool ReadLastCheck(unsigned long long& out) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKeyApp, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = sizeof(out);
    const LSTATUS r = RegQueryValueExW(hKey, kRegValLastCheck, nullptr, &type,
                                       reinterpret_cast<LPBYTE>(&out), &size);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS && type == REG_QWORD && size == sizeof(out);
}

inline void WriteLastCheck(unsigned long long value) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKeyApp, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return;
    const LSTATUS r = RegSetValueExW(hKey, kRegValLastCheck, 0, REG_QWORD,
                                     reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hKey);
    (void)r;
}

inline unsigned long long NowUnix() {
    return static_cast<unsigned long long>(_time64(nullptr));
}

inline bool AlreadyCheckedToday() {
    unsigned long long last = 0;
    if (!ReadLastCheck(last)) return false;
    return NowUnix() - last < 86400ull;   // política: 1 comprobación al día
}

// Lanza artpicst_updater.exe con argumentos. Devuelve true si se inició.
inline bool LaunchUpdaterProcess(const wchar_t* args, HWND hwnd) {
    wchar_t selfDir[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, selfDir, MAX_PATH) == 0) return false;
    if (wchar_t* slash = wcsrchr(selfDir, L'\\')) *slash = L'\0';

    std::wstring path = std::wstring(selfDir) + L"\\" + kUpdaterExe;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Reserva: junto a la raíz de instalación registrada.
        HKEY hKey = nullptr;
        bool found = false;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRegKeyApp, 0, KEY_READ, &hKey) == ERROR_SUCCESS ||
            RegOpenKeyExW(HKEY_CURRENT_USER, kRegKeyApp, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t dir[MAX_PATH] = {};
            DWORD size = sizeof(dir) - sizeof(wchar_t);
            if (RegQueryValueExW(hKey, L"InstallDir", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(dir), &size) == ERROR_SUCCESS && dir[0]) {
                path = std::wstring(dir) + L"\\" + kUpdaterExe;
                found = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
            }
            RegCloseKey(hKey);
        }
        if (!found) return false;
    }

    const HINSTANCE result = ShellExecuteW(hwnd, L"open", path.c_str(), args,
                                           selfDir, SW_SHOWNOACTIVATE);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

// Comprobación diaria en segundo plano: 20-30 s tras arrancar el visor.
inline void StartBackgroundDailyCheck() {
    std::thread([]() {
        // Retraso pseudoaleatorio 20-30 s (xorshift32): reparte la carga y
        // nunca molesta al arranque del visor.
        unsigned int seed = static_cast<unsigned int>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        const int delaySeconds = 20 + static_cast<int>(seed % 11u);
        for (int slept = 0; slept < delaySeconds; ++slept) {
            Sleep(1000);
        }
        if (AlreadyCheckedToday()) return;
        // El propio updater guarda LastUpdateCheck tras consultar GitHub.
        LaunchUpdaterProcess(L"", nullptr);
    }).detach();
}

// Entrada de menú "Buscar actualizaciones...": fuerza la comprobación visible.
inline void CheckForUpdatesInteractive(HWND hwnd) {
    LaunchUpdaterProcess(L"--check", hwnd);
}

} // namespace updater
