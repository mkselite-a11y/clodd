// GlobalEventsRemote.dll: the Global Events mod's link to the internet.
//
// The game can't make network calls itself, so this native library does it
// on a background thread: every second it asks the relay (a Cloudflare
// Worker) for new commands from your friend's control panel, and every couple
// of seconds it uploads a status snapshot the panel shows. The mod calls in
// once per frame to hand over status and take commands; those calls never
// wait on the network.
//
// Config: GlobalEventsRemote.txt next to this DLL (in the mods folder):
//   url=https://your-worker.your-name.workers.dev
//   key=your room password
//
// Built with llvm-mingw:
//   x86_64-w64-mingw32-gcc -O2 -shared -o GlobalEventsRemote.dll ge_remote.c -lwinhttp

#include <windows.h>
#include <winhttp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <io.h>

// --- Recomp native library interface ----------------------------------------

__declspec(dllexport) uint32_t recomp_api_version = 1;

// Only the general registers are used here; they come first in the real struct.
typedef struct {
    uint64_t r0, r1, r2, r3, r4, r5, r6, r7;
} RecompCtx;

// The game's RAM is stored with each 32-bit word byte-swapped.
static uint8_t* RamByte(uint8_t* rdram, uint64_t addr, uint32_t i) {
    uint32_t offset = (uint32_t)addr - 0x80000000u + i;

    return &rdram[offset ^ 3];
}

static void SetReturn(RecompCtx* ctx, int32_t value) {
    ctx->r2 = (uint64_t)(int64_t)value;
}

// --- Shared state (game thread <-> network thread) ---------------------------

#define QUEUE_LEN 32
#define CMD_LEN 128
#define STATUS_LEN 2048

enum { ST_NO_CONFIG = 0, ST_CONNECTING = 1, ST_OK = 2, ST_BAD_KEY = 3, ST_NET_ERROR = 4, ST_BAD_URL = 5 };

static CRITICAL_SECTION sLock;
static int sStarted = 0;
static volatile LONG sState = ST_NO_CONFIG;

static char sQueue[QUEUE_LEN][CMD_LEN];
static int sQueueHead = 0;
static int sQueueCount = 0;

static char sStatus[STATUS_LEN];
static int sStatusLen = 0;
static int sStatusDirty = 0;
static ULONGLONG sStatusTick = 0; // when the mod last handed us a status

static wchar_t sHost[256];
static wchar_t sBasePath[256];
static INTERNET_PORT sPort = 443;
static int sSecure = 1;
static char sKey[128];


// --- Config ------------------------------------------------------------------

static void Trim(char* s);
static int sBadUrl = 0;

// "  Key = value " -> key lowercased into k, value trimmed into v. Returns 0 for comments/blank.
static int SplitLine(char* line, char* k, int kmax, char* v, int vmax) {
    char* eq;
    int i;

    Trim(line);
    if ((line[0] == '#') || (line[0] == ';') || (line[0] == '\0')) {
        return 0;
    }
    eq = strchr(line, '=');
    if (eq == NULL) {
        return 0;
    }
    *eq = '\0';
    Trim(line);
    for (i = 0; (line[i] != '\0') && (i < kmax - 1); i++) {
        k[i] = (char)(((line[i] >= 'A') && (line[i] <= 'Z')) ? (line[i] + 32) : line[i]);
    }
    k[i] = '\0';
    strncpy(v, eq + 1, vmax - 1);
    v[vmax - 1] = '\0';
    Trim(v);
    // Drop surrounding quotes.
    if ((v[0] == '"') && (strlen(v) >= 2) && (v[strlen(v) - 1] == '"')) {
        v[strlen(v) - 1] = '\0';
        memmove(v, v + 1, strlen(v));
    }
    return 1;
}

static void Trim(char* s) {
    size_t n = strlen(s);

    while ((n > 0) && ((s[n - 1] == '\r') || (s[n - 1] == '\n') || (s[n - 1] == ' ') || (s[n - 1] == '\t'))) {
        s[--n] = '\0';
    }
    while ((*s == ' ') || (*s == '\t')) {
        memmove(s, s + 1, strlen(s));
    }
}

