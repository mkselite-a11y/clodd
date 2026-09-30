// Moonfall Mayhem native library.
//
// Loaded by MM Recomp next to moonfall_mayhem.nrm. It gives the mod two things
// the recompiled game code can't do on its own:
//   1. Talk to the friend server over HTTPS (WinHTTP, on a background thread).
//   2. Read and write the mod's save file.
//
// Every exported function uses the recomp calling convention:
//   void func(uint8_t* rdram, recomp_context* ctx)
// Arguments arrive in a0-a3 (ctx->r[4..7]) as N64 virtual addresses, and the
// return value goes in v0 (ctx->r[2]). N64 memory is stored byteswapped per
// 32-bit word, so single bytes live at (address ^ 3).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#define MFN_EXPORT __declspec(dllexport)
#else
#include <pthread.h>
#include <unistd.h>
#define MFN_EXPORT __attribute__((visibility("default")))
#endif

MFN_EXPORT uint32_t recomp_api_version = 1;

typedef struct {
    uint64_t r[32]; // general purpose registers; the rest of the context is unused here
} RecompContext;

#define ARG(ctx, i) ((uint32_t)(ctx)->r[4 + (i)])
#define RETURN(ctx, v) ((ctx)->r[2] = (uint64_t)(int64_t)(int32_t)(v))
#define KSEG0 0x80000000u

static uint8_t GetByte(uint8_t* rdram, uint32_t vaddr) {
    return rdram[((vaddr ^ 3u) - KSEG0)];
}

static void SetByte(uint8_t* rdram, uint32_t vaddr, uint8_t v) {
    rdram[((vaddr ^ 3u) - KSEG0)] = v;
}

static void ReadString(uint8_t* rdram, uint32_t vaddr, char* out, size_t max) {
    size_t i = 0;
    if (vaddr == 0) {
        out[0] = '\0';
        return;
    }
    while (i + 1 < max) {
        char c = (char)GetByte(rdram, vaddr + (uint32_t)i);
        if (c == '\0') {
            break;
        }
        out[i++] = c;
    }
    out[i] = '\0';
}

static uint32_t* Words(uint8_t* rdram, uint32_t vaddr) {
    return (uint32_t*)(rdram + (vaddr - KSEG0));
}

// ---------------------------------------------------------------------------
// Threading primitives
// ---------------------------------------------------------------------------

#ifdef _WIN32
static CRITICAL_SECTION gLock;
#define LOCK() EnterCriticalSection(&gLock)
#define UNLOCK() LeaveCriticalSection(&gLock)
#else
static pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&gLock)
#define UNLOCK() pthread_mutex_unlock(&gLock)
#endif

static int gInitialized = 0;
static volatile int gRunning = 0;
static volatile int gStatus = 0; // 0 off, 1 connecting, 2 connected, 3 server error, 4 bad key, 5 unsupported

static char gUrl[512];
static char gRoom[64];
static char gKey[128];
static int gEnabled = 0;

#define STATE_MAX (64 * 1024)
static char* gState = NULL;
static size_t gStateLen = 0;

#define QUEUE_LEN 64
#define LINE_MAX_LEN 256
static char gQueue[QUEUE_LEN][LINE_MAX_LEN];
static int gQueueHead = 0;
static int gQueueCount = 0;

static char gLogPath[1024];

