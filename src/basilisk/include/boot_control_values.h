#ifndef BOOT_CONTROL_VALUES_H
#define BOOT_CONTROL_VALUES_H

#include <stddef.h>
#include <string.h>

// Hex keeps filenames, spaces and UTF-8 independent of the line protocol.
// A dash represents an empty value. Settings are one line each on the SD card.
static inline int BootControlHexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static inline bool BootControlDecode(const char *hex, char *out, size_t capacity)
{
    if (!hex || !out || !capacity) return false;
    if (strcmp(hex, "-") == 0) { out[0] = 0; return true; }
    size_t n = strlen(hex);
    if (!n || n % 2 || n / 2 >= capacity) return false;
    for (size_t i = 0; i < n; i += 2) {
        int hi = BootControlHexDigit(hex[i]), lo = BootControlHexDigit(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        unsigned char ch = (unsigned char)((hi << 4) | lo);
        if (ch < 32 || ch == 127) return false;
        out[i / 2] = (char)ch;
    }
    out[n / 2] = 0;
    return true;
}

static inline bool BootControlBool(const char *value, bool *out)
{
    if (!strcmp(value, "true")) { *out = true; return true; }
    if (!strcmp(value, "false")) { *out = false; return true; }
    return false;
}

// Guest RAM sizes offered at startup, in MB. Of the 32 MB of PSRAM, the panel
// framebuffer, ROM and emulator tables need about 4 MB, so 24 MB of guest RAM
// still leaves several MB for runtime buffers.
static const int kBootRamChoicesMB[] = {8, 16, 20, 24};
static const int kBootRamChoiceCount = (int)(sizeof(kBootRamChoicesMB) / sizeof(kBootRamChoicesMB[0]));
static const int kBootRamDefaultMB = 8;

// Older firmware offered 4 and 12 MB. Round a saved size up to the next
// choice so an existing setup never gets less memory than before.
static inline int BootControlSavedRam(int mb)
{
    if (mb <= 0) return kBootRamDefaultMB;
    for (int i = 0; i < kBootRamChoiceCount; ++i) {
        if (mb <= kBootRamChoicesMB[i]) return kBootRamChoicesMB[i];
    }
    return kBootRamChoicesMB[kBootRamChoiceCount - 1];
}

static inline bool BootControlChoice(const char *value, bool ram, int *out)
{
    // Plain digits only: no sign, spaces or leading zero.
    if (!value || !*value || (value[0] == '0' && value[1])) return false;
    int n = 0;
    for (const char *p = value; *p; ++p) {
        if (*p < '0' || *p > '9' || n > 1000) return false;
        n = n * 10 + (*p - '0');
    }
    static const int kRotations[] = {0, 180};
    const int *choices = ram ? kBootRamChoicesMB : kRotations;
    const int count = ram ? kBootRamChoiceCount : 2;
    for (int i = 0; i < count; ++i) {
        if (n == choices[i]) { *out = n; return true; }
    }
    return false;
}
#endif
