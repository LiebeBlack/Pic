#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <comdef.h>
#include <comutil.h>
#include <winreg.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <regex>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comsuppw.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

const wchar_t APP_NAME[] = L"ARTPICST";
const wchar_t GITHUB_OWNER[] = L"LiebeBlack";
const wchar_t GITHUB_REPO[] = L"Pic";
const wchar_t INSTALLER_ASSET_NAME[] = L"artpicst-installer.exe";
const wchar_t INSTALL_REG_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ARTPICST";
const wchar_t CURRENT_VERSION[] = L"auto-28";

// Estructura para información de release
struct ReleaseInfo {
    std::wstring tag_name;
    std::wstring name;
    std::wstring body;
    std::wstring installer_url;
};

// Función para convertir std::string a std::wstring
std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Función para convertir std::wstring a std::string
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Función de logging
void Log(const wchar_t* msg) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timestamp[64];
    swprintf_s(timestamp, L"[%04u-%02u-%02u %02u:%02u:%02u]", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    OutputDebugStringW((std::wstring(timestamp) + L" [ARTPICST Updater] " + msg + L"\n").c_str());
}

// Normalizar versión para comparación
std::vector<int> NormalizeVersion(const std::wstring& ver_str) {
    std::wstring raw = ver_str;
    std::transform(raw.begin(), raw.end(), raw.begin(), ::towlower);
    
    // Eliminar prefijos como "auto-", "release-", "v"
    static const std::wregex prefix_pattern(L"^(auto|release|v)[-_]?");
    raw = std::regex_replace(raw, prefix_pattern, L"");
    
    // Extraer números
    std::vector<int> parts;
    std::wregex num_pattern(L"\\d+");
    std::wsmatch match;
    
    std::wstring::const_iterator searchStart = raw.begin();
    while (std::regex_search(searchStart, raw.cend(), match, num_pattern)) {
        parts.push_back(std::stoi(match[0].str()));
        searchStart = match.suffix().first;
    }
    
    // Asegurar al menos 4 componentes
    while (parts.size() < 4) {
        parts.push_back(0);
    }
    
    return parts;
}

// Comparar versiones
bool IsNewerVersion(const std::wstring& current, const std::wstring& remote) {
    std::vector<int> current_parts = NormalizeVersion(current);
    std::vector<int> remote_parts = NormalizeVersion(remote);
    
    for (size_t i = 0; i < std::min(current_parts.size(), remote_parts.size()); ++i) {
        if (remote_parts[i] > current_parts[i]) return true;
        if (remote_parts[i] < current_parts[i]) return false;
    }
    
    return false;
}

