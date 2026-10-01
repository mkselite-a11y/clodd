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
// --- Reachable ground only ---------------------------------------------------------
// Many areas have scenery with collision (hills and cliffs behind invisible
// walls). Starting from the area's entrances, we walk outward cell by cell:
// small steps up, drops down, swimming, and never through a wall. Only ground
// reached that way goes on the map.

#define LAYERS 5          // floor heights kept per cell (the last can be a water surface)
#define STEP_UP 70.0f     // a ledge Link walks up
#define WATER_OUT 100.0f  // climbing out of water
#define DROP_MAX 800.0f   // jumping down

typedef struct {
    float ax, az, bx, bz, cx, cz;
    float minY, maxY;
} Wall;

static int SegCross(float ax, float az, float bx, float bz, float cx, float cz, float dx, float dz) {
    float d1 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
    float d2 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
    float d3 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx);
    float d4 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);

    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

static int WallBlocks(const Wall* w, float ax, float az, float bx, float bz, float lowY, float highY) {
    if ((w->maxY < lowY) || (w->minY > highY)) {
        return 0;
    }
    return SegCross(ax, az, bx, bz, w->ax, w->az, w->bx, w->bz) || SegCross(ax, az, bx, bz, w->bx, w->bz, w->cx, w->cz) ||
           SegCross(ax, az, bx, bz, w->cx, w->cz, w->ax, w->az);
}

