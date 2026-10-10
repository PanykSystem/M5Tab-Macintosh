// Host differential test of ahead-of-time translated ROM code.
//
//   aot_diff ROM.bin [--trials N] [--seed S] [--entry INDEX] [--verbose]
//
// Loads the ROM the image was translated from, installs the image and runs
// AotSelfTest() (src/basilisk/aot/aot_selftest.cpp), the same test the
// firmware's AOT_SELFTEST build runs on the device.
#include "sysdeps.h"
#include "cpu_emulation.h"
#include "m68k.h"
#include "memory.h"
#include "readcpu.h"
#include "newcpu.h"
#include "aot_runtime.h"
#include "aot_selftest.h"
#include "host_env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: aot_diff ROM.bin [--trials N] [--seed S] [--entry INDEX] [--verbose]\n");
        return 2;
    }
    AotSelfTestOptions options = {16, 1, 0x40000, -1, false, getenv("AOT_TRACE") != NULL};
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--trials") && i + 1 < argc) options.trials = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) options.seed = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--entry") && i + 1 < argc) options.only_entry = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--verbose")) options.verbose = true;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror(argv[1]);
        return 2;
    }
    std::vector<uint8_t> pristine(aot_image.rom_size + 64), patched;
    if (aot_image.entry_count + aot_image.ram_entry_count == 0 || fread(pristine.data(), 1, aot_image.rom_size, file) != aot_image.rom_size) {
        fprintf(stderr, "no translated image, or the ROM file is shorter than the image's ROM\n");
        return 2;
    }
    fclose(file);
    patched = pristine;

    std::vector<uint8_t> ram(0x10000);
    HostSetRam(ram.data(), ram.size());
    HostSetRom(patched.data(), aot_image.rom_base, aot_image.rom_size);
    cpufunctbl = (cpuop_func **)calloc(65536, sizeof(cpuop_func *));
    init_m68k();
    AotInstall();
    AotStats stats;
    AotReadStats(&stats);
    if (stats.regions_installed != stats.regions_total || stats.entries_installed != stats.entries_total) {
        fprintf(stderr, "FAIL: installed %u/%u regions, %u/%u entries\n", stats.regions_installed,
                stats.regions_total, stats.entries_installed, stats.entries_total);
        return 1;
    }

    AotSelfTestResult result;
    const bool ok = AotSelfTest(options, pristine.data(), &result);
    printf("%s: %u runs, %llu instructions compared, %u failures, %u skipped\n", ok ? "PASS" : "FAIL", result.runs,
           (unsigned long long)result.instructions, result.failures, result.skipped);
    return ok ? 0 : 1;
}
