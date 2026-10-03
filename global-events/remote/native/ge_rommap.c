// Maps of every area for the friends' radar and map, made once from the
// player's own ROM (the copy MM Recompiled keeps), then uploaded to their
// relay. Nothing from the ROM ever goes in the release: only these coarse
// height grids, and only to the player's relay.
//
// Included by ge_remote.c. Define GE_ROMMAP_TEST to build only the parts that
// don't need Windows (the map builder and the ROM file parsing), for tests.

#include "ge_scenes.h"

#define DMADATA_US 0x1A500
#define DMADATA_MAX 2000 // the US table has 1552 files

static uint8_t* sRom = NULL;
static uint32_t sRomLen = 0;

static uint32_t BE32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t BE16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

#ifndef GE_ROMMAP_TEST
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
#endif // GE_ROMMAP_TEST

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
    out = (uint8_t*)calloc(size, 1);
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
    const uint8_t* e;
    uint32_t vStart, vEnd, pStart, pEnd;
    uint8_t* out;

    if ((sRom == NULL) || (index >= DMADATA_MAX) || (DMADATA_US + (index + 1) * 16 > sRomLen)) {
        return NULL;
    }
    e = sRom + DMADATA_US + index * 16;
    vStart = BE32(e);
    vEnd = BE32(e + 4);
    pStart = BE32(e + 8);
    pEnd = BE32(e + 12);
    if ((vEnd <= vStart) || (vEnd - vStart > 0x400000) || (pStart >= sRomLen) || (pStart == 0xFFFFFFFF)) {
        return NULL;
    }
    if (pEnd == 0) { // stored as is
        if (vEnd - vStart > sRomLen - pStart) {
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

// The file table row whose ROM address (vStart) is this one, or -1.
static int RomFindVrom(uint32_t vrom) {
    uint32_t i;

    if (sRom == NULL) {
        return -1;
    }
    for (i = 0; (i < DMADATA_MAX) && (DMADATA_US + (i + 1) * 16 <= sRomLen); i++) {
        const uint8_t* e = sRom + DMADATA_US + i * 16;
        uint32_t vs = BE32(e), ve = BE32(e + 4);

        if ((i > 0) && (vs == 0) && (ve == 0)) {
            break; // end of the table
        }
        if ((vs == vrom) && (ve > vs)) {
            return (int)i;
        }
    }
    return -1;
}

// =====================================================================================
// The map builder: collision triangles in, "scene|bx|bz|base|cells" lines out.
//
// Every floor triangle is sampled on a fine grid (RM_SAMPLE units), keeping a few
// floor heights per spot (bridges, upper floors). Then, starting from every way in
// and every actor's spot, we walk outward sample by sample: small steps and ramps
// up, drops down, swimming, ladders and vines, and never through a wall. Every
// surface goes on the map; the ground reached that way is marked reachable.
//
// Output, format v3: a block is 16 x 16 cells of 64 units (1024 units a side),
// bx = floor(x / 1024), bz = floor(z / 1024). The cells field is 320 chars: 256 cell
// chars, row-major (row along z, col along x), then 64 lowercase hex chars, a
// reachable bit per cell (hex char k holds cells 4k..4k+3, cell 4k+j is bit 1<<j).
// Cell chars: '.' nothing, '~' water on top, else '0' + code with
// height = base + (code - 32) * 40, code 0..63.
// =====================================================================================

#define RM_CELL 64            // output cell, world units
#define RM_BLOCK (RM_CELL * 16) // output block
#define RM_SAMPLE 16          // fine sample spacing (grows to 32 or 64 for huge areas)
#define RM_COLS_MAX 1500000   // sample columns before the spacing grows
#define RM_LAYERS 8           // floor heights kept per sample column
#define RM_MERGE 20.0f        // floor heights this close are one floor
#define RM_EDGE_TOL 6.0f      // a sample this close outside a floor still counts
#define RM_STEP_UP 45.0f      // up between neighbouring samples (steps, ramps)
#define RM_WATER_OUT 100.0f   // climbing out of water
#define RM_DROP_MAX 800.0f    // jumping down
#define RM_BODY_LO 20.0f      // a wall this far above the floor blocks the way...
#define RM_BODY_HI 50.0f      // ...up to this high
#define RM_SHALLOW 20.0f      // a floor this close under the water surface: wade on it
#define RM_COVERED 100.0f     // a floor this close above the water surface hides it
#define RM_CLIMB_REACH 40.0f  // floor this close to a ladder or vine wall can use it
#define RM_SEED_REACH 120.0f  // an actor snaps to floor this close
#define RM_SEED_BELOW 300.0f
#define RM_SEED_ABOVE 60.0f
#define RM_HITS_MAX 6000000
#define RM_BUCKET_MAX 12000000
#define RM_TRIS_MAX 65536
#define RM_SEEDS_MAX 8192

typedef struct {
    float x[3], y[3], z[3];
    int group; // walls only: ladder or vine group (-1 if not climbable)
} MapTri;

typedef struct {
    float minX, maxX, minZ, maxZ, y;
} MapWater;

typedef struct {
    float x, y, z;
} MapSeed;

typedef struct {
    int col;
    float y;
    int water;
} MapHit;

typedef struct {
    float ox, oz; // world corner of sample (0, 0)
    int S, W, H;
    MapHit* hits;
    int nHits, capHits;
} MapGrid;

static float MapMinF(float a, float b) {
    return (a < b) ? a : b;
}

static float MapMaxF(float a, float b) {
    return (a > b) ? a : b;
}

static int MapMinI(int a, int b) {
    return (a < b) ? a : b;
}

static int MapMaxI(int a, int b) {
    return (a > b) ? a : b;
}

// Grows a malloc'd array to hold at least `need` items.
static int MapGrow(void** buf, int* cap, int need, size_t elem) {
    int n = (*cap == 0) ? 1024 : *cap;
    void* p;

    if (need <= *cap) {
        return 1;
    }
    while (n < need) {
        if (n > (1 << 27)) {
            return 0;
        }
        n *= 2;
    }
    p = realloc(*buf, (size_t)n * elem);
    if (p == NULL) {
        return 0;
    }
    *buf = p;
    *cap = n;
    return 1;
}

static void MapAddHit(MapGrid* g, int ix, int iz, float y, int water) {
    MapHit* h;

    if ((ix < 0) || (iz < 0) || (ix >= g->W) || (iz >= g->H) || (g->nHits >= RM_HITS_MAX)) {
        return;
    }
    if (!MapGrow((void**)&g->hits, &g->capHits, g->nHits + 1, sizeof(MapHit))) {
        return;
    }
    h = &g->hits[g->nHits++];
    h->col = iz * g->W + ix;
    h->y = y;
    h->water = water;
}

static int MapSampleIndex(float v, float o, int S) {
    return (int)floorf((v - o) / (float)S);
}

// Marks every sample a floor triangle covers (with a small edge tolerance), and
// always the sample under its middle, so even thin slivers show up.
static void MapRasterFloor(MapGrid* g, const MapTri* t) {
    float minX = MapMinF(t->x[0], MapMinF(t->x[1], t->x[2])), maxX = MapMaxF(t->x[0], MapMaxF(t->x[1], t->x[2]));
    float minZ = MapMinF(t->z[0], MapMinF(t->z[1], t->z[2])), maxZ = MapMaxF(t->z[0], MapMaxF(t->z[1], t->z[2]));
    float minY = MapMinF(t->y[0], MapMinF(t->y[1], t->y[2])), maxY = MapMaxF(t->y[0], MapMaxF(t->y[1], t->y[2]));
    float ux = t->x[1] - t->x[0], uy = t->y[1] - t->y[0], uz = t->z[1] - t->z[0];
    float vx = t->x[2] - t->x[0], vy = t->y[2] - t->y[0], vz = t->z[2] - t->z[0];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float area2 = ux * vz - uz * vx;
    float en[3][3]; // per edge: inward normal x, z and offset
    int ix0, ix1, iz0, iz1, ix, iz, e;
    float S = (float)g->S;

    MapAddHit(g, MapSampleIndex((t->x[0] + t->x[1] + t->x[2]) / 3.0f, g->ox, g->S),
              MapSampleIndex((t->z[0] + t->z[1] + t->z[2]) / 3.0f, g->oz, g->S), (t->y[0] + t->y[1] + t->y[2]) / 3.0f, 0);
    if ((fabsf(ny) < 0.001f) || (fabsf(area2) < 0.01f)) {
        return;
    }
    for (e = 0; e < 3; e++) {
        int f = (e + 1) % 3;
        float ex = t->x[f] - t->x[e], ez = t->z[f] - t->z[e];
        float len = sqrtf(ex * ex + ez * ez);
        float sg = (area2 > 0.0f) ? 1.0f : -1.0f;

        if (len < 0.0001f) {
            en[e][0] = en[e][1] = 0.0f;
            en[e][2] = 1.0f; // always passes
            continue;
        }
        // distance inside = sg * (ex * (z - pz) - ez * (x - px)) / len
        en[e][0] = -sg * ez / len;
        en[e][1] = sg * ex / len;
        en[e][2] = -(en[e][0] * t->x[e] + en[e][1] * t->z[e]);
    }
    ix0 = MapMaxI(MapSampleIndex(minX - RM_EDGE_TOL, g->ox, g->S), 0);
    ix1 = MapMinI(MapSampleIndex(maxX + RM_EDGE_TOL, g->ox, g->S), g->W - 1);
    iz0 = MapMaxI(MapSampleIndex(minZ - RM_EDGE_TOL, g->oz, g->S), 0);
    iz1 = MapMinI(MapSampleIndex(maxZ + RM_EDGE_TOL, g->oz, g->S), g->H - 1);
    for (iz = iz0; iz <= iz1; iz++) {
        float z = g->oz + ((float)iz + 0.5f) * S;

        for (ix = ix0; ix <= ix1; ix++) {
            float x = g->ox + ((float)ix + 0.5f) * S;
            float y;

            // The box check keeps sharp corners from reaching far past the tolerance.
            if ((x < minX - RM_EDGE_TOL) || (x > maxX + RM_EDGE_TOL) || (z < minZ - RM_EDGE_TOL) || (z > maxZ + RM_EDGE_TOL)) {
                continue;
            }
            for (e = 0; e < 3; e++) {
                if (en[e][0] * x + en[e][1] * z + en[e][2] < -RM_EDGE_TOL) {
                    break;
                }
            }
            if (e < 3) {
                continue;
            }
            y = t->y[0] - (nx * (x - t->x[0]) + nz * (z - t->z[0])) / ny;
            y = MapMaxF(minY, MapMinF(maxY, y));
            MapAddHit(g, ix, iz, y, 0);
        }
    }
}

// Does this wall cut the walk from A to B anywhere between heights lo and hi?
// Slices the wall with the upright plane through A and B and checks that slice.
static int MapWallCuts(const MapTri* w, float ax, float az, float bx, float bz, float lo, float hi) {
    float ex = bx - ax, ez = bz - az;
    float len2 = ex * ex + ez * ez;
    float s[3], t[3];
    float pt[4], py[4];
    int np = 0;
    int i;
    float tMin = 1e30f, tMax = -1e30f, yAtMin = 0.0f, yAtMax = 0.0f, yLo = 1e30f, yHi = -1e30f;
    float t0, t1, y0, y1;

    if (len2 < 0.0001f) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        s[i] = ex * (w->z[i] - az) - ez * (w->x[i] - ax);
        t[i] = ((w->x[i] - ax) * ex + (w->z[i] - az) * ez) / len2;
    }
    for (i = 0; i < 3; i++) {
        int j = (i + 1) % 3;

        if (fabsf(s[i]) < 0.001f) {
            pt[np] = t[i];
            py[np] = w->y[i];
            np++;
        }
        if (((s[i] > 0.001f) && (s[j] < -0.001f)) || ((s[i] < -0.001f) && (s[j] > 0.001f))) {
            float f = s[i] / (s[i] - s[j]);

            if (np < 4) {
                pt[np] = t[i] + f * (t[j] - t[i]);
                py[np] = w->y[i] + f * (w->y[j] - w->y[i]);
                np++;
            }
        }
        if (np >= 4) {
            break;
        }
    }
    if (np == 0) {
        return 0;
    }
    for (i = 0; i < np; i++) {
        if (pt[i] < tMin) {
            tMin = pt[i];
            yAtMin = py[i];
        }
        if (pt[i] > tMax) {
            tMax = pt[i];
            yAtMax = py[i];
        }
        yLo = MapMinF(yLo, py[i]);
        yHi = MapMaxF(yHi, py[i]);
    }
    // A wall right through A doesn't stop leaving A (a sample sitting exactly on a
    // wall line would be stuck otherwise), but one through B stops reaching B.
    if ((tMax < 0.001f) || (tMin > 1.0f)) {
        return 0;
    }
    if (tMax - tMin > 0.0001f) { // a slanted slice: just the part between A and B
        t0 = MapMaxF(tMin, 0.0f);
        t1 = MapMinF(tMax, 1.0f);
        y0 = yAtMin + (yAtMax - yAtMin) * (t0 - tMin) / (tMax - tMin);
        y1 = yAtMin + (yAtMax - yAtMin) * (t1 - tMin) / (tMax - tMin);
        yLo = MapMinF(y0, y1);
        yHi = MapMaxF(y0, y1);
    }
    return (yHi >= lo) && (yLo <= hi);
}

// Horizontal distance from a point to a triangle seen from above.
static float MapDistTriXZ(const MapTri* t, float x, float z) {
    float best = 1e30f;
    int inside = 1, sign = 0;
    int e;

    for (e = 0; e < 3; e++) {
        int f = (e + 1) % 3;
        float ex = t->x[f] - t->x[e], ez = t->z[f] - t->z[e];
        float px = x - t->x[e], pz = z - t->z[e];
        float len2 = ex * ex + ez * ez;
        float c = ex * pz - ez * px;
        float u = (len2 > 0.0001f) ? (px * ex + pz * ez) / len2 : 0.0f;
        float dx, dz;

        if (c != 0.0f) {
            int sg = (c > 0.0f) ? 1 : -1;

            if (sign == 0) {
                sign = sg;
            } else if (sg != sign) {
                inside = 0;
            }
        }
        u = MapMaxF(0.0f, MapMinF(1.0f, u));
        dx = px - u * ex;
        dz = pz - u * ez;
        best = MapMinF(best, sqrtf(dx * dx + dz * dz));
    }
    return (inside && (sign != 0)) ? 0.0f : best;
}

// Adds wall `k` to every bucket cell its footprint (padded) touches, row by row.
// fill == NULL: just counts.
static int MapBucketWall(const MapTri* w, int k, float ox, float oz, int bW, int bH, float pad, int* start, int* fill, int* bucket) {
    float minZ = MapMinF(w->z[0], MapMinF(w->z[1], w->z[2])) - pad;
    float maxZ = MapMaxF(w->z[0], MapMaxF(w->z[1], w->z[2])) + pad;
    int r0 = MapMaxI((int)floorf((minZ - oz) / RM_CELL), 0), r1 = MapMinI((int)floorf((maxZ - oz) / RM_CELL), bH - 1);
    int r, added = 0;

    for (r = r0; r <= r1; r++) {
        float zlo = oz + (float)r * RM_CELL - pad, zhi = oz + (float)(r + 1) * RM_CELL + pad;
        float xlo = 1e30f, xhi = -1e30f;
        int e, c0, c1, c;

        for (e = 0; e < 3; e++) {
            int f = (e + 1) % 3;
            float pz = w->z[e], qz = w->z[f], px = w->x[e], qx = w->x[f];
            float ta = 0.0f, tb = 1.0f;

            if (fabsf(qz - pz) < 0.0001f) {
                if ((pz < zlo) || (pz > zhi)) {
                    continue;
                }
            } else {
                float t1 = (zlo - pz) / (qz - pz), t2 = (zhi - pz) / (qz - pz);

                ta = MapMaxF(ta, MapMinF(t1, t2));
                tb = MapMinF(tb, MapMaxF(t1, t2));
                if (ta > tb) {
                    continue;
                }
            }
            xlo = MapMinF(xlo, MapMinF(px + (qx - px) * ta, px + (qx - px) * tb));
            xhi = MapMaxF(xhi, MapMaxF(px + (qx - px) * ta, px + (qx - px) * tb));
        }
        if (xlo > xhi) {
            continue;
        }
        c0 = MapMaxI((int)floorf((xlo - pad - ox) / RM_CELL), 0);
        c1 = MapMinI((int)floorf((xhi + pad - ox) / RM_CELL), bW - 1);
        for (c = c0; c <= c1; c++) {
            int cell = r * bW + c;

            if (fill == NULL) {
                start[cell + 1]++;
            } else {
                bucket[start[cell] + fill[cell]++] = k;
            }
            added++;
        }
    }
    return added;
}

static void MapAppend(char** out, size_t* outLen, size_t* outCap, const char* line, int n) {
    if (*outLen + n + 1 > *outCap) {
        size_t cap = (*outCap == 0) ? 65536 : *outCap;
        char* grown;

        while (*outLen + n + 1 > cap) {
            cap *= 2;
        }
        grown = (char*)realloc(*out, cap);
        if (grown == NULL) {
            return;
        }
        *out = grown;
        *outCap = cap;
    }
    memcpy(*out + *outLen, line, n);
    *outLen += n;
}

static int MapCmpFloat(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;

    return (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
}

// One area's surfaces to map lines (appended to out). Returns the number of blocks.
static int MeshToBlocks(unsigned sceneId, const MapTri* floors, int nFloor, const MapTri* walls, int nWall,
                        const MapWater* water, int nWat, const MapSeed* seeds, int nSeed, char** out, size_t* outLen,
                        size_t* outCap) {
    MapGrid g;
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    int nCols, k, i;
    int* colCount = NULL;   // hits per column, then layer start per column (nCols + 1)
    MapHit* sorted = NULL;
    int* layStart = NULL;   // first node of each column (nCols + 1)
    float* nodeY = NULL;
    unsigned char* nodeFlags = NULL; // 1 water, 2 reached
    int* nodeCol = NULL;
    int nNodes = 0;
    int bW = 0, bH = 0;
    int* bStart = NULL;
    int* bucket = NULL;
    int* bFill = NULL;
    int nHubs = 0;
    float* hubLo = NULL;
    float* hubHi = NULL;
    int* pairNode = NULL;
    int* pairHub = NULL;
    int nPairs = 0, capPairs = 0, capPairs2 = 0;
    int* nodeHubStart = NULL;
    int* nodeHubs = NULL;
    int* hubNodeStart = NULL;
    int* hubNodes = NULL;
    unsigned char* hubReached = NULL;
    int* queue = NULL;
    int qHead = 0, qTail = 0;
    int nSeeded = 0;
    int blocks = 0;

    memset(&g, 0, sizeof(g));
    if ((nFloor <= 0) || (floors == NULL)) {
        return 0;
    }
    for (k = 0; k < nFloor; k++) {
        for (i = 0; i < 3; i++) {
            minX = MapMinF(minX, floors[k].x[i]);
            maxX = MapMaxF(maxX, floors[k].x[i]);
            minZ = MapMinF(minZ, floors[k].z[i]);
            maxZ = MapMaxF(maxZ, floors[k].z[i]);
        }
    }
    // The grid starts on a cell edge, with a cell of room around the floors.
    g.ox = floorf(minX / RM_CELL) * RM_CELL - RM_CELL;
    g.oz = floorf(minZ / RM_CELL) * RM_CELL - RM_CELL;
    for (g.S = RM_SAMPLE;; g.S *= 2) {
        g.W = (int)ceilf((maxX - g.ox) / g.S) + RM_CELL / g.S + 1;
        g.H = (int)ceilf((maxZ - g.oz) / g.S) + RM_CELL / g.S + 1;
        g.W = (g.W + RM_CELL / g.S - 1) / (RM_CELL / g.S) * (RM_CELL / g.S); // whole cells
        g.H = (g.H + RM_CELL / g.S - 1) / (RM_CELL / g.S) * (RM_CELL / g.S);
        if (((long long)g.W * g.H <= RM_COLS_MAX) || (g.S >= RM_CELL)) {
            break;
        }
    }
    if ((g.W <= 0) || (g.H <= 0) || ((long long)g.W * g.H > 4 * RM_COLS_MAX)) {
        return 0;
    }
    nCols = g.W * g.H;

    // 1. Floor and water heights, sample by sample.
    for (k = 0; k < nFloor; k++) {
        MapRasterFloor(&g, &floors[k]);
    }
    for (k = 0; k < nWat; k++) {
        int ix0 = MapMaxI(MapSampleIndex(water[k].minX, g.ox, g.S), 0);
        int ix1 = MapMinI(MapSampleIndex(water[k].maxX, g.ox, g.S), g.W - 1);
        int iz0 = MapMaxI(MapSampleIndex(water[k].minZ, g.oz, g.S), 0);
        int iz1 = MapMinI(MapSampleIndex(water[k].maxZ, g.oz, g.S), g.H - 1);
        int ix, iz;

        for (iz = iz0; iz <= iz1; iz++) {
            for (ix = ix0; ix <= ix1; ix++) {
                MapAddHit(&g, ix, iz, water[k].y, 1);
            }
        }
    }
    if (g.nHits == 0) {
        goto done;
    }

    // 2. Hits sorted by column, then merged into at most RM_LAYERS floors per column.
    colCount = (int*)calloc((size_t)nCols + 1, sizeof(int));
    sorted = (MapHit*)malloc(sizeof(MapHit) * (size_t)g.nHits);
    layStart = (int*)calloc((size_t)nCols + 1, sizeof(int));
    nodeY = (float*)malloc(sizeof(float) * (size_t)g.nHits);
    nodeFlags = (unsigned char*)calloc((size_t)g.nHits, 1);
    nodeCol = (int*)malloc(sizeof(int) * (size_t)g.nHits);
    if (!colCount || !sorted || !layStart || !nodeY || !nodeFlags || !nodeCol) {
        goto done;
    }
    for (k = 0; k < g.nHits; k++) {
        colCount[g.hits[k].col + 1]++;
    }
    for (k = 0; k < nCols; k++) {
        colCount[k + 1] += colCount[k];
    }
    for (k = 0; k < g.nHits; k++) {
        sorted[colCount[g.hits[k].col]++] = g.hits[k];
    }
    // colCount[c] now holds the end of column c's hits (and the start of c + 1).
    for (k = 0; k < nCols; k++) {
        int h0 = (k == 0) ? 0 : colCount[k - 1], h1 = colCount[k];
        float fy[128], wy[16];
        int nf = 0, nw = 0;
        float ly[RM_LAYERS * 2 + 16];
        unsigned char lw[RM_LAYERS * 2 + 16];
        int nl = 0;
        int a, b;

        layStart[k] = nNodes;
        for (a = h0; a < h1; a++) {
            if (sorted[a].water) {
                if (nw < 16) {
                    wy[nw++] = sorted[a].y;
                }
            } else if (nf < 128) {
                fy[nf++] = sorted[a].y;
            }
        }
        if ((nf == 0) && (nw == 0)) {
            continue;
        }
        qsort(fy, nf, sizeof(float), MapCmpFloat);
        // Floors: heights within RM_MERGE of each other chain into one (the top one).
        for (a = 0; a < nf; a++) {
            if ((nl > 0) && (fy[a] - ly[nl - 1] < RM_MERGE)) {
                ly[nl - 1] = fy[a];
            } else if (nl < RM_LAYERS * 2) {
                ly[nl] = fy[a];
                lw[nl] = 0;
                nl++;
            } else {
                ly[nl - 1] = fy[a]; // too many: keep the top ones
            }
        }
        // Water: dropped where a floor sits just under it (wading) or just over it.
        for (a = 0; a < nw; a++) {
            int skip = 0;

            for (b = 0; b < nl; b++) {
                if (!lw[b] && (ly[b] >= wy[a] - RM_SHALLOW) && (ly[b] <= wy[a] + RM_COVERED)) {
                    skip = 1;
                }
                if (lw[b] && (fabsf(ly[b] - wy[a]) < RM_MERGE)) {
                    skip = 1;
                }
            }
            if (!skip && (nl < (int)(sizeof(ly) / sizeof(ly[0])))) {
                // insert keeping heights in order
                for (b = nl; (b > 0) && (ly[b - 1] > wy[a]); b--) {
                    ly[b] = ly[b - 1];
                    lw[b] = lw[b - 1];
                }
                ly[b] = wy[a];
                lw[b] = 1;
                nl++;
            }
        }
        // Only the top RM_LAYERS are kept.
        for (a = MapMaxI(nl - RM_LAYERS, 0); a < nl; a++) {
            nodeY[nNodes] = ly[a];
            nodeFlags[nNodes] = lw[a] ? 1 : 0;
            nodeCol[nNodes] = k;
            nNodes++;
        }
    }
    layStart[nCols] = nNodes;
    free(g.hits);
    g.hits = NULL;
    free(sorted);
    sorted = NULL;
    if (nNodes == 0) {
        goto done;
    }

    // 3. Walls bucketed by the 64-unit cells near them.
    bW = g.W * g.S / RM_CELL + 1;
    bH = g.H * g.S / RM_CELL + 1;
    bStart = (int*)calloc((size_t)bW * bH + 1, sizeof(int));
    bFill = (int*)calloc((size_t)bW * bH, sizeof(int));
    if (!bStart || !bFill) {
        goto done;
    }
    {
        float pad = 1.5f * (float)g.S;
        long long total = 0;

        for (k = 0; k < nWall; k++) {
            total += MapBucketWall(&walls[k], k, g.ox, g.oz, bW, bH, pad, bStart, NULL, NULL);
            if (total > RM_BUCKET_MAX) {
                goto done;
            }
        }
        for (k = 0; k < bW * bH; k++) {
            bStart[k + 1] += bStart[k];
        }
        bucket = (int*)malloc(sizeof(int) * ((size_t)bStart[bW * bH] + 1));
        if (bucket == NULL) {
            goto done;
        }
        for (k = 0; k < nWall; k++) {
            MapBucketWall(&walls[k], k, g.ox, g.oz, bW, bH, pad, bStart, bFill, bucket);
        }
    }

    // 4. Ladders and vines: floor near the bottom and floor near the top of each
    //    climbable group join up through a hub node, both ways.
    for (k = 0; k < nWall; k++) {
        if ((walls[k].group >= 0) && (walls[k].group < RM_TRIS_MAX)) {
            nHubs = MapMaxI(nHubs, walls[k].group + 1);
        }
    }
    if (nHubs > 0) {
        float reach = MapMaxF(RM_CLIMB_REACH, 0.75f * (float)g.S);

        hubLo = (float*)malloc(sizeof(float) * nHubs);
        hubHi = (float*)malloc(sizeof(float) * nHubs);
        if (!hubLo || !hubHi) {
            goto done;
        }
        for (k = 0; k < nHubs; k++) {
            hubLo[k] = 1e30f;
            hubHi[k] = -1e30f;
        }
        for (k = 0; k < nWall; k++) {
            int h = walls[k].group;

            if ((h < 0) || (h >= nHubs)) {
                continue;
            }
            for (i = 0; i < 3; i++) {
                hubLo[h] = MapMinF(hubLo[h], walls[k].y[i]);
                hubHi[h] = MapMaxF(hubHi[h], walls[k].y[i]);
            }
        }
        for (k = 0; k < nWall; k++) {
            const MapTri* w = &walls[k];
            int h = w->group;
            int ix0, ix1, iz0, iz1, ix, iz;

            if ((h < 0) || (h >= nHubs)) {
                continue;
            }
            ix0 = MapMaxI(MapSampleIndex(MapMinF(w->x[0], MapMinF(w->x[1], w->x[2])) - reach, g.ox, g.S), 0);
            ix1 = MapMinI(MapSampleIndex(MapMaxF(w->x[0], MapMaxF(w->x[1], w->x[2])) + reach, g.ox, g.S), g.W - 1);
            iz0 = MapMaxI(MapSampleIndex(MapMinF(w->z[0], MapMinF(w->z[1], w->z[2])) - reach, g.oz, g.S), 0);
            iz1 = MapMinI(MapSampleIndex(MapMaxF(w->z[0], MapMaxF(w->z[1], w->z[2])) + reach, g.oz, g.S), g.H - 1);
            for (iz = iz0; iz <= iz1; iz++) {
                for (ix = ix0; ix <= ix1; ix++) {
                    int c = iz * g.W + ix;
                    int j;

                    if ((layStart[c] == layStart[c + 1]) ||
                        (MapDistTriXZ(w, g.ox + (ix + 0.5f) * g.S, g.oz + (iz + 0.5f) * g.S) > reach)) {
                        continue;
                    }
                    for (j = layStart[c]; j < layStart[c + 1]; j++) {
                        float y = nodeY[j];
                        int bottom = (y >= hubLo[h] - 60.0f) && (y <= hubLo[h] + 80.0f);
                        int top = (y >= hubHi[h] - 40.0f) && (y <= hubHi[h] + 60.0f);

                        if (!bottom && !top) {
                            continue;
                        }
                        if ((nPairs >= 4000000) || !MapGrow((void**)&pairNode, &capPairs, nPairs + 1, sizeof(int)) ||
                            !MapGrow((void**)&pairHub, &capPairs2, nPairs + 1, sizeof(int))) {
                            continue;
                        }
                        pairNode[nPairs] = j;
                        pairHub[nPairs] = h;
                        nPairs++;
                    }
                }
            }
        }
        nodeHubStart = (int*)calloc((size_t)nNodes + 1, sizeof(int));
        hubNodeStart = (int*)calloc((size_t)nHubs + 1, sizeof(int));
        nodeHubs = (int*)malloc(sizeof(int) * ((size_t)nPairs + 1));
        hubNodes = (int*)malloc(sizeof(int) * ((size_t)nPairs + 1));
        hubReached = (unsigned char*)calloc(nHubs, 1);
        if (!nodeHubStart || !hubNodeStart || !nodeHubs || !hubNodes || !hubReached) {
            goto done;
        }
        for (k = 0; k < nPairs; k++) {
            nodeHubStart[pairNode[k] + 1]++;
            hubNodeStart[pairHub[k] + 1]++;
        }
        for (k = 0; k < nNodes; k++) {
            nodeHubStart[k + 1] += nodeHubStart[k];
        }
        for (k = 0; k < nHubs; k++) {
            hubNodeStart[k + 1] += hubNodeStart[k];
        }
        {
            int* fa = (int*)calloc((size_t)nNodes, sizeof(int));
            int* fb = (int*)calloc((size_t)nHubs, sizeof(int));

            if (!fa || !fb) {
                free(fa);
                free(fb);
                goto done;
            }
            for (k = 0; k < nPairs; k++) {
                nodeHubs[nodeHubStart[pairNode[k]] + fa[pairNode[k]]++] = pairHub[k];
                hubNodes[hubNodeStart[pairHub[k]] + fb[pairHub[k]]++] = pairNode[k];
            }
            free(fa);
            free(fb);
        }
    }

    // 5. Seeds: every way in and every actor, snapped to the floor near it.
    queue = (int*)malloc(sizeof(int) * ((size_t)nNodes + nHubs + 1));
    if (queue == NULL) {
        goto done;
    }
    for (k = 0; k < nSeed; k++) {
        int r = (int)ceilf(RM_SEED_REACH / g.S);
        int cx = MapSampleIndex(seeds[k].x, g.ox, g.S), cz = MapSampleIndex(seeds[k].z, g.oz, g.S);
        int ix, iz, best = -1;
        float bestScore = 1e30f;

        for (iz = MapMaxI(cz - r, 0); iz <= MapMinI(cz + r, g.H - 1); iz++) {
            for (ix = MapMaxI(cx - r, 0); ix <= MapMinI(cx + r, g.W - 1); ix++) {
                int c = iz * g.W + ix;
                float dx = g.ox + (ix + 0.5f) * g.S - seeds[k].x, dz = g.oz + (iz + 0.5f) * g.S - seeds[k].z;
                float dh = sqrtf(dx * dx + dz * dz);
                int j;

                if (dh > RM_SEED_REACH) {
                    continue;
                }
                for (j = layStart[c]; j < layStart[c + 1]; j++) {
                    float dy = nodeY[j] - seeds[k].y;
                    float score;

                    if ((dy < -RM_SEED_BELOW) || (dy > RM_SEED_ABOVE)) {
                        continue;
                    }
                    // Prefer the floor under the actor: height above counts more.
                    score = dh + ((dy <= 0.0f) ? -dy * 0.25f : dy * 2.0f);
                    if (score < bestScore) {
                        bestScore = score;
                        best = j;
                    }
                }
            }
        }
        if ((best >= 0) && !(nodeFlags[best] & 2)) {
            nodeFlags[best] |= 2;
            queue[qTail++] = best;
            nSeeded++;
        }
    }
    if (nSeeded == 0) {
        // No way in found: call every surface reachable (better than an all-dim map).
        for (k = 0; k < nNodes; k++) {
            nodeFlags[k] |= 2;
        }
    }

    // 6. Walk outward.
    while (qHead < qTail) {
        int node = queue[qHead++];
        static const int sDir[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
        int c, cx, cz, d, bc;
        float ya, ax, az, up;

        if (node >= nNodes) { // a ladder or vine group: every floor at it
            int h = node - nNodes;

            for (k = hubNodeStart[h]; k < hubNodeStart[h + 1]; k++) {
                int nn = hubNodes[k];

                if (!(nodeFlags[nn] & 2)) {
                    nodeFlags[nn] |= 2;
                    queue[qTail++] = nn;
                }
            }
            continue;
        }
        if (nodeHubStart != NULL) {
            for (k = nodeHubStart[node]; k < nodeHubStart[node + 1]; k++) {
                int h = nodeHubs[k];

                if (!hubReached[h]) {
                    hubReached[h] = 1;
                    queue[qTail++] = nNodes + h;
                }
            }
        }
        c = nodeCol[node];
        cx = c % g.W;
        cz = c / g.W;
        ya = nodeY[node];
        ax = g.ox + (cx + 0.5f) * g.S;
        az = g.oz + (cz + 0.5f) * g.S;
        up = (nodeFlags[node] & 1) ? RM_WATER_OUT : RM_STEP_UP;
        bc = (cz * g.S / RM_CELL) * bW + (cx * g.S / RM_CELL);
        for (d = 0; d < 8; d++) {
            int nx = cx + sDir[d][0], nz = cz + sDir[d][1];
            int nc, j;
            float bx, bz;

            if ((nx < 0) || (nz < 0) || (nx >= g.W) || (nz >= g.H)) {
                continue;
            }
            nc = nz * g.W + nx;
            bx = g.ox + (nx + 0.5f) * g.S;
            bz = g.oz + (nz + 0.5f) * g.S;
            for (j = layStart[nc]; j < layStart[nc + 1]; j++) {
                float yb = nodeY[j];
                float dy = yb - ya;
                float top = MapMaxF(ya, yb);
                int b, blocked = 0;

                if ((nodeFlags[j] & 2) || (dy > up) || (dy < -RM_DROP_MAX)) {
                    continue;
                }
                // A wall across the way at body height stops it (but not the face of a
                // ledge we drop off or step onto).
                for (b = bStart[bc]; (b < bStart[bc + 1]) && !blocked; b++) {
                    blocked = MapWallCuts(&walls[bucket[b]], ax, az, bx, bz, top + RM_BODY_LO, top + RM_BODY_HI);
                }
                if (!blocked) {
                    nodeFlags[j] |= 2;
                    queue[qTail++] = j;
                }
            }
        }
    }

    // 7. Out to 16 x 16 cell blocks.
    {
        int per = RM_CELL / g.S; // samples per cell side
        int cW = g.W / per, cH = g.H / per;
        int gcx0 = (int)floorf(g.ox / RM_CELL), gcz0 = (int)floorf(g.oz / RM_CELL);
        int bx0 = (int)floorf((float)gcx0 / 16.0f), bz0 = (int)floorf((float)gcz0 / 16.0f);
        int bx1 = (int)floorf((float)(gcx0 + cW - 1) / 16.0f), bz1 = (int)floorf((float)(gcz0 + cH - 1) / 16.0f);
        int bx, bz;

        for (bz = bz0; bz <= bz1; bz++) {
            for (bx = bx0; bx <= bx1; bx++) {
                float h[256];
                unsigned char kind[256]; // 0 none, 1 floor, 2 water
                unsigned char reach[256];
                float sortedH[256];
                int nh = 0;
                int any = 0;
                int c;
                int base = 0;
                char line[400];
                int n;

                for (c = 0; c < 256; c++) {
                    int ccx = bx * 16 + (c % 16) - gcx0, ccz = bz * 16 + (c / 16) - gcz0;
                    float rY = -1e30f, aY = -1e30f;
                    int rW = 0, aW = 0, hasR = 0, hasA = 0;
                    int sx, sz;

                    kind[c] = 0;
                    reach[c] = 0;
                    if ((ccx < 0) || (ccz < 0) || (ccx >= cW) || (ccz >= cH)) {
                        continue;
                    }
                    for (sz = ccz * per; sz < (ccz + 1) * per; sz++) {
                        for (sx = ccx * per; sx < (ccx + 1) * per; sx++) {
                            int col = sz * g.W + sx;
                            int j;

                            for (j = layStart[col]; j < layStart[col + 1]; j++) {
                                if (nodeY[j] > aY) {
                                    aY = nodeY[j];
                                    aW = nodeFlags[j] & 1;
                                    hasA = 1;
                                }
                                if ((nodeFlags[j] & 2) && (nodeY[j] > rY)) {
                                    rY = nodeY[j];
                                    rW = nodeFlags[j] & 1;
                                    hasR = 1;
                                }
                            }
                        }
                    }
                    if (hasR) {
                        reach[c] = 1;
                        h[c] = rY;
                        kind[c] = rW ? 2 : 1;
                    } else if (hasA) {
                        h[c] = aY;
                        kind[c] = aW ? 2 : 1;
                    }
                    if (kind[c] == 1) {
                        sortedH[nh++] = h[c];
                    }
                    any |= kind[c];
                }
                if (!any) {
                    continue;
                }
                if (nh > 0) {
                    qsort(sortedH, nh, sizeof(float), MapCmpFloat);
                    base = (int)floorf(sortedH[nh / 2] / 40.0f + 0.5f) * 40;
                }
                n = snprintf(line, sizeof(line), "%u|%d|%d|%d|", sceneId, bx, bz, base);
                if ((n <= 0) || (n > 60)) {
                    continue;
                }
                for (c = 0; c < 256; c++) {
                    char ch = '.';

                    if (kind[c] == 2) {
                        ch = '~';
                    } else if (kind[c] == 1) {
                        int code = (int)floorf((h[c] - (float)base) / 40.0f + 0.5f) + 32;

                        ch = (char)(0x30 + ((code < 0) ? 0 : (code > 63) ? 63 : code));
                    }
                    line[n++] = ch;
                }
                for (c = 0; c < 64; c++) {
                    int nib = reach[c * 4] | (reach[c * 4 + 1] << 1) | (reach[c * 4 + 2] << 2) | (reach[c * 4 + 3] << 3);

                    line[n++] = "0123456789abcdef"[nib];
                }
                line[n++] = '\n';
                MapAppend(out, outLen, outCap, line, n);
                blocks++;
            }
        }
    }

done:
    free(g.hits);
    free(colCount);
    free(sorted);
    free(layStart);
    free(nodeY);
    free(nodeFlags);
    free(nodeCol);
    free(bStart);
    free(bFill);
    free(bucket);
    free(hubLo);
    free(hubHi);
    free(pairNode);
    free(pairHub);
    free(nodeHubStart);
    free(nodeHubs);
    free(hubNodeStart);
    free(hubNodes);
    free(hubReached);
    free(queue);
    return blocks;
}

// =====================================================================================
// Reading an area from the ROM: its collision, ways in, and every room's actors.
// =====================================================================================

// Adds a seed from three s16s (x, y, z) at `at`, if they fit in the file.
static void MapSeedAt(const uint8_t* f, uint32_t len, uint32_t at, MapSeed* seeds, int* n, int max) {
    if ((*n >= max) || (at > len) || (len - at < 6)) {
        return;
    }
    seeds[*n].x = (float)(int16_t)BE16(f + at);
    seeds[*n].y = (float)(int16_t)BE16(f + at + 2);
    seeds[*n].z = (float)(int16_t)BE16(f + at + 4);
    (*n)++;
}

// A room file's actor list (header command 0x01): 16-byte entries, id then x, y, z.
// Returns how many actors were added.
static int MapRoomActors(const uint8_t* room, uint32_t len, MapSeed* seeds, int* n, int max) {
    uint32_t off;
    int added = 0;

    if (room == NULL) {
        return 0;
    }
    for (off = 0; (off + 8 <= len) && (off < 0x200); off += 8) {
        if (room[off] == 0x14) {
            break;
        }
        if (room[off] == 0x01) {
            uint32_t count = room[off + 1];
            uint32_t list = BE32(room + off + 4) & 0xFFFFFF;
            uint32_t a;

            for (a = 0; a < count; a++) {
                uint32_t at = list + a * 16;
                int before = *n;

                if ((at > len) || (len - at < 16)) {
                    break;
                }
                MapSeedAt(room, len, at + 2, seeds, n, max);
                added += *n - before;
            }
        }
    }
    return added;
}

// Union-find for grouping ladder and vine triangles that share corners.
static int MapRoot(int* parent, int a) {
    while (parent[a] != a) {
        parent[a] = parent[parent[a]];
        a = parent[a];
    }
    return a;
}

static int SceneToBlocks(uint16_t sceneId, uint32_t dmaIndex, char** out, size_t* outLen, size_t* outCap) {
    uint32_t len = 0;
    uint8_t* scene = RomFile(dmaIndex, &len);
    uint32_t off;
    uint32_t col = 0;
    uint32_t spawnOff = 0, spawnN = 0, doorOff = 0, doorN = 0, roomOff = 0, roomN = 0;
    uint32_t nVtx, vtxOff, nPoly, polyOff, surfOff, nWater, waterOff;
    MapTri* floors = NULL;
    MapTri* walls = NULL;
    int nFloor = 0, nWall = 0;
    MapWater* water = NULL;
    int nWat = 0;
    MapSeed* seeds = NULL;
    int nSeed = 0;
    int* parent = NULL;
    int* groupOf = NULL;
    int* wallVtx = NULL; // a corner of each climbable wall
    uint32_t i;
    int blocks = 0;

    if (scene == NULL) {
        return 0;
    }
    // Scene header: 0x00 spawn points, 0x03 collision, 0x04 rooms, 0x0E doors, 0x14 the end.
    for (off = 0; (off + 8 <= len) && (off < 0x200); off += 8) {
        uint8_t cmd = scene[off];

        if (cmd == 0x14) {
            break;
        }
        if (cmd == 0x03) {
            col = BE32(scene + off + 4) & 0xFFFFFF;
        } else if (cmd == 0x00) {
            spawnN = scene[off + 1];
            spawnOff = BE32(scene + off + 4) & 0xFFFFFF;
        } else if (cmd == 0x0E) {
            doorN = scene[off + 1];
            doorOff = BE32(scene + off + 4) & 0xFFFFFF;
        } else if (cmd == 0x04) {
            roomN = scene[off + 1];
            roomOff = BE32(scene + off + 4) & 0xFFFFFF;
        }
    }
    if ((col == 0) || (col > len) || (len - col < 0x2C)) {
        goto done;
    }
    nVtx = BE16(scene + col + 0x0C);
    vtxOff = BE32(scene + col + 0x10) & 0xFFFFFF;
    nPoly = BE16(scene + col + 0x14);
    polyOff = BE32(scene + col + 0x18) & 0xFFFFFF;
    surfOff = BE32(scene + col + 0x1C) & 0xFFFFFF;
    nWater = BE16(scene + col + 0x24);
    waterOff = BE32(scene + col + 0x28) & 0xFFFFFF;
    if ((nVtx == 0) || (nPoly == 0) || (vtxOff > len) || (nVtx > (len - vtxOff) / 6) || (polyOff > len) ||
        (nPoly > (len - polyOff) / 16)) {
        goto done;
    }
    floors = (MapTri*)malloc(sizeof(MapTri) * (nPoly + 1));
    walls = (MapTri*)malloc(sizeof(MapTri) * (nPoly + 1));
    parent = (int*)malloc(sizeof(int) * (nVtx + 1));
    groupOf = (int*)malloc(sizeof(int) * (nVtx + 1));
    wallVtx = (int*)malloc(sizeof(int) * (nPoly + 1));
    seeds = (MapSeed*)malloc(sizeof(MapSeed) * RM_SEEDS_MAX);
    if (!floors || !walls || !parent || !groupOf || !wallVtx || !seeds) {
        goto done;
    }
    for (i = 0; i < nVtx; i++) {
        parent[i] = (int)i;
        groupOf[i] = -1;
    }
    for (i = 0; i < nPoly; i++) {
        const uint8_t* p = scene + polyOff + i * 16;
        uint32_t idx[3];
        uint32_t type = BE16(p);
        int16_t ny = (int16_t)BE16(p + 0xA);
        uint32_t wallType = 0;
        MapTri* t;
        int v;

        idx[0] = BE16(p + 2) & 0x1FFF;
        idx[1] = BE16(p + 4) & 0x1FFF;
        idx[2] = BE16(p + 6) & 0x1FFF;
        if ((idx[0] >= nVtx) || (idx[1] >= nVtx) || (idx[2] >= nVtx)) {
            continue;
        }
        if (ny >= 0x2800) { // floor (up to about 72 degrees steep)
            t = &floors[nFloor++];
        } else if (ny > -0x2800) { // wall
            t = &walls[nWall++];
        } else {
            continue; // ceiling
        }
        for (v = 0; v < 3; v++) {
            const uint8_t* q = scene + vtxOff + idx[v] * 6;

            t->x[v] = (float)(int16_t)BE16(q);
            t->y[v] = (float)(int16_t)BE16(q + 2);
            t->z[v] = (float)(int16_t)BE16(q + 4);
        }
        t->group = -1;
        // Wall type: bits 21..25 of the surface type's first word. Types 2, 3 and 4
        // set WALL_FLAG_1, WALL_FLAG_2 and WALL_FLAG_3, which the player climbs
        // (ladders and vines).
        if ((surfOff != 0) && (surfOff <= len) && (type < (len - surfOff) / 8)) {
            wallType = (BE32(scene + surfOff + type * 8) >> 21) & 0x1F;
        }
        if ((ny < 0x2800) && (wallType >= 2) && (wallType <= 4)) {
            int ra = MapRoot(parent, (int)idx[0]);

            parent[MapRoot(parent, (int)idx[1])] = ra;
            parent[MapRoot(parent, (int)idx[2])] = ra;
            t->group = -2; // climbable, group given below
            wallVtx[nWall - 1] = (int)idx[0];
        }
    }
    // Climbable triangles sharing corners form one ladder or vine wall.
    {
        int nGroups = 0;
        int k;

        for (k = 0; k < nWall; k++) {
            int r;

            if (walls[k].group != -2) {
                continue;
            }
            r = MapRoot(parent, wallVtx[k]);
            if (groupOf[r] < 0) {
                groupOf[r] = nGroups++;
            }
            walls[k].group = groupOf[r];
        }
    }
    if ((nWater > 0) && (waterOff <= len) && (nWater <= (len - waterOff) / 16)) {
        water = (MapWater*)malloc(sizeof(MapWater) * nWater);
        for (i = 0; (water != NULL) && (i < nWater); i++) {
            const uint8_t* w = scene + waterOff + i * 16;
            int16_t xl = (int16_t)BE16(w + 6), zl = (int16_t)BE16(w + 8);

            if ((xl <= 0) || (zl <= 0)) {
                continue;
            }
            water[nWat].minX = (float)(int16_t)BE16(w);
            water[nWat].y = (float)(int16_t)BE16(w + 2);
            water[nWat].minZ = (float)(int16_t)BE16(w + 4);
            water[nWat].maxX = water[nWat].minX + (float)xl;
            water[nWat].maxZ = water[nWat].minZ + (float)zl;
            nWat++;
        }
    }
    if (nFloor == 0) {
        goto done;
    }
    // Seeds: spawn points (ActorEntry, position at +2) and doors (position at +6).
    for (i = 0; i < spawnN; i++) {
        MapSeedAt(scene, len, spawnOff + i * 16 + 2, seeds, &nSeed, RM_SEEDS_MAX);
    }
    for (i = 0; i < doorN; i++) {
        MapSeedAt(scene, len, doorOff + i * 16 + 6, seeds, &nSeed, RM_SEEDS_MAX);
    }
    // Every room's actors: the room list gives each room's ROM address, which is
    // found in the file table.
    for (i = 0; i < roomN; i++) {
        uint32_t at = roomOff + i * 8;
        int idx;
        uint32_t rLen = 0;
        uint8_t* room;

        if ((at > len) || (len - at < 8)) {
            break;
        }
        idx = RomFindVrom(BE32(scene + at));
        if (idx < 0) {
            continue;
        }
        room = RomFile((uint32_t)idx, &rLen);
        if (room != NULL) {
            MapRoomActors(room, rLen, seeds, &nSeed, RM_SEEDS_MAX);
            free(room);
        }
    }
    blocks = MeshToBlocks(sceneId, floors, nFloor, walls, nWall, water, nWat, seeds, nSeed, out, outLen, outCap);

done:
    free(floors);
    free(walls);
    free(water);
    free(seeds);
    free(parent);
    free(groupOf);
    free(wallVtx);
    free(scene);
    return blocks;
}

#ifndef GE_ROMMAP_TEST
// 0 not started, 1 working, 2 done, 3 no ROM found, 4 upload failed
static volatile LONG sMapRomState = 0;

static int Http(const wchar_t* verb, const wchar_t* path, const char* body, int bodyLen, char* out, int outMax);

// Once per relay: build every area's map and send it up (format v3; the relay
// files it under maps_rom_v3).
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
    // 40 blocks per request (about 14 KB; the relay takes up to 60 lines and 40000 bytes).
    while (pos < len) {
        size_t end = pos;
        int lines = 0;

        while ((end < len) && (lines < 40)) {
            size_t next = end;

            while ((next < len) && (all[next] != '\n')) {
                next++;
            }
            next++;
            if ((lines > 0) && (next - pos > 39000)) {
                break;
            }
            end = next;
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
#endif // GE_ROMMAP_TEST
