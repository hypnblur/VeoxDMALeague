#pragma once
#include <windows.h>
#include <dpapi.h>
#include <bcrypt.h>
#include <cstring>
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace SecureMem {

static void Wipe(void* p, size_t n) { SecureZeroMemory(p, n); }

static bool RandomBytes(void* buf, ULONG len) {
    return BCryptGenRandom(NULL, (PUCHAR)buf, len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

// Per-run XOR key — generated once at startup, lives on the heap (not a predictable global).
// Survives the lifetime of the process. Cheap and fast for frequent encrypt/decrypt cycles.
struct RunKey {
    uint8_t key[64];
    bool valid = false;

    void Generate() {
        valid = RandomBytes(key, sizeof(key));
    }

    void XorInPlace(uint8_t* data, size_t len) const {
        if (!valid) return;
        for (size_t i = 0; i < len; i++)
            data[i] ^= key[i % sizeof(key)];
    }
};

// Encrypted buffer: stores data XOR'd with the per-run key.
// Decrypt into a stack buffer, use, then Wipe the stack buffer.
struct EncBuf {
    uint8_t* data = nullptr;
    size_t   len  = 0;
    bool     enc  = false;

    void Alloc(size_t n) {
        Free();
        data = (uint8_t*)VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        len = n;
        enc = false;
    }

    void Free() {
        if (data) {
            Wipe(data, len);
            VirtualFree(data, 0, MEM_RELEASE);
            data = nullptr;
        }
        len = 0; enc = false;
    }

    void Encrypt(const RunKey& rk) {
        if (data && !enc) { rk.XorInPlace(data, len); enc = true; }
    }

    void DecryptTo(const RunKey& rk, uint8_t* out, size_t outsz) const {
        if (!data) return;
        size_t n = (outsz < len) ? outsz : len;
        memcpy(out, data, n);
        if (enc) rk.XorInPlace(out, n);
    }

    ~EncBuf() { Free(); }
};

// DPAPI-based encryption for data that crosses process boundaries (bootstrap pipe).
// Uses CRYPTPROTECT_LOCAL_MACHINE so both loader and product on the same machine can decrypt.
static bool DpapiEncrypt(const uint8_t* plain, DWORD plainLen,
                         uint8_t** cipherOut, DWORD* cipherLen) {
    DATA_BLOB in = { plainLen, (BYTE*)plain };
    DATA_BLOB out = {};
    if (!CryptProtectData(&in, NULL, NULL, NULL, NULL,
                          CRYPTPROTECT_LOCAL_MACHINE, &out))
        return false;
    *cipherOut = (uint8_t*)malloc(out.cbData);
    memcpy(*cipherOut, out.pbData, out.cbData);
    *cipherLen = out.cbData;
    LocalFree(out.pbData);
    return true;
}

static bool DpapiDecrypt(const uint8_t* cipher, DWORD cipherLen,
                         uint8_t** plainOut, DWORD* plainLen) {
    DATA_BLOB in = { cipherLen, (BYTE*)cipher };
    DATA_BLOB out = {};
    if (!CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out))
        return false;
    *plainOut = (uint8_t*)malloc(out.cbData);
    memcpy(*plainOut, out.pbData, out.cbData);
    *plainLen = out.cbData;
    LocalFree(out.pbData);
    return true;
}

// AES-256-CBC encrypt/decrypt using BCrypt (for bootstrap pipe payload).
static bool AesEncrypt(const uint8_t key[32], const uint8_t iv[16],
                       const uint8_t* plain, DWORD plainLen,
                       uint8_t* cipher, DWORD* cipherLen) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE hk = NULL;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0)) return false;
    DWORD bs = 0, tmp = 0;
    BCryptGetProperty(alg, BCRYPT_BLOCK_LENGTH, (PUCHAR)&bs, sizeof(bs), &tmp, 0);
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                      sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (BCryptGenerateSymmetricKey(alg, &hk, NULL, 0, (PUCHAR)key, 32, 0) == 0) {
        uint8_t ivCopy[16]; memcpy(ivCopy, iv, 16);
        DWORD sz = 0;
        BCryptEncrypt(hk, (PUCHAR)plain, plainLen, NULL, ivCopy, 16,
                      NULL, 0, &sz, BCRYPT_BLOCK_PADDING);
        if (sz <= *cipherLen) {
            memcpy(ivCopy, iv, 16);
            ok = (BCryptEncrypt(hk, (PUCHAR)plain, plainLen, NULL, ivCopy, 16,
                                cipher, *cipherLen, cipherLen, BCRYPT_BLOCK_PADDING) == 0);
        }
        BCryptDestroyKey(hk);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

static bool AesDecrypt(const uint8_t key[32], const uint8_t iv[16],
                       const uint8_t* cipher, DWORD cipherLen,
                       uint8_t* plain, DWORD* plainLen) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE hk = NULL;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0)) return false;
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                      sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (BCryptGenerateSymmetricKey(alg, &hk, NULL, 0, (PUCHAR)key, 32, 0) == 0) {
        uint8_t ivCopy[16]; memcpy(ivCopy, iv, 16);
        ok = (BCryptDecrypt(hk, (PUCHAR)cipher, cipherLen, NULL, ivCopy, 16,
                            plain, *plainLen, plainLen, BCRYPT_BLOCK_PADDING) == 0);
        BCryptDestroyKey(hk);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// HMAC-SHA256 (same as the existing product expects)
static bool HmacSha256(const uint8_t* key, DWORD keyLen,
                       const uint8_t* data, DWORD dataLen,
                       uint8_t out[32]) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hh = NULL;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG))
        return false;
    if (BCryptCreateHash(alg, &hh, NULL, 0, (PUCHAR)key, keyLen, 0) == 0) {
        if (BCryptHashData(hh, (PUCHAR)data, dataLen, 0) == 0)
            ok = (BCryptFinishHash(hh, out, 32, 0) == 0);
        BCryptDestroyHash(hh);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

} // namespace SecureMem