// Obtener versión instalada desde registro
std::wstring GetInstalledVersion() {
    HKEY hKey = NULL;
    
    // Intentar HKLM primero
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, INSTALL_REG_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        wchar_t version[256] = {0};
        DWORD size = sizeof(version);
        DWORD type = REG_SZ;
        if (RegQueryValueExW(hKey, L"DisplayVersion", NULL, &type, (LPBYTE)version, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::wstring(version);
        }
        RegCloseKey(hKey);
    }
    
    // Intentar HKCU
    if (RegOpenKeyExW(HKEY_CURRENT_USER, INSTALL_REG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t version[256] = {0};
        DWORD size = sizeof(version);
        DWORD type = REG_SZ;
        if (RegQueryValueExW(hKey, L"DisplayVersion", NULL, &type, (LPBYTE)version, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::wstring(version);
        }
        RegCloseKey(hKey);
    }
    
    return std::wstring(CURRENT_VERSION);
}

// HTTP Request simple usando WinHTTP
std::string HttpGet(const std::wstring& url) {
    std::string result;
    
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;
    
    if (!WinHttpCrackUrl(url.c_str(), url.length(), 0, &urlComp)) {
        return result;
    }
    
    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    
    HINTERNET hSession = WinHttpOpen(L"ARTPICST-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;
    
    HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    
    // Headers personalizados
    std::wstring headers = L"User-Agent: ARTPICST-Updater/1.0\r\nAccept: application/vnd.github.v3+json\r\n";
    WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    
    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }
    
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, &buffer[0], bytesAvailable, &bytesRead)) {
            buffer[bytesRead] = '\0';
            result.append(buffer.data(), bytesRead);
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return result;
}

// Parse JSON response (parser simple)
ReleaseInfo ParseReleaseJson(const std::string& json) {
    ReleaseInfo info;
    
    // Parser JSON muy simplificado para este caso específico
    std::string jsonStr = json;
    
    // Extraer tag_name
    size_t tagPos = jsonStr.find("\"tag_name\"");
    if (tagPos != std::string::npos) {
        size_t valueStart = jsonStr.find("\"", tagPos + 10);
        if (valueStart != std::string::npos) {
            size_t valueEnd = jsonStr.find("\"", valueStart + 1);
            if (valueEnd != std::string::npos) {
                info.tag_name = StringToWString(jsonStr.substr(valueStart + 1, valueEnd - valueStart - 1));
            }
        }
    }
    
    // Extraer name
    size_t namePos = jsonStr.find("\"name\"");
    if (namePos != std::string::npos) {
        size_t valueStart = jsonStr.find("\"", namePos + 6);
        if (valueStart != std::string::npos) {
            size_t valueEnd = jsonStr.find("\"", valueStart + 1);
            if (valueEnd != std::string::npos) {
                info.name = StringToWString(jsonStr.substr(valueStart + 1, valueEnd - valueStart - 1));
            }
        }
    }
    
    // Extraer body
    size_t bodyPos = jsonStr.find("\"body\"");
    if (bodyPos != std::string::npos) {
        size_t valueStart = jsonStr.find("\"", bodyPos + 6);
        if (valueStart != std::string::npos) {
            size_t valueEnd = jsonStr.find("\"", valueStart + 1);
            if (valueEnd != std::string::npos) {
                info.body = StringToWString(jsonStr.substr(valueStart + 1, valueEnd - valueStart - 1));
            }
        }
    }
    
    // Extraer assets para encontrar el instalador
    size_t assetsPos = jsonStr.find("\"assets\"");
    if (assetsPos != std::string::npos) {
        size_t assetsArrayStart = jsonStr.find("[", assetsPos);
        if (assetsArrayStart != std::string::npos) {
            size_t assetsArrayEnd = jsonStr.find("]", assetsArrayStart);
            if (assetsArrayEnd != std::string::npos) {
                std::string assetsArray = jsonStr.substr(assetsArrayStart, assetsArrayEnd - assetsArrayStart + 1);
                
                std::wstring installerNameLower = INSTALLER_ASSET_NAME;
                std::transform(installerNameLower.begin(), installerNameLower.end(), installerNameLower.begin(), ::towlower);
                
                // Buscar cada asset
                size_t assetPos = 0;
                while ((assetPos = assetsArray.find("\"name\"", assetPos)) != std::string::npos) {
                    size_t nameStart = assetsArray.find("\"", assetPos + 6);
                    if (nameStart != std::string::npos) {
                        size_t nameEnd = assetsArray.find("\"", nameStart + 1);
                        if (nameEnd != std::string::npos) {
                            std::string assetName = assetsArray.substr(nameStart + 1, nameEnd - nameStart - 1);
                            std::wstring assetNameW = StringToWString(assetName);
                            std::transform(assetNameW.begin(), assetNameW.end(), assetNameW.begin(), ::towlower);
                            
                            if (assetNameW == installerNameLower) {
                                // Encontrar browser_download_url
                                size_t urlPos = assetsArray.find("\"browser_download_url\"", nameEnd);
                                if (urlPos != std::string::npos) {
                                    size_t urlStart = assetsArray.find("\"", urlPos + 22);
                                    if (urlStart != std::string::npos) {
                                        size_t urlEnd = assetsArray.find("\"", urlStart + 1);
                                        if (urlEnd != std::string::npos) {
                                            info.installer_url = StringToWString(assetsArray.substr(urlStart + 1, urlEnd - urlStart - 1));
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    assetPos = nameEnd + 1;
                }
            }
        }
    }
    
    return info;
}

// Obtener última release desde GitHub
ReleaseInfo FetchLatestRelease() {
    std::wstring apiUrl = L"https://api.github.com/repos/";
    apiUrl += GITHUB_OWNER;
    apiUrl += L"/";
    apiUrl += GITHUB_REPO;
    apiUrl += L"/releases/latest";
    
    std::string jsonResponse = HttpGet(apiUrl);
    if (jsonResponse.empty()) {
        return ReleaseInfo();
    }
    
    return ParseReleaseJson(jsonResponse);
}

// Descargar archivo
bool DownloadFile(const std::wstring& url, const std::wstring& destPath) {
    std::wstring urlLower = url;
    std::transform(urlLower.begin(), urlLower.end(), urlLower.begin(), ::towlower);
    
    // Extraer host y path de la URL
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;
    
    if (!WinHttpCrackUrl(url.c_str(), url.length(), 0, &urlComp)) {
        return false;
    }
    
    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    
    HINTERNET hSession = WinHttpOpen(L"ARTPICST-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    
    HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    std::wstring headers = L"User-Agent: ARTPICST-Updater/1.0\r\n";
    WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    
    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Crear archivo de destino
    HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, &buffer[0], bytesAvailable, &bytesRead)) {
            DWORD bytesWritten = 0;
            WriteFile(hFile, buffer.data(), bytesRead, &bytesWritten, NULL);
        }
    }
    
    CloseHandle(hFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return true;
}

// Cerrar instancias en ejecución
void CloseRunningInstances() {
    ShellExecuteW(NULL, L"open", L"taskkill.exe", L"/IM artpicst.exe /T /F", NULL, SW_HIDE);
    Sleep(1000);
}

// Ejecutar instalador
bool RunInstaller(const std::wstring& installerPath) {
    CloseRunningInstances();
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = NULL;
    sei.lpVerb = L"open";
    sei.lpFile = installerPath.c_str();
    sei.lpParameters = L"/S";
    sei.nShow = SW_SHOW;
    
    return ShellExecuteExW(&sei);
}

// Función principal
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // Habilitar DPI awareness
    SetProcessDPIAware();
    
    Log(L"Iniciando comprobación de actualizaciones...");
    
    std::wstring currentVersion = GetInstalledVersion();
    Log((L"Versión instalada: " + currentVersion).c_str());
    
    ReleaseInfo release = FetchLatestRelease();
    if (release.tag_name.empty()) {
        Log(L"No se pudo obtener información de la release");
        MessageBoxW(NULL, L"No se pudo conectar con los servidores de actualización.", APP_NAME, MB_ICONERROR);
        return 1;
    }
    
    Log((L"Última versión en GitHub: " + release.tag_name).c_str());
    
    if (!IsNewerVersion(currentVersion, release.tag_name)) {
        Log(L"La aplicación está actualizada");
        MessageBoxW(NULL, (L"ARTPICST está actualizado a la versión " + currentVersion).c_str(), APP_NAME, MB_ICONINFORMATION);
        return 0;
    }
    
    if (release.installer_url.empty()) {
        Log(L"No se encontró el instalador en la release");
        MessageBoxW(NULL, L"La nueva versión no contiene el instalador automático.", APP_NAME, MB_ICONWARNING);
        return 1;
    }
    
    // Preguntar al usuario
    std::wstring message = L"Se encontró una nueva versión:\n\n";
    message += L"Versión actual: " + currentVersion + L"\n";
    message += L"Nueva versión: " + release.tag_name + L"\n\n";
    message += L"¿Desea descargar e instalar la actualización?";
    
    int result = MessageBoxW(NULL, message.c_str(), L"Actualización disponible - ARTPICST", MB_YESNO | MB_ICONQUESTION);
    if (result != IDYES) {
        Log(L"Usuario canceló la actualización");
        return 0;
    }
    
    // Descargar instalador
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring installerPath = std::wstring(tempPath) + INSTALLER_ASSET_NAME;
    
    Log(L"Descargando instalador...");
    if (!DownloadFile(release.installer_url, installerPath)) {
        Log(L"Error al descargar el instalador");
        MessageBoxW(NULL, L"No se pudo descargar el instalador.", APP_NAME, MB_ICONERROR);
        return 1;
    }
    
    Log(L"Instalador descargado correctamente");
    
    // Ejecutar instalador
    if (RunInstaller(installerPath)) {
        Log(L"Instalador iniciado correctamente");
        MessageBoxW(NULL, L"El instalador se iniciará automáticamente.", APP_NAME, MB_ICONINFORMATION);
        return 0;
    } else {
        Log(L"Error al iniciar el instalador");
        MessageBoxW(NULL, L"No se pudo iniciar el instalador.", APP_NAME, MB_ICONERROR);
        return 1;
    }
}