static int SceneToBlocks(uint16_t sceneId, uint32_t dmaIndex, char** out, size_t* outLen, size_t* outCap) {
    uint32_t len = 0;
    uint8_t* scene = RomFile(dmaIndex, &len);
    uint32_t off;
    uint32_t col = 0;
    uint32_t spawnOff = 0, spawnN = 0, doorOff = 0, doorN = 0;
    uint32_t nVtx, vtxOff, nPoly, polyOff, nWater, waterOff;
    Tri* tris = NULL;
    Wall* walls = NULL;
    int nTri = 0, nWall = 0;
    Water* water = NULL;
    int nWat = 0;
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    int gx0, gz0, W, H, nCells;
    float* lay = NULL;      // [cell][LAYERS] heights
    unsigned char* nLay = NULL;
    unsigned char* isWater = NULL; // [cell][LAYERS]
    unsigned char* reached = NULL; // [cell][LAYERS]
    int* bucketStart = NULL;
    int* bucket = NULL;
    int* queue = NULL;
    int qHead = 0, qTail = 0;
    int seeds = 0;
    uint32_t i;
    int blocks = 0;
    int k;

    if (scene == NULL) {
        return 0;
    }
    // Scene header: 0x00 spawn points, 0x03 collision, 0x0E doors, 0x14 the end.
    for (off = 0; (off + 8 <= len) && (off < 0x200); off += 8) {
        if (scene[off] == 0x14) {
            break;
        }
        if (scene[off] == 0x03) {
            col = BE32(scene + off + 4) & 0xFFFFFF;
        } else if (scene[off] == 0x00) {
            spawnN = scene[off + 1];
            spawnOff = BE32(scene + off + 4) & 0xFFFFFF;
        } else if (scene[off] == 0x0E) {
            doorN = scene[off + 1];
            doorOff = BE32(scene + off + 4) & 0xFFFFFF;
        }
    }
    if ((col == 0) || (col + 0x2C > len)) {
        goto done;
    }
    nVtx = BE16(scene + col + 0x0C);
    vtxOff = BE32(scene + col + 0x10) & 0xFFFFFF;
    nPoly = BE16(scene + col + 0x14);
    polyOff = BE32(scene + col + 0x18) & 0xFFFFFF;
    nWater = BE16(scene + col + 0x24);
    waterOff = BE32(scene + col + 0x28) & 0xFFFFFF;
    if ((vtxOff + nVtx * 6 > len) || (polyOff + nPoly * 16 > len)) {
        goto done;
    }
    tris = (Tri*)malloc(sizeof(Tri) * (nPoly + 1));
    walls = (Wall*)malloc(sizeof(Wall) * (nPoly + 1));
    if ((tris == NULL) || (walls == NULL)) {
        goto done;
    }
    for (i = 0; i < nPoly; i++) {
        const uint8_t* p = scene + polyOff + i * 16;
        uint32_t ia = BE16(p + 2) & 0x1FFF, ib = BE16(p + 4) & 0x1FFF, ic = BE16(p + 6) & 0x1FFF;
        int16_t ny = (int16_t)BE16(p + 0xA);
        float v[9];

        if ((ia >= nVtx) || (ib >= nVtx) || (ic >= nVtx)) {
            continue;
        }
#define VX(k, o) ((float)(int16_t)BE16(scene + vtxOff + (k) * 6 + (o)))
        v[0] = VX(ia, 0), v[1] = VX(ia, 2), v[2] = VX(ia, 4);
        v[3] = VX(ib, 0), v[4] = VX(ib, 2), v[5] = VX(ib, 4);
        v[6] = VX(ic, 0), v[7] = VX(ic, 2), v[8] = VX(ic, 4);
#undef VX
        if (ny >= 0x3000) {
            Tri* t = &tris[nTri++];

            t->ax = v[0], t->ay = v[1], t->az = v[2];
            t->bx = v[3], t->by = v[4], t->bz = v[5];
            t->cx = v[6], t->cy = v[7], t->cz = v[8];
            t->minX = min(t->ax, min(t->bx, t->cx));
            t->maxX = max(t->ax, max(t->bx, t->cx));
            t->minZ = min(t->az, min(t->bz, t->cz));
            t->maxZ = max(t->az, max(t->bz, t->cz));
            minX = min(minX, t->minX);
            maxX = max(maxX, t->maxX);
            minZ = min(minZ, t->minZ);
            maxZ = max(maxZ, t->maxZ);
        } else if (ny > -0x3000) { // walls (and steep slopes): they stop the walk
            Wall* w = &walls[nWall++];

            w->ax = v[0], w->az = v[2], w->bx = v[3], w->bz = v[5], w->cx = v[6], w->cz = v[8];
            w->minY = min(v[1], min(v[4], v[7]));
            w->maxY = max(v[1], max(v[4], v[7]));
        }
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
        goto done;
    }
    gx0 = (int)floorf(minX / MAP_CELL);
    gz0 = (int)floorf(minZ / MAP_CELL);
    W = (int)floorf(maxX / MAP_CELL) - gx0 + 1;
    H = (int)floorf(maxZ / MAP_CELL) - gz0 + 1;
    if ((W <= 0) || (H <= 0) || ((long long)W * H > 400 * 400)) {
        goto done;
    }
    nCells = W * H;
    lay = (float*)malloc(sizeof(float) * nCells * LAYERS);
    nLay = (unsigned char*)calloc(nCells, 1);
    isWater = (unsigned char*)calloc((size_t)nCells * LAYERS, 1);
    reached = (unsigned char*)calloc((size_t)nCells * LAYERS, 1);
    bucketStart = (int*)calloc(nCells + 1, sizeof(int));
    queue = (int*)malloc(sizeof(int) * nCells * LAYERS);
    if (!lay || !nLay || !isWater || !reached || !bucketStart || !queue) {
        goto done;
    }
    // Floor heights per cell (several, for bridges and upper floors).
    for (k = 0; k < nTri; k++) {
        int x0 = (int)floorf(tris[k].minX / MAP_CELL) - gx0, x1 = (int)floorf(tris[k].maxX / MAP_CELL) - gx0;
        int z0 = (int)floorf(tris[k].minZ / MAP_CELL) - gz0, z1 = (int)floorf(tris[k].maxZ / MAP_CELL) - gz0;
        int cx, cz;

        for (cz = max(z0, 0); cz <= min(z1, H - 1); cz++) {
            for (cx = max(x0, 0); cx <= min(x1, W - 1); cx++) {
                int c = cz * W + cx;
                float y;
                int j;
                int dup = 0;

                if (!TriHeight(&tris[k], (gx0 + cx) * MAP_CELL + MAP_CELL / 2.0f, (gz0 + cz) * MAP_CELL + MAP_CELL / 2.0f, &y)) {
                    continue;
                }
                for (j = 0; j < nLay[c]; j++) {
                    if (fabsf(lay[c * LAYERS + j] - y) < 30.0f) {
                        dup = 1;
                        lay[c * LAYERS + j] = max(lay[c * LAYERS + j], y);
                    }
                }
                if (!dup && (nLay[c] < LAYERS - 1)) {
                    lay[c * LAYERS + nLay[c]++] = y;
                }
            }
        }
    }
    // Water surfaces you can swim on (where they sit above the floor).
    for (k = 0; k < nWat; k++) {
        int x0 = (int)floorf(water[k].minX / MAP_CELL) - gx0, x1 = (int)floorf(water[k].maxX / MAP_CELL) - gx0;
        int z0 = (int)floorf(water[k].minZ / MAP_CELL) - gz0, z1 = (int)floorf(water[k].maxZ / MAP_CELL) - gz0;
        int cx, cz;

        for (cz = max(z0, 0); cz <= min(z1, H - 1); cz++) {
            for (cx = max(x0, 0); cx <= min(x1, W - 1); cx++) {
                int c = cz * W + cx;

                if (nLay[c] < LAYERS) {
                    isWater[c * LAYERS + nLay[c]] = 1;
                    lay[c * LAYERS + nLay[c]++] = water[k].y;
                }
            }
        }
    }
    // Walls bucketed by the cells they touch.
    for (k = 0; k < nWall; k++) {
        int x0 = (int)floorf(min(walls[k].ax, min(walls[k].bx, walls[k].cx)) / MAP_CELL) - gx0;
        int x1 = (int)floorf(max(walls[k].ax, max(walls[k].bx, walls[k].cx)) / MAP_CELL) - gx0;
        int z0 = (int)floorf(min(walls[k].az, min(walls[k].bz, walls[k].cz)) / MAP_CELL) - gz0;
        int z1 = (int)floorf(max(walls[k].az, max(walls[k].bz, walls[k].cz)) / MAP_CELL) - gz0;
        int cx, cz;

        for (cz = max(z0, 0); cz <= min(z1, H - 1); cz++) {
            for (cx = max(x0, 0); cx <= min(x1, W - 1); cx++) {
                bucketStart[cz * W + cx + 1]++;
            }
        }
    }
    for (k = 0; k < nCells; k++) {
        bucketStart[k + 1] += bucketStart[k];
    }
    bucket = (int*)malloc(sizeof(int) * (bucketStart[nCells] + 1));
    if (bucket == NULL) {
        goto done;
    }
    {
        int* fill = (int*)calloc(nCells, sizeof(int));

        if (fill == NULL) {
            goto done;
        }
        for (k = 0; k < nWall; k++) {
            int x0 = (int)floorf(min(walls[k].ax, min(walls[k].bx, walls[k].cx)) / MAP_CELL) - gx0;
            int x1 = (int)floorf(max(walls[k].ax, max(walls[k].bx, walls[k].cx)) / MAP_CELL) - gx0;
            int z0 = (int)floorf(min(walls[k].az, min(walls[k].bz, walls[k].cz)) / MAP_CELL) - gz0;
            int z1 = (int)floorf(max(walls[k].az, max(walls[k].bz, walls[k].cz)) / MAP_CELL) - gz0;
            int cx, cz;

            for (cz = max(z0, 0); cz <= min(z1, H - 1); cz++) {
                for (cx = max(x0, 0); cx <= min(x1, W - 1); cx++) {
                    int c = cz * W + cx;

                    bucket[bucketStart[c] + fill[c]++] = k;
                }
            }
        }
        free(fill);
    }
    // Start from every spawn point and door.
    for (k = 0; k < (int)(spawnN + doorN); k++) {
        uint32_t at = (k < (int)spawnN) ? spawnOff + k * 16 + 2 : doorOff + (k - spawnN) * 16 + 6;
        float px, py, pz;
        int cx, cz, c, j, best = -1;
        float bestD = 200.0f;

        if (at + 6 > len) {
            continue;
        }
        px = (float)(int16_t)BE16(scene + at);
        py = (float)(int16_t)BE16(scene + at + 2);
        pz = (float)(int16_t)BE16(scene + at + 4);
        cx = (int)floorf(px / MAP_CELL) - gx0;
        cz = (int)floorf(pz / MAP_CELL) - gz0;
        if ((cx < 0) || (cz < 0) || (cx >= W) || (cz >= H)) {
            continue;
        }
        c = cz * W + cx;
        for (j = 0; j < nLay[c]; j++) {
            float d = fabsf(lay[c * LAYERS + j] - py);

            if (d < bestD) {
                bestD = d;
                best = j;
            }
        }
        if ((best >= 0) && !reached[c * LAYERS + best]) {
            reached[c * LAYERS + best] = 1;
            queue[qTail++] = c * LAYERS + best;
            seeds++;
        }
    }
    if (seeds == 0) {
        // No way in found: keep every floor (better than nothing).
        for (k = 0; k < nCells; k++) {
            int j;

            for (j = 0; j < nLay[k]; j++) {
                reached[k * LAYERS + j] = 1;
            }
        }
    }
    while (qHead < qTail) {
        int node = queue[qHead++];
        int c = node / LAYERS, la = node % LAYERS;
        int cx = c % W, cz = c / W;
        float ya = lay[node];
        static const int sDir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        int d;

        for (d = 0; d < 4; d++) {
            int nx = cx + sDir[d][0], nz = cz + sDir[d][1];
            int nc, j;
            float ax, az, bx, bz;

            if ((nx < 0) || (nz < 0) || (nx >= W) || (nz >= H)) {
                continue;
            }
            nc = nz * W + nx;
            ax = (gx0 + cx) * MAP_CELL + MAP_CELL / 2.0f, az = (gz0 + cz) * MAP_CELL + MAP_CELL / 2.0f;
            bx = (gx0 + nx) * MAP_CELL + MAP_CELL / 2.0f, bz = (gz0 + nz) * MAP_CELL + MAP_CELL / 2.0f;
            for (j = 0; j < nLay[nc]; j++) {
                int nn = nc * LAYERS + j;
                float yb = lay[nn];
                float dy = yb - ya;
                float up = isWater[node] ? WATER_OUT : STEP_UP;
                int b, blocked = 0;

                if (reached[nn] || (dy > up) || (dy < -DROP_MAX)) {
                    continue;
                }
                // A wall between the two cells at chest height stops it.
                for (b = bucketStart[c]; (b < bucketStart[c + 1]) && !blocked; b++) {
                    blocked = WallBlocks(&walls[bucket[b]], ax, az, bx, bz, max(ya, yb) + 20.0f, max(ya, yb) + 60.0f);
                }
                for (b = bucketStart[nc]; (b < bucketStart[nc + 1]) && !blocked; b++) {
                    blocked = WallBlocks(&walls[bucket[b]], ax, az, bx, bz, max(ya, yb) + 20.0f, max(ya, yb) + 60.0f);
                }
                if (!blocked) {
                    reached[nn] = 1;
                    queue[qTail++] = nn;
                }
            }
            (void)la;
        }
    }
    // Out to 16x16 blocks: the highest reached floor per cell, or water.
    {
        int bx0 = (int)floorf((float)gx0 / 16.0f), bz0 = (int)floorf((float)gz0 / 16.0f);
        int bx1 = (int)floorf((float)(gx0 + W - 1) / 16.0f), bz1 = (int)floorf((float)(gz0 + H - 1) / 16.0f);
        int bx, bz;

        for (bz = bz0; bz <= bz1; bz++) {
            for (bx = bx0; bx <= bx1; bx++) {
                float h[256];
                int kind[256]; // 0 none, 1 floor, 2 water
                float sorted[256];
                int nh = 0;
                int any = 0;
                int c;
                float base = 0.0f;
                char line[300];
                int n;

                for (c = 0; c < 256; c++) {
                    int cx = bx * 16 + (c % 16) - gx0, cz = bz * 16 + (c / 16) - gz0;
                    int j;
                    int cell;
                    float bestY = -1e9f;
                    int bestKind = 0;

                    kind[c] = 0;
                    if ((cx < 0) || (cz < 0) || (cx >= W) || (cz >= H)) {
                        continue;
                    }
                    cell = cz * W + cx;
                    for (j = 0; j < nLay[cell]; j++) {
                        if (reached[cell * LAYERS + j] && (lay[cell * LAYERS + j] > bestY)) {
                            bestY = lay[cell * LAYERS + j];
                            bestKind = isWater[cell * LAYERS + j] ? 2 : 1;
                        }
                    }
                    kind[c] = bestKind;
                    h[c] = bestY;
                    if (bestKind == 1) {
                        sorted[nh++] = bestY;
                    }
                    any |= bestKind;
                }
                if (!any) {
                    continue;
                }
                if (nh > 0) {
                    qsort(sorted, nh, sizeof(float), CmpFloat);
                    base = sorted[nh / 2];
                }
                n = snprintf(line, sizeof(line), "%u|%d|%d|%d|", sceneId, bx, bz, (int)base);
                for (c = 0; c < 256; c++) {
                    char ch = '.';

                    if (kind[c] == 2) {
                        ch = '~';
                    } else if (kind[c] == 1) {
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
    }
done:
    free(tris);
    free(walls);
    free(water);
    free(lay);
    free(nLay);
    free(isWater);
    free(reached);
    free(bucketStart);
    free(bucket);
    free(queue);
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
    if (len == 0) {
        InterlockedExchange(&sMapRomState, 4);
        free(all);
        return;
    }
    // New maps ready: the old ones (an older format) go first.
    if (Http(L"POST", L"/api/mapbulk?reset=1", "", 0, NULL, 0) != 200) {
        InterlockedExchange(&sMapRomState, 4);
        free(all);
        return;
    }
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