static void LogLine(const char* msg) {
    FILE* f;
    if (gLogPath[0] == '\0') {
        return;
    }
    f = fopen(gLogPath, "a");
    if (f != NULL) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

static void Enqueue(const char* line) {
    size_t n = strlen(line);
    if (n == 0) {
        return;
    }
    LOCK();
    if (gQueueCount < QUEUE_LEN) {
        int slot = (gQueueHead + gQueueCount) % QUEUE_LEN;
        strncpy(gQueue[slot], line, LINE_MAX_LEN - 1);
        gQueue[slot][LINE_MAX_LEN - 1] = '\0';
        gQueueCount++;
    }
    UNLOCK();
}

// Only letters, digits, '-' and '_' are allowed in room codes.
static void CleanRoom(const char* in, char* out, size_t max) {
    size_t n = 0;
    while (*in != '\0' && n + 1 < max) {
        char c = *in++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out[n++] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
    }
    out[n] = '\0';
}

// Response format: first line "OK" (or "ERR ..."), then one command per line.
static void HandleResponse(int httpStatus, const char* body) {
    const char* p = body;
    char line[LINE_MAX_LEN];

    if (httpStatus == 401 || httpStatus == 403) {
        gStatus = 4;
        return;
    }
    if (httpStatus != 200 || strncmp(body, "OK", 2) != 0) {
        gStatus = 3;
        return;
    }
    gStatus = 2;
    while (*p != '\0' && *p != '\n') {
        p++;
    }
    while (*p == '\n' || *p == '\r') {
        p++;
    }
    while (*p != '\0') {
        size_t n = 0;
        while (*p != '\0' && *p != '\n' && *p != '\r') {
            if (n + 1 < sizeof(line)) {
                line[n++] = *p;
            }
            p++;
        }
        line[n] = '\0';
        Enqueue(line);
        while (*p == '\n' || *p == '\r') {
            p++;
        }
    }
}

// ---------------------------------------------------------------------------
// HTTP (Windows: WinHTTP handles TLS, proxies and certificates for us)
// ---------------------------------------------------------------------------

#ifdef _WIN32
static HINTERNET gSession = NULL;

static int HttpPost(const char* url, const char* path, const char* key, const char* body, size_t bodyLen,
                    char* out, size_t outMax, int* status) {
    wchar_t wurl[600];
    wchar_t host[256];
    wchar_t basePath[512];
    wchar_t fullPath[700];
    wchar_t wpath[256];
    wchar_t headers[300];
    URL_COMPONENTS uc;
    HINTERNET hConnect = NULL;
    HINTERNET hReq = NULL;
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    size_t total = 0;
    int ok = 0;

    *status = 0;
    out[0] = '\0';
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 600);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 256);

    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = basePath;
    uc.dwUrlPathLength = 512;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
        LogLine("could not parse server URL");
        return 0;
    }
    // Strip a trailing slash from the base path, then append the API path.
    if (uc.dwUrlPathLength > 0 && basePath[uc.dwUrlPathLength - 1] == L'/') {
        basePath[uc.dwUrlPathLength - 1] = L'\0';
    }
    _snwprintf(fullPath, 700, L"%ls%ls", basePath, wpath);
    fullPath[699] = L'\0';

    if (gSession == NULL) {
        gSession = WinHttpOpen(L"MoonfallMayhem/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
        if (gSession == NULL) {
            return 0;
        }
        WinHttpSetTimeouts(gSession, 4000, 4000, 4000, 6000);
    }
    hConnect = WinHttpConnect(gSession, host, uc.nPort, 0);
    if (hConnect == NULL) {
        goto done;
    }
    hReq = WinHttpOpenRequest(hConnect, L"POST", fullPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                              (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    if (hReq == NULL) {
        goto done;
    }
    {
        wchar_t wkey[130];
        MultiByteToWideChar(CP_UTF8, 0, key, -1, wkey, 130);
        _snwprintf(headers, 300, L"Content-Type: application/json\r\nX-Host-Key: %ls\r\n", wkey);
        headers[299] = L'\0';
    }
    if (!WinHttpSendRequest(hReq, headers, (DWORD)-1L, (LPVOID)body, (DWORD)bodyLen, (DWORD)bodyLen, 0)) {
        goto done;
    }
    if (!WinHttpReceiveResponse(hReq, NULL)) {
        goto done;
    }
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
    *status = (int)statusCode;
    for (;;) {
        DWORD avail = 0;
        DWORD got = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail) || avail == 0) {
            break;
        }
        if (total + avail >= outMax) {
            avail = (DWORD)(outMax - total - 1);
        }
        if (avail == 0 || !WinHttpReadData(hReq, out + total, avail, &got) || got == 0) {
            break;
        }
        total += got;
    }
    out[total] = '\0';
    ok = 1;

done:
    if (hReq != NULL) {
        WinHttpCloseHandle(hReq);
    }
    if (hConnect != NULL) {
        WinHttpCloseHandle(hConnect);
    }
    return ok;
}

static void SleepMs(int ms) {
    Sleep(ms);
}
#else
// Non-Windows builds keep saving working but have no network client.
static int HttpPost(const char* url, const char* path, const char* key, const char* body, size_t bodyLen,
                    char* out, size_t outMax, int* status) {
    (void)url; (void)path; (void)key; (void)body; (void)bodyLen; (void)outMax;
    out[0] = '\0';
    *status = 0;
    gStatus = 5;
    return 0;
}

