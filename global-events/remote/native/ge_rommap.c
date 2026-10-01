// Maps of every area for the friends' radar and map, made once from the
// player's own ROM (the copy MM Recompiled keeps), then uploaded to their
// relay. Nothing from the ROM ever goes in the release: only these coarse
// height grids, and only to the player's relay.
//
// Included by ge_remote.c.

#include "ge_scenes.h"

#define DMADATA_US 0x1A500
#define MAP_CELL 128
#define MAP_BLOCK (16 * MAP_CELL)

static uint8_t* sRom = NULL;
static uint32_t sRomLen = 0;

static uint32_t BE32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t BE16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

// Reads a ROM in any of the three byte orders and keeps it if it's US Majora's Mask.
static int RomLoad(const wchar_t* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size;
    uint8_t* buf;
    DWORD got = 0;
    uint32_t i;

    if (f == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!GetFileSizeEx(f, &size) || (size.QuadPart < 0x1000000) || (size.QuadPart > 0x4000000)) {
        CloseHandle(f);
        return 0;
    }
    buf = (uint8_t*)malloc((size_t)size.QuadPart);
    if ((buf == NULL) || !ReadFile(f, buf, (DWORD)size.QuadPart, &got, NULL) || (got != (DWORD)size.QuadPart)) {
        CloseHandle(f);
        free(buf);
        return 0;
    }
    CloseHandle(f);
    if ((buf[0] == 0x37) && (buf[1] == 0x80)) { // .v64: bytes swapped in pairs
        for (i = 0; i + 1 < got; i += 2) {
            uint8_t t = buf[i];

            buf[i] = buf[i + 1];
            buf[i + 1] = t;
        }
    } else if ((buf[0] == 0x40) && (buf[1] == 0x12)) { // .n64: words reversed
        for (i = 0; i + 3 < got; i += 4) {
            uint8_t a = buf[i], b = buf[i + 1];

            buf[i] = buf[i + 3];
            buf[i + 1] = buf[i + 2];
            buf[i + 2] = b;
            buf[i + 3] = a;
        }
    }
    if ((buf[0] != 0x80) || (memcmp(buf + 0x20, "ZELDA MAJORA'S MASK", 19) != 0) || (buf[0x3E] != 'E')) {
        free(buf);
        return 0;
    }
    free(sRom);
    sRom = buf;
    sRomLen = got;
    return 1;
}

