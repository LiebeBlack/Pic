// ============================================================================
// payload_manifest.hpp — Placeholder de desarrollo (hashes vacíos)
// ----------------------------------------------------------------------------
// El archivo real se GENERA con scripts/gen_payload_manifest.py (invocado por
// scripts/collect_app_payload.py) y contiene el SHA-256 de cada archivo del
// payload incrustado. Este placeholder permite compilar el instalador en
// desarrollos sin payload poblado; el instalador trata los hashes vacíos
// como "sin verificar" y lo registra en su consola de log.
// ============================================================================
#pragma once

#include <cstddef>

namespace artpicst {
namespace manifest {

struct PayloadEntry {
    unsigned    resourceId;      // id RCDATA en artpicst_installer.rc
    const char* fileName;        // nombre en el directorio de instalación
    const char* sha256Hex;       // 64 chars hex, o "" si no hay referencia
};

inline constexpr PayloadEntry kPayload[] = {
    { 201, "artpicst.exe",         "" },   // RES_APP_EXE
    { 202, "artpicst.ico",         "" },   // RES_APP_ICO
    { 203, "version.json",         "" },   // RES_APP_VERSION
    { 204, "README.md",            "" },   // RES_APP_README
    { 205, "artpicst_updater.exe", "" },   // RES_APP_UPDATER
};

inline constexpr std::size_t kPayloadCount = sizeof(kPayload) / sizeof(kPayload[0]);

}  // namespace manifest
}  // namespace artpicst
