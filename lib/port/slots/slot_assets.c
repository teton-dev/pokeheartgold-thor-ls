#include "slot_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t R32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

uint8_t *SlotAssets_ReadFile(const char *dir, const char *name, uint32_t *size) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = length > 0 ? malloc(length) : NULL;
    if (data != NULL && fread(data, 1, length, f) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data != NULL) {
        *size = (uint32_t)length;
    }
    return data;
}

int SlotNarc_Open(SlotNarc *narc, const char *dir, const char *path) {
    memset(narc, 0, sizeof(*narc));
    narc->data = SlotAssets_ReadFile(dir, path, &narc->size);
    if (narc->data == NULL || narc->size < 0x2C || memcmp(narc->data, "NARC", 4) != 0
        || memcmp(narc->data + 0x10, "BTAF", 4) != 0) {
        SlotNarc_Close(narc);
        return 0;
    }
    uint32_t btafSize = R32(narc->data + 0x14);
    narc->count = (uint16_t)(narc->data[0x18] | narc->data[0x19] << 8);
    narc->fatOffset = 0x1C;
    uint32_t btnf = 0x10 + btafSize;
    if (btnf + 8 > narc->size || memcmp(narc->data + btnf, "BTNF", 4) != 0) {
        SlotNarc_Close(narc);
        return 0;
    }
    uint32_t gmif = btnf + R32(narc->data + btnf + 4);
    if (gmif + 8 > narc->size || memcmp(narc->data + gmif, "GMIF", 4) != 0
        || narc->fatOffset + (uint32_t)narc->count * 8 > narc->size) {
        SlotNarc_Close(narc);
        return 0;
    }
    narc->imgOffset = gmif + 8;
    return 1;
}

void SlotNarc_Close(SlotNarc *narc) {
    free(narc->data);
    memset(narc, 0, sizeof(*narc));
}

SlotBlock SlotNarc_Member(const SlotNarc *narc, unsigned index) {
    SlotBlock none = { NULL, 0 };
    if (narc->data == NULL || index >= narc->count) {
        return none;
    }
    uint32_t start = R32(narc->data + narc->fatOffset + index * 8);
    uint32_t end = R32(narc->data + narc->fatOffset + index * 8 + 4);
    if (end < start || narc->imgOffset + end > narc->size) {
        return none;
    }
    SlotBlock block = { narc->data + narc->imgOffset + start, end - start };
    return block;
}

// The section called tag in a Gen 4 graphics file (the header is 0x10 bytes), its body after the 8-byte tag
static const uint8_t *Section(SlotBlock file, const char *tag, uint32_t *bodySize) {
    uint32_t at = 0x10;
    while (at + 8 <= file.size) {
        uint32_t size = R32(file.data + at + 4);
        if (size < 8 || at + size > file.size) {
            return NULL;
        }
        if (memcmp(file.data + at, tag, 4) == 0) {
            *bodySize = size - 8;
            return file.data + at + 8;
        }
        at += size;
    }
    return NULL;
}

SlotBlock SlotGfx_Palette(SlotBlock file) {
    SlotBlock out = { NULL, 0 };
    uint32_t n;
    const uint8_t *p = Section(file, "TTLP", &n);
    if (p != NULL && n >= 16) {
        uint32_t size = R32(p + 8);
        if (16 + size <= n) {
            out.data = p + 16;
            out.size = size;
        }
    }
    return out;
}

SlotBlock SlotGfx_Tiles(SlotBlock file) {
    SlotBlock out = { NULL, 0 };
    uint32_t n;
    const uint8_t *p = Section(file, "RAHC", &n);
    if (p != NULL && n >= 24) {
        uint32_t size = R32(p + 16);
        if (24 + size <= n) {
            out.data = p + 24;
            out.size = size;
        }
    }
    return out;
}

SlotBlock SlotGfx_Screen(SlotBlock file) {
    SlotBlock out = { NULL, 0 };
    uint32_t n;
    const uint8_t *p = Section(file, "NRCS", &n);
    if (p != NULL && n >= 12) {
        uint32_t size = R32(p + 8);
        if (12 + size <= n) {
            out.data = p + 12;
            out.size = size;
        }
    }
    return out;
}