static int LoadConfig(void) {
    wchar_t path[MAX_PATH];
    HMODULE self = NULL;
    wchar_t* slash;
    FILE* f;
    char line[512];
    char url[512] = { 0 };
    URL_COMPONENTS parts;
    wchar_t wurl[512];

    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&LoadConfig, &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    slash = wcsrchr(path, L'\\');
    if (slash != NULL) {
        slash[1] = L'\0';
    }
    wcscat(path, L"GlobalEventsRemote.txt");

    f = _wfopen(path, L"r");
    if (f == NULL) {
        // Leave a template to fill in.
        f = _wfopen(path, L"w");
        if (f != NULL) {
            fputs("# Global Events friend link. Fill these in, then restart the game.\n"
                  "# url: your Cloudflare Worker address. key: the ROOM_KEY you set in the worker.\n"
                  "url=\n"
                  "key=\n",
                  f);
            fclose(f);
        }
        return 0;
    }
    sKey[0] = '\0';
    while (fgets(line, sizeof(line), f) != NULL) {
        char k[32];
        char v[512];
        char* p = line;

        // Notepad may save a UTF-8 marker at the start of the file.
        if (((unsigned char)p[0] == 0xEF) && ((unsigned char)p[1] == 0xBB) && ((unsigned char)p[2] == 0xBF)) {
            p += 3;
        }
        if (!SplitLine(p, k, sizeof(k), v, sizeof(v))) {
            continue;
        }
        if (strcmp(k, "url") == 0) {
            strncpy(url, v, sizeof(url) - 1);
        } else if ((strcmp(k, "key") == 0) || (strcmp(k, "room_key") == 0) || (strcmp(k, "password") == 0)) {
            strncpy(sKey, v, sizeof(sKey) - 1);
        }
    }
    fclose(f);
    if ((url[0] == '\0') || (sKey[0] == '\0') || (strstr(url, "YOUR-") != NULL)) {
        return 0;
    }
    // "moons-hand.name.workers.dev" works too.
    if ((strncmp(url, "http://", 7) != 0) && (strncmp(url, "https://", 8) != 0)) {
        char full[512];

        snprintf(full, sizeof(full), "https://%s", url);
        strncpy(url, full, sizeof(url) - 1);
    }
    // Drop a trailing slash.
    if (url[strlen(url) - 1] == '/') {
        url[strlen(url) - 1] = '\0';
    }
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 512);
    memset(&parts, 0, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = sHost;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = sBasePath;
    parts.dwUrlPathLength = 256;
    if (!WinHttpCrackUrl(wurl, 0, 0, &parts) || (sHost[0] == L'\0')) {
        sBadUrl = 1;
        return 0;
    }
    sPort = parts.nPort;
    sSecure = (parts.nScheme == INTERNET_SCHEME_HTTPS);
    return 1;
}

// --- HTTP ----------------------------------------------------------------------

static HINTERNET sSession = NULL;
static HINTERNET sConnect = NULL;

// Returns the HTTP status (0 on a network error). Response body goes in out.
static int Http(const wchar_t* verb, const wchar_t* path, const char* body, int bodyLen, char* out, int outMax) {
    HINTERNET req;
    wchar_t full[512];
    wchar_t header[200];
    DWORD status = 0;
    DWORD size = sizeof(status);
    int got = 0;

    if (sConnect == NULL) {
        return 0;
    }
    _snwprintf(full, 512, L"%ls%ls", sBasePath, path);
    req = WinHttpOpenRequest(sConnect, verb, full, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             sSecure ? WINHTTP_FLAG_SECURE : 0);
    if (req == NULL) {
        return 0;
    }
    _snwprintf(header, 200, L"X-Room-Key: %hs\r\nContent-Type: text/plain\r\n", sKey);
    if (!WinHttpSendRequest(req, header, (DWORD)-1L, (LPVOID)body, bodyLen, bodyLen, 0) ||
        !WinHttpReceiveResponse(req, NULL)) {
        WinHttpCloseHandle(req);
        return 0;
    }
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (out != NULL) {
        for (;;) {
            DWORD avail = 0;
            DWORD read = 0;

            if (!WinHttpQueryDataAvailable(req, &avail) || (avail == 0)) {
                break;
            }
            if (got + (int)avail >= outMax) {
                avail = (DWORD)(outMax - 1 - got);
            }
            if ((avail == 0) || !WinHttpReadData(req, out + got, avail, &read) || (read == 0)) {
                break;
            }
            got += (int)read;
        }
        out[got] = '\0';
    }
    WinHttpCloseHandle(req);
    return (int)status;
}

// --- Network thread ----------------------------------------------------------