// Any ROM-looking file in a folder (and its subfolders, one level down).
static int RomSearchDir(const wchar_t* dir, int depth) {
    static const wchar_t* sExt[] = { L"*.z64", L"*.n64", L"*.v64" };
    wchar_t pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int e;

    for (e = 0; e < 3; e++) {
        _snwprintf(pattern, MAX_PATH, L"%ls\\%ls", dir, sExt[e]);
        h = FindFirstFileW(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            wchar_t full[MAX_PATH];

            _snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            if (RomLoad(full)) {
                FindClose(h);
                return 1;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (depth > 0) {
        _snwprintf(pattern, MAX_PATH, L"%ls\\*", dir);
        h = FindFirstFileW(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (fd.cFileName[0] != L'.')) {
                    wchar_t sub[MAX_PATH];

                    _snwprintf(sub, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
                    if (RomSearchDir(sub, depth - 1)) {
                        FindClose(h);
                        return 1;
                    }
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    return 0;
}

// rom= from the .txt, then MM Recompiled's own folder, then around the mods folder.
static int RomFind(void) {
    wchar_t dir[MAX_PATH];
    wchar_t* slash;
    DWORD n;
    int up;

    if ((sRomPath[0] != L'\0') && RomLoad(sRomPath)) {
        return 1;
    }
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if ((n > 0) && (n < MAX_PATH - 40)) {
        wcscat(dir, L"\\Zelda64Recompiled");
        if (RomSearchDir(dir, 1)) {
            return 1;
        }
    }
    DllDirFile(dir, L"");
    for (up = 0; up < 3; up++) {
        n = (DWORD)wcslen(dir);
        while ((n > 0) && (dir[n - 1] == L'\\')) {
            dir[--n] = L'\0';
        }
        slash = wcsrchr(dir, L'\\');
        if (slash == NULL) {
            break;
        }
        *slash = L'\0';
        if (RomSearchDir(dir, 0)) {
            return 1;
        }
    }
    return 0;
}

// Yaz0, the ROM's compression.
static uint8_t* Yaz0(const uint8_t* src, uint32_t srcMax, uint32_t* outLen) {
    uint32_t size;
    uint8_t* out;
    uint32_t sp = 16;
    uint32_t dp = 0;

    if ((srcMax < 16) || (memcmp(src, "Yaz0", 4) != 0)) {
        return NULL;
    }
    size = BE32(src + 4);
    if ((size == 0) || (size > 0x400000)) {
        return NULL;
    }
    out = (uint8_t*)malloc(size);
    if (out == NULL) {
        return NULL;
    }
    while ((dp < size) && (sp < srcMax)) {
        uint8_t code = src[sp++];
        int bit;

        for (bit = 0; (bit < 8) && (dp < size) && (sp < srcMax); bit++, code <<= 1) {
            if (code & 0x80) {
                out[dp++] = src[sp++];
            } else {
                uint32_t b1, b2, dist, count;

                if (sp + 1 >= srcMax) {
                    break;
                }
                b1 = src[sp++];
                b2 = src[sp++];
                dist = ((b1 & 0xF) << 8 | b2) + 1;
                if ((b1 >> 4) == 0) {
                    if (sp >= srcMax) {
                        break;
                    }
                    count = src[sp++] + 0x12;
                } else {
                    count = (b1 >> 4) + 2;
                }
                if (dist > dp) {
                    free(out);
                    return NULL;
                }
                while ((count-- > 0) && (dp < size)) {
                    out[dp] = out[dp - dist];
                    dp++;
                }
            }
        }
    }
    *outLen = size;
    return out;
}

// A file from the ROM's file table, unpacked (free() it after).
static uint8_t* RomFile(uint32_t index, uint32_t* outLen) {
    const uint8_t* e = sRom + DMADATA_US + index * 16;
    uint32_t vStart, vEnd, pStart, pEnd;
    uint8_t* out;

    if (DMADATA_US + (index + 1) * 16 > sRomLen) {
        return NULL;
    }
    vStart = BE32(e);
    vEnd = BE32(e + 4);
    pStart = BE32(e + 8);
    pEnd = BE32(e + 12);
    if ((vEnd <= vStart) || (pStart >= sRomLen) || (pStart == 0xFFFFFFFF)) {
        return NULL;
    }
    if (pEnd == 0) { // stored as is
        if (pStart + (vEnd - vStart) > sRomLen) {
            return NULL;
        }
        out = (uint8_t*)malloc(vEnd - vStart);
        if (out != NULL) {
            memcpy(out, sRom + pStart, vEnd - vStart);
            *outLen = vEnd - vStart;
        }
        return out;
    }
    if ((pEnd > sRomLen) || (pEnd <= pStart)) {
        return NULL;
    }
    return Yaz0(sRom + pStart, pEnd - pStart, outLen);
}

typedef struct {
    float ax, ay, az, bx, by, bz, cx, cy, cz;
    float minX, maxX, minZ, maxZ;
} Tri;

// Height of the triangle at (x, z) if the point is inside it (XZ only).
static int TriHeight(const Tri* t, float x, float z, float* y) {
    float d = (t->bz - t->cz) * (t->ax - t->cx) + (t->cx - t->bx) * (t->az - t->cz);
    float u;
    float v;
    float w;

    if ((d > -0.0001f) && (d < 0.0001f)) {
        return 0;
    }
    u = ((t->bz - t->cz) * (x - t->cx) + (t->cx - t->bx) * (z - t->cz)) / d;
    v = ((t->cz - t->az) * (x - t->cx) + (t->ax - t->cx) * (z - t->cz)) / d;
    w = 1.0f - u - v;
    if ((u < -0.001f) || (v < -0.001f) || (w < -0.001f)) {
        return 0;
    }
    *y = u * t->ay + v * t->by + w * t->cy;
    return 1;
}

typedef struct {
    float minX, maxX, minZ, maxZ, y;
} Water;

static int CmpFloat(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;

    return (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
}

// Turns one scene's floor into "scene|bx|bz|base|cells" lines (appended to out).
static int SceneToBlocks(uint16_t sceneId, uint32_t dmaIndex, char** out, size_t* outLen, size_t* outCap) {
    uint32_t len = 0;
    uint8_t* scene = RomFile(dmaIndex, &len);
    uint32_t off;
    uint32_t col = 0;
    uint32_t nVtx, vtxOff, nPoly, polyOff, nWater, waterOff;
    Tri* tris;
    int nTri = 0;
    Water* water = NULL;
    int nWat = 0;
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    int bx0, bx1, bz0, bz1, bx, bz;
    uint32_t i;
    int blocks = 0;

    if (scene == NULL) {
        return 0;
    }
    // Scene header commands: 0x03 points at the collision, 0x14 ends the list.
    for (off = 0; (off + 8 <= len) && (off < 0x200); off += 8) {
        if (scene[off] == 0x14) {
            break;
        }
        if (scene[off] == 0x03) {
            col = BE32(scene + off + 4) & 0xFFFFFF;
        }
    }
    if ((col == 0) || (col + 0x2C > len)) {
        free(scene);
        return 0;
    }
    nVtx = BE16(scene + col + 0x0C);
    vtxOff = BE32(scene + col + 0x10) & 0xFFFFFF;
    nPoly = BE16(scene + col + 0x14);
    polyOff = BE32(scene + col + 0x18) & 0xFFFFFF;
    nWater = BE16(scene + col + 0x24);
    waterOff = BE32(scene + col + 0x28) & 0xFFFFFF;
    if ((vtxOff + nVtx * 6 > len) || (polyOff + nPoly * 16 > len)) {
        free(scene);
        return 0;
    }
    tris = (Tri*)malloc(sizeof(Tri) * (nPoly + 1));
    if (tris == NULL) {
        free(scene);
        return 0;
    }
    for (i = 0; i < nPoly; i++) {
        const uint8_t* p = scene + polyOff + i * 16;
        uint32_t ia = BE16(p + 2) & 0x1FFF, ib = BE16(p + 4) & 0x1FFF, ic = BE16(p + 6) & 0x1FFF;
        int16_t ny = (int16_t)BE16(p + 0xA);
        Tri* t;

        if ((ny < 0x3000) || (ia >= nVtx) || (ib >= nVtx) || (ic >= nVtx)) {
            continue; // walls, ceilings and steep slopes aren't ground
        }
        t = &tris[nTri++];
#define VX(k, o) ((float)(int16_t)BE16(scene + vtxOff + (k) * 6 + (o)))
        t->ax = VX(ia, 0), t->ay = VX(ia, 2), t->az = VX(ia, 4);
        t->bx = VX(ib, 0), t->by = VX(ib, 2), t->bz = VX(ib, 4);
        t->cx = VX(ic, 0), t->cy = VX(ic, 2), t->cz = VX(ic, 4);
#undef VX
        t->minX = min(t->ax, min(t->bx, t->cx));
        t->maxX = max(t->ax, max(t->bx, t->cx));
        t->minZ = min(t->az, min(t->bz, t->cz));
        t->maxZ = max(t->az, max(t->bz, t->cz));
        minX = min(minX, t->minX);
        maxX = max(maxX, t->maxX);
        minZ = min(minZ, t->minZ);
        maxZ = max(maxZ, t->maxZ);
    }
    if ((nWater > 0) && (waterOff + nWater * 16 <= len)) {
        water = (Water*)malloc(sizeof(Water) * nWater);
        for (i = 0; (water != NULL) && (i < nWater); i++) {
            const uint8_t* w = scene + waterOff + i * 16;

            water[nWat].minX = (float)(int16_t)BE16(w);
            water[nWat].y = (float)(int16_t)BE16(w + 2);
            water[nWat].minZ = (float)(int16_t)BE16(w + 4);
            water[nWat].maxX = water[nWat].minX + (float)(int16_t)BE16(w + 6);
            water[nWat].maxZ = water[nWat].minZ + (float)(int16_t)BE16(w + 8);
            nWat++;
        }
    }
    if (nTri == 0) {
        free(tris);
        free(water);
        free(scene);
        return 0;
    }
    bx0 = (int)floorf(minX / MAP_BLOCK);
    bx1 = (int)floorf(maxX / MAP_BLOCK);
    bz0 = (int)floorf(minZ / MAP_BLOCK);
    bz1 = (int)floorf(maxZ / MAP_BLOCK);
    for (bz = bz0; bz <= bz1; bz++) {
        for (bx = bx0; bx <= bx1; bx++) {
            float x0 = (float)(bx * MAP_BLOCK), z0 = (float)(bz * MAP_BLOCK);
            float h[256];
            int has[256];
            float sorted[256];
            int nh = 0;
            int wet[256];
            int c;
            float base;
            char line[300];
            int n;
            int any = 0;

            for (c = 0; c < 256; c++) {
                float x = x0 + (c % 16) * MAP_CELL + MAP_CELL / 2;
                float z = z0 + (c / 16) * MAP_CELL + MAP_CELL / 2;
                int k;

                has[c] = 0;
                wet[c] = 0;
                for (k = 0; k < nTri; k++) {
                    float y;

                    if ((x < tris[k].minX) || (x > tris[k].maxX) || (z < tris[k].minZ) || (z > tris[k].maxZ)) {
                        continue;
                    }
                    if (TriHeight(&tris[k], x, z, &y) && (!has[c] || (y > h[c]))) {
                        h[c] = y;
                        has[c] = 1;
                    }
                }
                for (k = 0; k < nWat; k++) {
                    if ((x >= water[k].minX) && (x <= water[k].maxX) && (z >= water[k].minZ) && (z <= water[k].maxZ) &&
                        (!has[c] || (water[k].y > h[c] + 10.0f))) {
                        wet[c] = 1;
                    }
                }
                if (has[c]) {
                    sorted[nh++] = h[c];
                }
                any |= has[c] | wet[c];
            }
            if (!any) {
                continue;
            }
            if (nh > 0) {
                qsort(sorted, nh, sizeof(float), CmpFloat);
                base = sorted[nh / 2];
            } else {
                base = 0.0f;
            }
            n = snprintf(line, sizeof(line), "%u|%d|%d|%d|", sceneId, bx, bz, (int)base);
            for (c = 0; c < 256; c++) {
                char ch;

                if (wet[c]) {
                    ch = '~';
                } else if (!has[c]) {
                    ch = '.';
                } else {
                    float rel = (h[c] - base) / 40.0f;
                    int code = (int)((rel >= 0.0f) ? (rel + 0.5f) : (rel - 0.5f)) + 32;

                    ch = (char)(0x30 + ((code < 0) ? 0 : (code > 63) ? 63 : code));
                }
                line[n++] = ch;
            }
            line[n++] = '\n';
            if (*outLen + n + 1 > *outCap) {
                size_t cap = (*outCap == 0) ? 65536 : *outCap * 2;
                char* grown = (char*)realloc(*out, cap);

                if (grown == NULL) {
                    continue;
                }
                *out = grown;
                *outCap = cap;
            }
            memcpy(*out + *outLen, line, n);
            *outLen += n;
            blocks++;
        }
    }
    free(tris);
    free(water);
    free(scene);
    return blocks;
}

// 0 not started, 1 working, 2 done, 3 no ROM found, 4 upload failed
static volatile LONG sMapRomState = 0;

static int Http(const wchar_t* verb, const wchar_t* path, const char* body, int bodyLen, char* out, int outMax);

// Once per relay: build every area's map and send it up (a few hundred lines, in batches).
static void MapRom_Run(void) {
    static char resp[64];
    char* all = NULL;
    size_t len = 0;
    size_t cap = 0;
    size_t pos = 0;
    size_t i;

    if (Http(L"GET", L"/api/mapbulk", NULL, 0, resp, sizeof(resp)) != 200) {
        return; // an older relay, or offline: try again next start
    }
    if (strncmp(resp, "done", 4) == 0) {
        InterlockedExchange(&sMapRomState, 2);
        return;
    }
    InterlockedExchange(&sMapRomState, 1);
    if (!RomFind()) {
        InterlockedExchange(&sMapRomState, 3);
        return;
    }
    for (i = 0; i < sizeof(sSceneRom) / sizeof(sSceneRom[0]); i++) {
        SceneToBlocks(sSceneRom[i][0], sSceneRom[i][1], &all, &len, &cap);
    }
    free(sRom);
    sRom = NULL;
    // About 40 blocks per request.
    while (pos < len) {
        size_t end = pos;
        int lines = 0;

        while ((end < len) && (lines < 40)) {
            while ((end < len) && (all[end] != '\n')) {
                end++;
            }
            end++;
            lines++;
        }
        if (end > len) {
            end = len;
        }
        if (Http(L"POST", L"/api/mapbulk", all + pos, (int)(end - pos), NULL, 0) != 200) {
            InterlockedExchange(&sMapRomState, 4);
            free(all);
            return;
        }
        pos = end;
    }
    free(all);
    Http(L"POST", L"/api/mapbulk?done=1", "", 0, NULL, 0);
    InterlockedExchange(&sMapRomState, 2);
}

// Extra status line from the DLL itself: how the area maps are coming along.
static int ExtraStatus(char* out, int max) {
    static const char* sStates[] = { "", "working", "done", "no-rom", "upload-failed" };
    LONG st = sMapRomState;

    return snprintf(out, max, "\nmaprom=%s", sStates[(st >= 0 && st <= 4) ? st : 0]);
}