static void SleepMs(int ms) {
    usleep((useconds_t)ms * 1000);
}
#endif

static void NetLoop(void) {
    static char body[STATE_MAX];
    static char response[32 * 1024];
    char url[512];
    char room[64];
    char key[128];
    char path[128];
    int lastLoggedStatus = -1;

    while (gRunning) {
        int enabled;
        size_t len;
        int httpStatus = 0;

        SleepMs(1000);
        LOCK();
        enabled = gEnabled;
        strcpy(url, gUrl);
        CleanRoom(gRoom, room, sizeof(room));
        strcpy(key, gKey);
        len = gStateLen;
        if (gState != NULL && len > 0) {
            memcpy(body, gState, len);
        }
        body[len] = '\0';
        UNLOCK();

        if (!enabled || url[0] == '\0' || room[0] == '\0') {
            gStatus = 0;
            continue;
        }
        if (len == 0) {
            strcpy(body, "{}");
            len = 2;
        }
        if (gStatus == 0) {
            gStatus = 1;
        }
        snprintf(path, sizeof(path), "/api/room/%s/game", room);
        if (!HttpPost(url, path, key, body, len, response, sizeof(response), &httpStatus)) {
            if (gStatus != 5) {
                gStatus = 3;
            }
        } else {
            HandleResponse(httpStatus, response);
        }
        if (gStatus != lastLoggedStatus) {
            char msg[128];
            snprintf(msg, sizeof(msg), "network status %d (http %d)", gStatus, httpStatus);
            LogLine(msg);
            lastLoggedStatus = gStatus;
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI NetThread(LPVOID arg) {
    (void)arg;
    NetLoop();
    return 0;
}
#else
static void* NetThread(void* arg) {
    (void)arg;
    NetLoop();
    return NULL;
}
#endif

static void FindLogPath(void) {
#ifdef _WIN32
    HMODULE self = NULL;
    char dir[MAX_PATH];
    char* slash;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&FindLogPath, &self);
    if (GetModuleFileNameA(self, dir, MAX_PATH) == 0) {
        return;
    }
    slash = strrchr(dir, '\\');
    if (slash != NULL) {
        *slash = '\0';
    }
    snprintf(gLogPath, sizeof(gLogPath), "%s\\moonfall_net.log", dir);
#else
    snprintf(gLogPath, sizeof(gLogPath), "moonfall_net.log");
#endif
    {
        FILE* f = fopen(gLogPath, "w");
        if (f != NULL) {
            fprintf(f, "Moonfall Mayhem native library started\n");
            fclose(f);
        }
    }
}

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

MFN_EXPORT void mfn_init(uint8_t* rdram, RecompContext* ctx) {
    (void)rdram;
    if (!gInitialized) {
#ifdef _WIN32
        InitializeCriticalSection(&gLock);
#endif
        gState = (char*)malloc(STATE_MAX);
        gStateLen = 0;
        FindLogPath();
        gRunning = 1;
#ifdef _WIN32
        CreateThread(NULL, 0, NetThread, NULL, 0, NULL);
#else
        {
            pthread_t t;
            pthread_create(&t, NULL, NetThread, NULL);
            pthread_detach(t);
        }
#endif
        gInitialized = 1;
    }
    RETURN(ctx, 1);
}

// mfn_configure(const char* url, const char* room, const char* key, s32 enabled)
MFN_EXPORT void mfn_configure(uint8_t* rdram, RecompContext* ctx) {
    char url[512];
    char room[64];
    char key[128];
    if (!gInitialized) {
        return;
    }
    ReadString(rdram, ARG(ctx, 0), url, sizeof(url));
    ReadString(rdram, ARG(ctx, 1), room, sizeof(room));
    ReadString(rdram, ARG(ctx, 2), key, sizeof(key));
    LOCK();
    strcpy(gUrl, url);
    strcpy(gRoom, room);
    strcpy(gKey, key);
    gEnabled = (int)ARG(ctx, 3);
    UNLOCK();
}

// mfn_push_state(const char* json)
MFN_EXPORT void mfn_push_state(uint8_t* rdram, RecompContext* ctx) {
    uint32_t vaddr = ARG(ctx, 0);
    size_t n = 0;
    if (!gInitialized || gState == NULL) {
        return;
    }
    LOCK();
    while (n + 1 < STATE_MAX) {
        char c = (char)GetByte(rdram, vaddr + (uint32_t)n);
        if (c == '\0') {
            break;
        }
        gState[n++] = c;
    }
    gState[n] = '\0';
    gStateLen = n;
    UNLOCK();
}

// s32 mfn_poll_command(char* out, s32 maxLen): returns the length written, 0 if none.
MFN_EXPORT void mfn_poll_command(uint8_t* rdram, RecompContext* ctx) {
    uint32_t out = ARG(ctx, 0);
    int maxLen = (int)ARG(ctx, 1);
    char line[LINE_MAX_LEN];
    int n = 0;
    int i;

    line[0] = '\0';
    if (gInitialized) {
        LOCK();
        if (gQueueCount > 0) {
            strcpy(line, gQueue[gQueueHead]);
            gQueueHead = (gQueueHead + 1) % QUEUE_LEN;
            gQueueCount--;
        }
        UNLOCK();
    }
    n = (int)strlen(line);
    if (n > maxLen - 1) {
        n = maxLen - 1;
    }
    for (i = 0; i < n; i++) {
        SetByte(rdram, out + (uint32_t)i, (uint8_t)line[i]);
    }
    if (maxLen > 0) {
        SetByte(rdram, out + (uint32_t)n, 0);
    }
    RETURN(ctx, n);
}

MFN_EXPORT void mfn_status(uint8_t* rdram, RecompContext* ctx) {
    (void)rdram;
    RETURN(ctx, gStatus);
}

static FILE* OpenUtf8(const char* path, const char* mode) {
#ifdef _WIN32
    wchar_t wpath[1024];
    wchar_t wmode[8];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024);
    MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8);
    return _wfopen(wpath, wmode);
#else
    return fopen(path, mode);
#endif
}

