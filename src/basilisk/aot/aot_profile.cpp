/*
 *  aot_profile.cpp - Execution counts of ROM instructions
 */

#include "aot_profile.h"

#include "cpu_emulation.h"

#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

uae_u32 *aot_profile_counts = NULL;
uae_u32 aot_profile_slots = 0;
uae_u32 **aot_profile_ram_pages = NULL;
uae_u32 aot_profile_ram_page_count = 0;

#if CPU_AOT_PROFILE

static uae_u32 *profile_storage = NULL;
static uae_u32 **ram_pages = NULL;
static uae_u32 ram_page_count = 0;

static void *profile_alloc(size_t bytes)
{
#ifdef ARDUINO
	return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM);
#else
	return calloc(1, bytes);
#endif
}

uae_u32 *AotProfileRamPage(uae_u32 page)
{
	if (page >= ram_page_count)
		return NULL;
	if (ram_pages[page] == NULL)
		ram_pages[page] = (uae_u32 *)profile_alloc(AOT_PROFILE_PAGE_SLOTS * sizeof(uae_u32));
	return ram_pages[page];
}

static bool ram_chunk_used(const uae_u32 *counts, uae_u32 chunk)
{
	for (uae_u32 i = 0; i < AOT_PROFILE_CHUNK; ++i) {
		if (counts[chunk * AOT_PROFILE_CHUNK + i])
			return true;
	}
	return false;
}

uae_u32 AotProfileRamMap(uae_u32 first_page, uae_u8 *bitmap, uae_u32 bytes)
{
	uae_u32 used = 0;
	memset(bitmap, 0, bytes);
	for (uae_u32 i = 0; i < bytes * 8 && first_page + i < ram_page_count; ++i) {
		if (ram_pages[first_page + i] != NULL)
			bitmap[i / 8] |= (uae_u8)(1u << (i % 8));
		used = i / 8 + 1;
	}
	return used;
}

bool AotProfileRamChunks(uae_u32 page, uae_u32 *chunk_bits)
{
	if (page >= ram_page_count || ram_pages[page] == NULL)
		return false;
	*chunk_bits = 0;
	for (uae_u32 chunk = 0; chunk < AOT_PROFILE_PAGE_SLOTS / AOT_PROFILE_CHUNK; ++chunk) {
		if (ram_chunk_used(ram_pages[page], chunk))
			*chunk_bits |= 1u << chunk;
	}
	return true;
}

bool AotProfileRamRead(uae_u32 page, uae_u32 chunk, uae_u32 *counts)
{
	if (page >= ram_page_count || ram_pages[page] == NULL ||
	    chunk >= AOT_PROFILE_PAGE_SLOTS / AOT_PROFILE_CHUNK)
		return false;
	memcpy(counts, ram_pages[page] + chunk * AOT_PROFILE_CHUNK, AOT_PROFILE_CHUNK * sizeof(uae_u32));
	return true;
}

bool AotProfileStart(void)
{
	const uae_u32 slots = ROMSize / 2;
	if (slots == 0)
		return false;
	__atomic_store_n(&aot_profile_counts, (uae_u32 *)NULL, __ATOMIC_RELEASE);
	__atomic_store_n(&aot_profile_ram_pages, (uae_u32 **)NULL, __ATOMIC_RELEASE);
	const uae_u32 pages = RAMSize >> AOT_PROFILE_PAGE_SHIFT;
	if (ram_pages == NULL || ram_page_count != pages) {
		ram_pages = (uae_u32 **)profile_alloc(pages * sizeof(uae_u32 *));
		if (ram_pages == NULL)
			return false;
		ram_page_count = pages;
	}
	for (uae_u32 i = 0; i < pages; ++i) {
		if (ram_pages[i] != NULL)
			memset(ram_pages[i], 0, AOT_PROFILE_PAGE_SLOTS * sizeof(uae_u32));
	}
	aot_profile_ram_page_count = pages;
	if (profile_storage == NULL || aot_profile_slots != slots) {
		free(profile_storage);
#ifdef ARDUINO
		profile_storage = (uae_u32 *)heap_caps_malloc(slots * sizeof(uae_u32), MALLOC_CAP_SPIRAM);
#else
		profile_storage = (uae_u32 *)malloc(slots * sizeof(uae_u32));
#endif
		if (profile_storage == NULL)
			return false;
	}
	memset(profile_storage, 0, slots * sizeof(uae_u32));
	aot_profile_slots = slots;
	__atomic_store_n(&aot_profile_counts, profile_storage, __ATOMIC_RELEASE);
	__atomic_store_n(&aot_profile_ram_pages, ram_pages, __ATOMIC_RELEASE);
	return true;
}

void AotProfileStop(void)
{
	__atomic_store_n(&aot_profile_counts, (uae_u32 *)NULL, __ATOMIC_RELEASE);
	__atomic_store_n(&aot_profile_ram_pages, (uae_u32 **)NULL, __ATOMIC_RELEASE);
}

uae_u32 AotProfileChunks(void)
{
	if (profile_storage == NULL)
		return 0;
	return (aot_profile_slots + AOT_PROFILE_CHUNK - 1) / AOT_PROFILE_CHUNK;
}

static bool chunk_used(uae_u32 chunk)
{
	const uae_u32 first = chunk * AOT_PROFILE_CHUNK;
	for (uae_u32 i = first; i < first + AOT_PROFILE_CHUNK && i < aot_profile_slots; ++i) {
		if (profile_storage[i])
			return true;
	}
	return false;
}

uae_u32 AotProfileMap(uae_u32 first_chunk, uae_u8 *bitmap, uae_u32 bytes)
{
	const uae_u32 chunks = AotProfileChunks();
	uae_u32 used = 0;
	memset(bitmap, 0, bytes);
	for (uae_u32 i = 0; i < bytes * 8 && first_chunk + i < chunks; ++i) {
		if (chunk_used(first_chunk + i))
			bitmap[i / 8] |= (uae_u8)(1u << (i % 8));
		used = i / 8 + 1;
	}
	return used;
}

bool AotProfileRead(uae_u32 chunk, uae_u32 *counts)
{
	if (chunk >= AotProfileChunks())
		return false;
	const uae_u32 first = chunk * AOT_PROFILE_CHUNK;
	for (uae_u32 i = 0; i < AOT_PROFILE_CHUNK; ++i)
		counts[i] = first + i < aot_profile_slots ? profile_storage[first + i] : 0;
	return true;
}

#else

uae_u32 *AotProfileRamPage(uae_u32) { return NULL; }
uae_u32 AotProfileRamMap(uae_u32, uae_u8 *, uae_u32) { return 0; }
bool AotProfileRamChunks(uae_u32, uae_u32 *) { return false; }
bool AotProfileRamRead(uae_u32, uae_u32, uae_u32 *) { return false; }
bool AotProfileStart(void) { return false; }
void AotProfileStop(void) {}
uae_u32 AotProfileChunks(void) { return 0; }
uae_u32 AotProfileMap(uae_u32, uae_u8 *, uae_u32) { return 0; }
bool AotProfileRead(uae_u32, uae_u32 *) { return false; }

#endif
