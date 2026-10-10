/*
 *  aot_selftest.h - Differential test of translated ROM code
 *
 *  Runs every installed region entry from randomized CPU and memory states
 *  and compares the result with the interpreter executing the same number of
 *  instructions from the same state: registers, condition codes, PC, special
 *  flags, every byte of guest RAM and the sequence of stores outside RAM.
 *
 *  The same code runs in the host test (test/aot_host) and, built by the
 *  firmware toolchain, on the device (AOT_SELFTEST=1), because translated
 *  code is sensitive to how a compiler treats the handlers' C.
 */

#ifndef AOT_SELFTEST_H
#define AOT_SELFTEST_H

#include "sysdeps.h"

#ifndef AOT_SELFTEST
#define AOT_SELFTEST 0
#endif

struct AotSelfTestOptions {
	uae_u32 trials;          // random states per entry and pass
	uae_u32 seed;
	uae_u32 ram_size;        // test RAM, a power of two of at least 64 KB
	int only_entry;          // -1 for all
	bool verbose;            // describe every mismatch, not just the first few
	bool trace;              // print each interpreter step
};

struct AotSelfTestResult {
	uae_u32 runs;
	uae_u32 failures;
	uae_u32 skipped;
	uae_u64 instructions;
};

// `pristine_rom` is a copy of the ROM taken before AotInstall() patched it;
// AotInstall() must already have run. Guest memory mappings are replaced for
// the duration of the test and restored afterwards. Returns true when no
// comparison failed.
extern bool AotSelfTest(const AotSelfTestOptions &options, const uae_u8 *pristine_rom,
                        AotSelfTestResult *result);

#endif /* AOT_SELFTEST_H */
