// Host stand-in for the Arduino core, for native tests of the CPU emulation.
#ifndef AOT_HOST_ARDUINO_H
#define AOT_HOST_ARDUINO_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IRAM_ATTR
#define DRAM_ATTR
#define EXT_RAM_ATTR

struct HostSerial {
    int printf(const char *format, ...) __attribute__((format(printf, 2, 3)))
    {
        va_list args;
        va_start(args, format);
        int n = vfprintf(stderr, format, args);
        va_end(args);
        return n;
    }
    void println(const char *text = "") { fprintf(stderr, "%s\n", text); }
    void print(const char *text) { fputs(text, stderr); }
    void flush() {}
};
extern HostSerial Serial;

static inline unsigned long millis() { return 0; }
static inline unsigned long micros() { return 0; }
static inline void delay(unsigned long) {}

#endif
