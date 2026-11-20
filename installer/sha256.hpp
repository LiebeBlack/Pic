// ============================================================================
// sha256.hpp — SHA-256 (solo cabecera, sin dependencias de Windows)
// ----------------------------------------------------------------------------
// Implementación única compartida por el instalador (verificación del payload
// incrustado) y el actualizador (verificación del asset descargado). Antes cada
// binario llevaba su propia copia del algoritmo: dos implementaciones de una
// primitiva de seguridad es exactamente lo que no debe duplicarse, porque un
// arreglo en una deja la otra vulnerable.
//
// Uso:
//     std::wstring hex = artpicst::sha256::Hex(data, size);
//     artpicst::sha256::Hasher h; h.Update(a, n); h.Update(b, m); h.FinalHex();
//
// FinalHex() devuelve 64 caracteres hex en MINÚSCULA y consume el estado: solo
// se puede llamar una vez por hasher.
// ============================================================================

#ifndef ARTPICST_SHA256_HPP
#define ARTPICST_SHA256_HPP

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace artpicst {
namespace sha256 {

struct Hasher {
    unsigned int state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    unsigned long long bitLen = 0;
    unsigned char buffer[64] = {};
    size_t bufferLen = 0;

    static unsigned int Rotr(unsigned int x, int n) { return (x >> n) | (x << (32 - n)); }

    void ProcessBlock(const unsigned char* block) {
        static const unsigned int K[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u };
        unsigned int w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<unsigned int>(block[i * 4]) << 24) |
                   (static_cast<unsigned int>(block[i * 4 + 1]) << 16) |
                   (static_cast<unsigned int>(block[i * 4 + 2]) << 8) |
                    static_cast<unsigned int>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const unsigned int s0 = Rotr(w[i-15], 7) ^ Rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            const unsigned int s1 = Rotr(w[i-2], 17) ^ Rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        unsigned int a = state[0], b = state[1], c = state[2], d = state[3];
        unsigned int e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const unsigned int S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            const unsigned int ch = (e & f) ^ (~e & g);
            const unsigned int t1 = h + S1 + ch + K[i] + w[i];
            const unsigned int S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
            const unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
            const unsigned int t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void Update(const void* data, size_t len) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        bitLen += static_cast<unsigned long long>(len) * 8ull;
        while (len > 0) {
            const size_t take = (64 - bufferLen < len) ? 64 - bufferLen : len;
            std::memcpy(buffer + bufferLen, bytes, take);
            bufferLen += take;
            bytes += take;
            len -= take;
            if (bufferLen == 64) {
                ProcessBlock(buffer);
                bufferLen = 0;
            }
        }
    }

    std::wstring FinalHex() {
        const unsigned long long bits = bitLen;
        const unsigned char pad = 0x80;
        Update(&pad, 1);
        const unsigned char zero = 0;
        while (bufferLen != 56) Update(&zero, 1);
        const unsigned char lenBytes[8] = {
            static_cast<unsigned char>(bits >> 56), static_cast<unsigned char>(bits >> 48),
            static_cast<unsigned char>(bits >> 40), static_cast<unsigned char>(bits >> 32),
            static_cast<unsigned char>(bits >> 24), static_cast<unsigned char>(bits >> 16),
            static_cast<unsigned char>(bits >> 8),  static_cast<unsigned char>(bits) };
        Update(lenBytes, 8);
        wchar_t hex[65] = {};
        for (int i = 0; i < 8; ++i) {
            // Forma no cualificada: es la que ya compilaba en MSVC y MinGW.
            swprintf(hex + i * 8, 9, L"%08x", state[i]);
        }
        return hex;
    }
};

// Hash de un bloque en memoria de una sola pasada.
inline std::wstring Hex(const void* data, size_t len) {
    Hasher h;
    h.Update(data, len);
    return h.FinalHex();
}

}  // namespace sha256
}  // namespace artpicst

#endif  // ARTPICST_SHA256_HPP
