/*
 *  aot_profile.h - Execution counts of ROM instructions
 *
 *  Profiling builds (CPU_AOT_PROFILE=1) count how often the interpreter
 *  dispatches each ROM instruction. tools/aot/aot_profile.py reads the counts
 *  over the automation port and writes the hot list that aot_translate.py
 *  translates. The counts also fix instruction boundaries: a sampled PC can
 *  lie inside an instruction, a counted address cannot.
 */

#ifndef AOT_PROFILE_H
#define AOT_PROFILE_H

#include "sysdeps.h"

#ifndef CPU_AOT_PROFILE
#define CPU_AOT_PROFILE 0
#endif

#define AOT_PROFILE_CHUNK 64   // counters per AotProfileRead()

// ROM: one saturating counter per even offset, or NULL while not profiling.
extern uae_u32 *aot_profile_counts;
extern uae_u32 aot_profile_slots;

// RAM is counted in pages that are allocated when code first runs in them:
// 2048 counters for each 4 KB of guest RAM, NULL where nothing ran.
#define AOT_PROFILE_PAGE_SHIFT 12
#define AOT_PROFILE_PAGE_SLOTS (1u << (AOT_PROFILE_PAGE_SHIFT - 1))
extern uae_u32 **aot_profile_ram_pages;
extern uae_u32 aot_profile_ram_page_count;
extern uae_u32 *AotProfileRamPage(uae_u32 page);   // allocates; NULL when out of memory

extern uae_u32 AotProfileRamMap(uae_u32 first_page, uae_u8 *bitmap, uae_u32 bytes);
// Bit i is set when chunk i (64 counters) of the page holds a count.
extern bool AotProfileRamChunks(uae_u32 page, uae_u32 *chunk_bits);
extern bool AotProfileRamRead(uae_u32 page, uae_u32 chunk, uae_u32 *counts);

extern bool AotProfileStart(void);       // allocate (once), clear and count
extern void AotProfileStop(void);        // stop counting; counts stay readable
extern uae_u32 AotProfileChunks(void);
// Bit i of byte b is set when chunk first_chunk + 8 * b + i holds a count.
extern uae_u32 AotProfileMap(uae_u32 first_chunk, uae_u8 *bitmap, uae_u32 bytes);
extern bool AotProfileRead(uae_u32 chunk, uae_u32 *counts);

#endif /* AOT_PROFILE_H */
