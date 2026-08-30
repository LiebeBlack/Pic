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
#include <winreg.h>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

const wchar_t APP_NAME[] = L"ARTPICST";
const wchar_t GITHUB_OWNER[] = L"LiebeBlack";
const wchar_t GITHUB_REPO[] = L"Pic";
const wchar_t INSTALLER_ASSET_NAME[] = L"artpicst-installer.exe";
const wchar_t INSTALL_REG_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ARTPICST";
const wchar_t CURRENT_VERSION[] = L"1.1.0";

void Log(const wchar_t* msg) {
    OutputDebugStringW((std::wstring(L"[Updater] ") + msg + L"\n").c_str());
}

std::vector<int> NormalizeVersion(const std::wstring& ver_str) {
    std::wstring raw = ver_str;
    for (auto& c : raw) c = towlower(c);
    
    std::vector<int> parts;
    for (size_t i = 0; i < raw.length(); ++i) {
        if (iswdigit(raw[i])) {
            int num = 0;
            while (i < raw.length() && iswdigit(raw[i])) {
                num = num * 10 + (raw[i] - L'0');
                ++i;
            }
            parts.push_back(num);
        }
    }
    
    while (parts.size() < 4) parts.push_back(0);
    return parts;
}

bool IsNewerVersion(const std::wstring& current, const std::wstring& remote) {
    std::vector<int> current_parts = NormalizeVersion(current);
    std::vector<int> remote_parts = NormalizeVersion(remote);
    
    for (size_t i = 0; i < std::min(current_parts.size(), remote_parts.size()); ++i) {
        if (remote_parts[i] > current_parts[i]) return true;
        if (remote_parts[i] < current_parts[i]) return false;
    }
    return false;
}

std::wstring GetInstalledVersion() {
    HKEY hKey = NULL;
    wchar_t version[256] = {0};
    DWORD size = sizeof(version);
    
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, INSTALL_REG_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"DisplayVersion", NULL, NULL, (LPBYTE)version, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::wstring(version);
        }
        RegCloseKey(hKey);
    }
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER, INSTALL_REG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"DisplayVersion", NULL, NULL, (LPBYTE)version, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::wstring(version);
        }
        RegCloseKey(hKey);
    }
    
    return std::wstring(CURRENT_VERSION);
}

std::string HttpGet(const std::wstring& url) {
    std::string result;
    
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    
    if (!WinHttpCrackUrl(url.c_str(), url.length(), 0, &urlComp)) return result;
    
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

struct ReleaseInfo {
    std::wstring tag_name;
    std::wstring installer_url;
};

ReleaseInfo ParseReleaseJson(const std::string& json) {
    ReleaseInfo info;
    
    std::string jsonStr = json;
    
    // Simple JSON parser - extract tag_name
    size_t tagPos = jsonStr.find("\"tag_name\"");
    if (tagPos != std::string::npos) {
        size_t valueStart = jsonStr.find("\"", tagPos + 10);
        if (valueStart != std::string::npos) {
            size_t valueEnd = jsonStr.find("\"", valueStart + 1);
            if (valueEnd != std::string::npos) {
                std::string tagValue = jsonStr.substr(valueStart + 1, valueEnd - valueStart - 1);
                info.tag_name = std::wstring(tagValue.begin(), tagValue.end());
            }
        }
    }
    
    // Find installer URL in assets
    size_t assetsPos = jsonStr.find("\"assets\"");
    if (assetsPos != std::string::npos) {
        size_t installerPos = jsonStr.find(INSTALLER_ASSET_NAME, assetsPos);
        if (installerPos != std::string::npos) {
            size_t urlPos = jsonStr.find("\"browser_download_url\"", installerPos);
            if (urlPos != std::string::npos) {
                size_t urlStart = jsonStr.find("\"", urlPos + 22);
                if (urlStart != std::string::npos) {
                    size_t urlEnd = jsonStr.find("\"", urlStart + 1);
                    if (urlEnd != std::string::npos) {
                        std::string urlValue = jsonStr.substr(urlStart + 1, urlEnd - urlStart - 1);
                        info.installer_url = std::wstring(urlValue.begin(), urlValue.end());
                    }
                }
            }
        }
    }
    
    return info;
}

ReleaseInfo FetchLatestRelease() {
    std::wstring apiUrl = L"https://api.github.com/repos/";
    apiUrl += GITHUB_OWNER;
    apiUrl += L"/";
    apiUrl += GITHUB_REPO;
    apiUrl += L"/releases/latest";
    
    std::string jsonResponse = HttpGet(apiUrl);
    if (jsonResponse.empty()) return ReleaseInfo();
    
    return ParseReleaseJson(jsonResponse);
}

bool DownloadFile(const std::wstring& url, const std::wstring& destPath) {
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    
    if (!WinHttpCrackUrl(url.c_str(), url.length(), 0, &urlComp)) return false;
    
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

void CloseRunningInstances() {
    ShellExecuteW(NULL, L"open", L"taskkill.exe", L"/IM artpicst.exe /T /F", NULL, SW_HIDE);
    Sleep(1000);
}

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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    SetProcessDPIAware();
    
    std::wstring currentVersion = GetInstalledVersion();
    
    ReleaseInfo release = FetchLatestRelease();
    if (release.tag_name.empty()) {
        MessageBoxW(NULL, L"No se pudo conectar con GitHub", APP_NAME, MB_ICONERROR);
        return 1;
    }
    
    if (!IsNewerVersion(currentVersion, release.tag_name)) {
        MessageBoxW(NULL, (L"ARTPICST está actualizado: " + currentVersion).c_str(), APP_NAME, MB_ICONINFORMATION);
        return 0;
    }
    
    if (release.installer_url.empty()) {
        MessageBoxW(NULL, L"No se encontró el instalador en la release", APP_NAME, MB_ICONWARNING);
        return 1;
    }
    
    std::wstring message = L"Nueva versión disponible:\n\n";
    message += L"Actual: " + currentVersion + L"\n";
    message += L"Nueva: " + release.tag_name + L"\n\n";
    message += L"¿Desea instalar la actualización?";
    
    if (MessageBoxW(NULL, message.c_str(), L"Actualización - ARTPICST", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return 0;
    }
    
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring installerPath = std::wstring(tempPath) + INSTALLER_ASSET_NAME;
    
    if (!DownloadFile(release.installer_url, installerPath)) {
        MessageBoxW(NULL, L"No se pudo descargar el instalador", APP_NAME, MB_ICONERROR);
        return 1;
    }
    
    if (RunInstaller(installerPath)) {
        MessageBoxW(NULL, L"Instalador iniciado correctamente", APP_NAME, MB_ICONINFORMATION);
        return 0;
    } else {
        MessageBoxW(NULL, L"No se pudo iniciar el instalador", APP_NAME, MB_ICONERROR);
        return 1;
    }
}