static int Enqueue(const char* line) {
    int ok = 0;

    EnterCriticalSection(&sLock);
    if (sQueueCount < QUEUE_LEN) {
        int slot = (sQueueHead + sQueueCount) % QUEUE_LEN;

        strncpy(sQueue[slot], line, CMD_LEN - 1);
        sQueue[slot][CMD_LEN - 1] = '\0';
        sQueueCount++;
        ok = 1;
    }
    LeaveCriticalSection(&sLock);
    return ok;
}

static DWORD WINAPI NetThread(LPVOID unused) {
    static char resp[8192];
    static char body[STATUS_LEN];
    long long cursor = -1; // -1: skip anything sent before the game started
    ULONGLONG lastPost = 0;

    (void)unused;
    sSession = WinHttpOpen(L"GlobalEventsRemote/1.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (sSession == NULL) { // before Windows 8.1
        sSession = WinHttpOpen(L"GlobalEventsRemote/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (sSession != NULL) {
        WinHttpSetTimeouts(sSession, 5000, 5000, 5000, 5000);
        sConnect = WinHttpConnect(sSession, sHost, sPort, 0);
    }
    for (;;) {
        wchar_t path[128];
        int code;
        int idle = 0;    // nobody has the panel open
        int linkOff = 0; // Friend Link switched off in the game
        ULONGLONG now = GetTickCount64();
        int fresh;
        int len = 0;

        // Commands
        _snwprintf(path, 128, L"/api/pull?after=%lld", cursor);
        code = Http(L"GET", path, NULL, 0, resp, sizeof(resp));
        if (code == 200) {
            char* line = strtok(resp, "\n");
            long long newCursor = cursor;
            int full = 0;

            InterlockedExchange(&sState, ST_OK);
            while (line != NULL) {
                if (strncmp(line, "cursor|", 7) == 0) {
                    newCursor = _atoi64(line + 7);
                } else if (strncmp(line, "idle|", 5) == 0) {
                    idle = (line[5] == '1');
                } else if ((line[0] != '\0') && !full) {
                    if (Enqueue(line)) {
                        cursor = _atoi64(line); // "id|..." : this one is ours now
                    } else {
                        full = 1; // queue full: the rest come again next time
                    }
                }
                line = strtok(NULL, "\n");
            }
            if (!full && (newCursor > cursor)) {
                cursor = newCursor;
            }
        } else {
            InterlockedExchange(&sState, (code == 401) ? ST_BAD_KEY : ST_NET_ERROR);
        }

        // Status: only while the game is actually handing us fresh ones, so the
        // panel shows "offline" if the game stops (title screen, a hang, closed).
        EnterCriticalSection(&sLock);
        fresh = (sStatusLen > 0) && (now - sStatusTick < 5000);
        if (fresh) {
            memcpy(body, sStatus, sStatusLen);
            len = sStatusLen;
        }
        linkOff = (strncmp(sStatus, "link=0", 6) == 0);
        LeaveCriticalSection(&sLock);
        if (fresh && (now - lastPost >= (ULONGLONG)((idle || linkOff) ? 8000 : 2000))) {
            Http(L"POST", L"/api/status", body, len, NULL, 0);
            lastPost = now;
        }
        // Ease off when nobody's watching or the link is off.
        Sleep(((code != 200) || idle || linkOff) ? 4000 : 1000);
    }
    return 0;
}

// --- Exports called by the mod ------------------------------------------------

// s32 ger_init(void): 1 = running, 0 = no config yet (a template was written).
__declspec(dllexport) void ger_init(uint8_t* rdram, RecompCtx* ctx) {
    (void)rdram;
    if (sStarted) {
        SetReturn(ctx, (sState == ST_NO_CONFIG) ? 0 : 1);
        return;
    }
    sStarted = 1;
    InitializeCriticalSection(&sLock);
    if (!LoadConfig()) {
        sState = sBadUrl ? ST_BAD_URL : ST_NO_CONFIG;
        SetReturn(ctx, 0);
        return;
    }
    sState = ST_CONNECTING;
    CreateThread(NULL, 0, NetThread, NULL, 0, NULL);
    SetReturn(ctx, 1);
}

// s32 ger_state(void): 0 no config, 1 connecting, 2 connected, 3 wrong key, 4 network error, 5 bad url.
__declspec(dllexport) void ger_state(uint8_t* rdram, RecompCtx* ctx) {
    (void)rdram;
    SetReturn(ctx, (int32_t)sState);
}

// s32 ger_poll(char* out, u32 max): copies the next command (without newline) and
// returns its length, or 0 if there's nothing waiting.
__declspec(dllexport) void ger_poll(uint8_t* rdram, RecompCtx* ctx) {
    uint64_t out = ctx->r4;
    uint32_t max = (uint32_t)ctx->r5;
    char line[CMD_LEN];
    int len = 0;
    uint32_t i;

    if (!sStarted || (max < 2)) {
        SetReturn(ctx, 0);
        return;
    }
    EnterCriticalSection(&sLock);
    if (sQueueCount > 0) {
        strncpy(line, sQueue[sQueueHead], CMD_LEN);
        sQueueHead = (sQueueHead + 1) % QUEUE_LEN;
        sQueueCount--;
        len = (int)strlen(line);
    }
    LeaveCriticalSection(&sLock);
    if (len >= (int)max) {
        len = (int)max - 1;
    }
    for (i = 0; i < (uint32_t)len; i++) {
        *RamByte(rdram, out, i) = (uint8_t)line[i];
    }
    *RamByte(rdram, out, (uint32_t)len) = 0;
    SetReturn(ctx, len);
}

// void ger_status(const char* text, u32 len): the latest status snapshot.
__declspec(dllexport) void ger_status(uint8_t* rdram, RecompCtx* ctx) {
    uint64_t in = ctx->r4;
    uint32_t len = (uint32_t)ctx->r5;
    uint32_t i;

    if (!sStarted) {
        return;
    }
    if (len >= STATUS_LEN) {
        len = STATUS_LEN - 1;
    }
    EnterCriticalSection(&sLock);
    for (i = 0; i < len; i++) {
        sStatus[i] = (char)*RamByte(rdram, in, i);
    }
    sStatus[len] = '\0';
    sStatusLen = (int)len;
    sStatusDirty = 1;
    sStatusTick = GetTickCount64();
    LeaveCriticalSection(&sLock);
}


// ---------------------------------------------------------------------------
// Auto-save: small key=value store in GlobalEventsSaves.txt next to this DLL
// (one line per game file). Only the game thread calls these.
// ---------------------------------------------------------------------------

#define STORE_MAX 64
#define STORE_KEY 32
#define STORE_VAL 128

static char sStoreKeys[STORE_MAX][STORE_KEY];
static char sStoreVals[STORE_MAX][STORE_VAL];
static int sStoreCount = 0;
static int sStoreLoaded = 0; // 1: read (or there's no file yet). 0: couldn't read it: never write over it.

static void StorePath(wchar_t* path, const wchar_t* name) {
    HMODULE self = NULL;
    wchar_t* slash;

    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&StorePath, &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    slash = wcsrchr(path, L'\\');
    if (slash != NULL) {
        slash[1] = L'\0';
    }
    wcscat(path, name);
}

static void StoreLoad(void) {
    wchar_t path[MAX_PATH];
    FILE* f;
    char line[STORE_KEY + STORE_VAL + 8];
    int first = 1;

    sStoreCount = 0;
    StorePath(path, L"GlobalEventsSaves.txt");
    f = _wfopen(path, L"r");
    if (f == NULL) {
        // No file yet is fine. A file we can't open (locked by a sync tool or antivirus)
        // is not: leave it alone and try again next time.
        sStoreLoaded = (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) ? 1 : 0;
        return;
    }
    sStoreLoaded = 1;
    while ((sStoreCount < STORE_MAX) && (fgets(line, sizeof(line), f) != NULL)) {
        char* p = line;
        char* eq;
        size_t n;

        // Notepad may put a UTF-8 marker at the start.
        if (first && ((unsigned char)p[0] == 0xEF) && ((unsigned char)p[1] == 0xBB) && ((unsigned char)p[2] == 0xBF)) {
            p += 3;
        }
        first = 0;
        eq = strchr(p, '=');
        if ((p[0] == '#') || (eq == NULL)) {
            continue;
        }
        memmove(line, p, strlen(p) + 1);
        eq = strchr(line, '=');
        *eq = '\0';
        n = strcspn(eq + 1, "\r\n");
        eq[1 + n] = '\0';
        if ((strlen(line) >= STORE_KEY) || (n >= STORE_VAL)) {
            continue;
        }
        strcpy(sStoreKeys[sStoreCount], line);
        strcpy(sStoreVals[sStoreCount], eq + 1);
        sStoreCount++;
    }
    fclose(f);
}

static void StoreWrite(void) {
    wchar_t path[MAX_PATH];
    wchar_t tmp[MAX_PATH];
    FILE* f;
    int i;

    StorePath(path, L"GlobalEventsSaves.txt");
    StorePath(tmp, L"GlobalEventsSaves.tmp");
    if (!sStoreLoaded) {
        return; // never replace a file we couldn't read
    }
    f = _wfopen(tmp, L"w");
    if (f == NULL) {
        return;
    }
    fputs("# Global Events: Moon Marks progress for each game file (written automatically).\n", f);
    for (i = 0; i < sStoreCount; i++) {
        fprintf(f, "%s=%s\n", sStoreKeys[i], sStoreVals[i]);
    }
    // Only replace the real file once the new one is safely on disk.
    if ((fflush(f) != 0) || ferror(f) || (_commit(_fileno(f)) != 0)) {
        fclose(f);
        DeleteFileW(tmp);
        return;
    }
    if (fclose(f) != 0) {
        DeleteFileW(tmp);
        return;
    }
    MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

// Copies a zero-terminated string out of game RAM.
static void RamString(uint8_t* rdram, uint64_t addr, char* out, int max) {
    int i;

    for (i = 0; i < max - 1; i++) {
        out[i] = (char)*RamByte(rdram, addr, (uint32_t)i);
        if (out[i] == '\0') {
            return;
        }
    }
    out[max - 1] = '\0';
}

// void ger_store(const char* key, const char* value)
__declspec(dllexport) void ger_store(uint8_t* rdram, RecompCtx* ctx) {
    char key[STORE_KEY];
    char val[STORE_VAL];
    int i;

    if (!sStoreLoaded) {
        StoreLoad();
    }
    if (!sStoreLoaded) {
        return; // the file exists but can't be read right now
    }
    RamString(rdram, ctx->r4, key, sizeof(key));
    RamString(rdram, ctx->r5, val, sizeof(val));
    if ((key[0] == '\0') || strchr(key, '=') || strchr(key, '\n') || strchr(val, '\n')) {
        return;
    }
    for (i = 0; i < sStoreCount; i++) {
        if (strcmp(sStoreKeys[i], key) == 0) {
            break;
        }
    }
    if ((i < sStoreCount) && (strcmp(sStoreVals[i], val) == 0)) {
        return; // nothing changed
    }
    if (i < sStoreCount) {
        // Move it to the end: the list is kept oldest-used first.
        memmove(sStoreKeys[i], sStoreKeys[i + 1], sizeof(sStoreKeys[0]) * (sStoreCount - i - 1));
        memmove(sStoreVals[i], sStoreVals[i + 1], sizeof(sStoreVals[0]) * (sStoreCount - i - 1));
        sStoreCount--;
    } else if (sStoreCount == STORE_MAX) {
        // Full: drop the one used longest ago.
        memmove(sStoreKeys[0], sStoreKeys[1], sizeof(sStoreKeys[0]) * (STORE_MAX - 1));
        memmove(sStoreVals[0], sStoreVals[1], sizeof(sStoreVals[0]) * (STORE_MAX - 1));
        sStoreCount--;
    }
    i = sStoreCount++;
    strcpy(sStoreKeys[i], key);
    strcpy(sStoreVals[i], val);
    StoreWrite();
}

// s32 ger_fetch(const char* key, char* out, u32 max): copies the stored value, returns its length
// (0 = nothing stored for this key, -1 = the save file exists but couldn't be read).
__declspec(dllexport) void ger_fetch(uint8_t* rdram, RecompCtx* ctx) {
    char key[STORE_KEY];
    uint64_t out = ctx->r5;
    uint32_t max = (uint32_t)ctx->r6;
    int i;
    uint32_t k;

    if (!sStoreLoaded) {
        StoreLoad();
    }
    if (!sStoreLoaded) {
        if (max >= 1) {
            *RamByte(rdram, out, 0) = 0;
        }
        SetReturn(ctx, -1);
        return;
    }
    RamString(rdram, ctx->r4, key, sizeof(key));
    for (i = 0; i < sStoreCount; i++) {
        if (strcmp(sStoreKeys[i], key) == 0) {
            uint32_t len = (uint32_t)strlen(sStoreVals[i]);

            if (max < 1) {
                break;
            }
            if (len > max - 1) {
                len = max - 1;
            }
            for (k = 0; k < len; k++) {
                *RamByte(rdram, out, k) = (uint8_t)sStoreVals[i][k];
            }
            *RamByte(rdram, out, len) = 0;
            SetReturn(ctx, (int32_t)len);
            return;
        }
    }
    if (max >= 1) {
        *RamByte(rdram, out, 0) = 0;
    }
    SetReturn(ctx, 0);
}