static int ReplaceFile8(const char* tmp, const char* dst) {
#ifdef _WIN32
    wchar_t wtmp[1024];
    wchar_t wdst[1024];
    MultiByteToWideChar(CP_UTF8, 0, tmp, -1, wtmp, 1024);
    MultiByteToWideChar(CP_UTF8, 0, dst, -1, wdst, 1024);
    return MoveFileExW(wtmp, wdst, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(tmp, dst) == 0;
#endif
}

// s32 mfn_save_blob(const char* path, void* words, s32 numWords)
MFN_EXPORT void mfn_save_blob(uint8_t* rdram, RecompContext* ctx) {
    char path[1000];
    char tmp[1024];
    uint32_t* words = Words(rdram, ARG(ctx, 1));
    int count = (int)ARG(ctx, 2);
    FILE* f;
    int ok;

    ReadString(rdram, ARG(ctx, 0), path, sizeof(path));
    if (path[0] == '\0' || count <= 0) {
        RETURN(ctx, 0);
        return;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = OpenUtf8(tmp, "wb");
    if (f == NULL) {
        LogLine("could not open save file for writing");
        RETURN(ctx, 0);
        return;
    }
    // Words are stored exactly as the game sees them, so no byte swapping is needed.
    ok = fwrite(words, sizeof(uint32_t), (size_t)count, f) == (size_t)count;
    fclose(f);
    if (ok) {
        ok = ReplaceFile8(tmp, path);
    }
    RETURN(ctx, ok);
}

// s32 mfn_load_blob(const char* path, void* words, s32 numWords)
MFN_EXPORT void mfn_load_blob(uint8_t* rdram, RecompContext* ctx) {
    char path[1000];
    uint32_t* words = Words(rdram, ARG(ctx, 1));
    int count = (int)ARG(ctx, 2);
    FILE* f;
    size_t got;

    ReadString(rdram, ARG(ctx, 0), path, sizeof(path));
    f = OpenUtf8(path, "rb");
    if (f == NULL || count <= 0) {
        if (f != NULL) {
            fclose(f);
        }
        RETURN(ctx, 0);
        return;
    }
    got = fread(words, sizeof(uint32_t), (size_t)count, f);
    fclose(f);
    RETURN(ctx, got == (size_t)count);
}

// void mfn_log(const char* msg)
MFN_EXPORT void mfn_log(uint8_t* rdram, RecompContext* ctx) {
    char msg[512];
    ReadString(rdram, ARG(ctx, 0), msg, sizeof(msg));
    LogLine(msg);
}
