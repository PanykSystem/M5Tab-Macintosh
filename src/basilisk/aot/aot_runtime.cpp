/*
 *  aot_runtime.cpp - Install ahead-of-time translated 68k code
 */

#include "aot_runtime.h"

#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#include <esp_cache.h>
#include <esp_heap_caps.h>
#endif

extern int CPUType;
extern void REGPARAM2 op_illg_1(uae_u32 opcode) REGPARAM;

int32 aot_extra_instructions SDATA_ATTR("aot_extra") = 0;
uae_u32 aot_region_calls SDATA_ATTR("aot_calls") = 0;
uae_u32 aot_flush_epoch SDATA_ATTR("aot_epoch") = 1;
uae_u64 aot_total_instructions = 0;

static AotStats aot_stats;
static bool aot_installed = false;
static bool aot_ram_enabled = false;
static uae_u32 aot_ram_opcode_base = 0;   // index of the first RAM entry's patch opcode

// Builds without a generated image link this empty one.
__attribute__((weak)) AotModuleState aot_module_state[1];
__attribute__((weak)) extern const AotImage aot_image = {
	0, 0, NULL, NULL, NULL, NULL, 0, 0, 0,
	NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 0,
	NULL, NULL,
};

extern void m68k_mull(uae_u32, uae_u32, uae_u16);

#ifdef ARDUINO
DRAM_ATTR
#endif
const AotFar aot_far = { AotRamEntrySlow, m68k_mull, Exception };

static intptr_t aot_code_delta = 0;   // from the linked address to where the code runs

#if defined(ARDUINO) && AOT_PSRAM
// The translated code, bracketed by the generated file.
extern "C" {
extern const uae_u8 aot_blob_begin[] __attribute__((weak));
extern const uae_u8 aot_blob_end[] __attribute__((weak));
}

// Copy the translated code to PSRAM. The code was checked at build time to
// reach everything outside itself through absolute addresses, so the copy
// runs unchanged.
static void aot_move_code(void)
{
	aot_code_delta = 0;
	if (&aot_blob_begin[0] == NULL || &aot_blob_end[0] == NULL || aot_blob_end <= aot_blob_begin)
		return;
	const size_t size = (size_t)(aot_blob_end - aot_blob_begin);
	aot_stats.code_bytes = (uae_u32)size;
	const size_t padded = (size + 127) & ~(size_t)127;
	uae_u8 *copy = (uae_u8 *)heap_caps_aligned_alloc(128, padded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (copy == NULL) {
		write_log("[AOT] no PSRAM for %u bytes of translated code; it runs from flash\n", (unsigned)size);
		return;
	}
	memcpy(copy, aot_blob_begin, size);
	memset(copy + size, 0, padded - size);
	if (esp_cache_msync(copy, padded, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA) != ESP_OK ||
	    esp_cache_msync(copy, padded, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_INST) != ESP_OK) {
		write_log("[AOT] cache synchronization failed; translated code runs from flash\n");
		heap_caps_free(copy);
		return;
	}
	aot_code_delta = copy - aot_blob_begin;
	aot_stats.code_in_psram = 1;
	write_log("[AOT] %u bytes of translated code run from PSRAM at %p\n", (unsigned)size, copy);
}

// Where a region function runs.
static cpuop_func *aot_handler(cpuop_func *linked)
{
	const uae_u8 *address = (const uae_u8 *)linked;
	if (aot_code_delta != 0 && address >= aot_blob_begin && address < aot_blob_end)
		return (cpuop_func *)(address + aot_code_delta);
	return linked;
}
#else
static void aot_move_code(void) {}
static cpuop_func *aot_handler(cpuop_func *linked) { return linked; }
#endif

static uae_u32 aot_crc32(uae_u32 crc, const uae_u8 *data, uae_u32 length)
{
	crc = ~crc;
	while (length--) {
		crc ^= *data++;
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
	}
	return ~crc;
}

// FNV-1a over 16-bit units. Guest code is word aligned and so is guest RAM
// in host memory, so these are aligned loads.
static inline uae_u32 aot_sum16(uae_u32 value, const uae_u8 *data, uae_u32 bytes)
{
	const uae_u16 *words = (const uae_u16 *)data;
	for (uae_u32 i = 0; i < bytes / 2; ++i) {
		value ^= words[i];
		value *= 0x01000193u;
	}
	return value;
}

// Opcode table index of a big-endian opcode (see cft_map() in newcpu.cpp).
static inline uae_u32 aot_table_index(uae_u32 opcode)
{
#ifdef HAVE_GET_WORD_UNSWAPPED
	return ((opcode >> 8) & 255) | ((opcode & 255) << 8);
#else
	return opcode;
#endif
}

static inline uae_u32 host_word(const uae_u8 *host)
{
	return ((uae_u32)host[0] << 8) | host[1];
}

void AotReadStats(AotStats *stats)
{
	*stats = aot_stats;
	uae_u32 installed = 0;
	for (uae_u32 m = 0; m < aot_image.module_count; ++m) {
		if (aot_image.module_state[m].base != AOT_NO_BASE)
			++installed;
	}
	stats->modules_installed = installed;
	// A 32-bit call counter wraps after hours; callers take differences.
	stats->calls = aot_region_calls;
	stats->instructions = aot_total_instructions + (uae_u64)aot_region_calls;
}

uae_u32 AotOriginalWord(uaecptr address)
{
	const AotImage &image = aot_image;
	const uae_u32 offset = address - image.rom_base;
	if (aot_installed && offset < image.rom_size) {
		for (uae_u32 e = 0; e < image.entry_count; ++e) {
			if (image.entries[e].offset == offset)
				return image.entries[e].original;
		}
	}
	return get_word(address);
}

void AotInstall(void)
{
	const AotImage &image = aot_image;
	aot_stats = AotStats();
	aot_stats.regions_total = image.region_count;
	aot_stats.entries_total = image.entry_count;
	aot_stats.modules_total = image.module_count;
	aot_installed = false;
	aot_ram_enabled = false;
	for (uae_u32 m = 0; m < image.module_count; ++m) {
		image.module_state[m].base = AOT_NO_BASE;
		image.module_state[m].epoch = 0;
	}
	if (!AOT_ROM || image.entry_count + image.ram_entry_count == 0) {
		write_log("[AOT] no translated image in this build\n");
		return;
	}
	if (CPUType != 4 || ROMBaseMac != image.rom_base || ROMSize != image.rom_size) {
		write_log("[AOT] disabled: CPU %d, ROM base %08x size %u do not match the image\n",
		          CPUType, (unsigned)ROMBaseMac, (unsigned)ROMSize);
		return;
	}
	uae_u8 *region_ok = (uae_u8 *)calloc(image.region_count + 1, 1);
	if (region_ok == NULL) {
		write_log("[AOT] disabled: out of memory\n");
		return;
	}

	aot_move_code();

	// Check every region against the ROM before changing any byte of it:
	// entry points of one region lie inside the ranges of others.
	for (uae_u32 r = 0; r < image.region_count; ++r) {
		const AotRegion &region = image.regions[r];
		uae_u32 crc = 0;
		bool in_range = true;
		for (uae_u32 i = 0; i < region.range_count; ++i) {
			const AotRange &range = image.ranges[region.first_range + i];
			if (range.offset > ROMSize || range.length > ROMSize - range.offset) {
				in_range = false;
				break;
			}
			crc = aot_crc32(crc, ROMBaseHost + range.offset, range.length);
		}
		if (in_range && crc == region.crc) {
			region_ok[r] = 1;
			aot_stats.regions_installed++;
		}
	}

	for (uae_u32 e = 0; e < image.entry_count; ++e) {
		const AotEntry &entry = image.entries[e];
		if (!region_ok[entry.owner])
			continue;
		const uae_u32 slot = aot_table_index(image.patch_opcodes[e]);
		if (cpufunctbl[slot] != op_illg_1)
			continue;  // the opcode is in use after all
		uae_u8 *host = ROMBaseHost + entry.offset;
		cpufunctbl[slot] = aot_handler(entry.handler);
		host[0] = (uae_u8)(image.patch_opcodes[e] >> 8);
		host[1] = (uae_u8)image.patch_opcodes[e];
		aot_stats.entries_installed++;
	}
	free(region_ok);
	aot_installed = true;

	// RAM entry handlers are always reachable; the opcodes are written into
	// guest RAM only where a module is installed.
	aot_ram_opcode_base = image.entry_count;
#if AOT_RAM
	aot_ram_enabled = image.module_count != 0;
	for (uae_u32 e = 0; aot_ram_enabled && e < image.ram_entry_count; ++e) {
		const uae_u32 slot = aot_table_index(image.patch_opcodes[aot_ram_opcode_base + e]);
		if (cpufunctbl[slot] != op_illg_1) {
			aot_ram_enabled = false;
			break;
		}
	}
	for (uae_u32 e = 0; aot_ram_enabled && e < image.ram_entry_count; ++e)
		cpufunctbl[aot_table_index(image.patch_opcodes[aot_ram_opcode_base + e])] =
			aot_handler(image.ram_entries[e].handler);
#endif
	write_log("[AOT] ROM: installed %u of %u regions, %u of %u entry points; RAM: %u modules %s\n",
	          (unsigned)aot_stats.regions_installed, (unsigned)aot_stats.regions_total,
	          (unsigned)aot_stats.entries_installed, (unsigned)aot_stats.entries_total,
	          (unsigned)image.module_count, aot_ram_enabled ? "armed" : "off");
}

void AotCodeChanged(void)
{
	aot_flush_epoch++;
	aot_stats.flushes++;
}

// Guest RAM that has held an installed module, as [low, high).
static uae_u32 aot_code_low = 0xffffffffu;
static uae_u32 aot_code_high = 0;

void AotCodeChangedRange(const uae_u8 *host, uae_u32 size)
{
	const uae_u32 start = (uae_u32)(host - RAMBaseHost);
	if (start >= RAMSize)
		return;
	if (start < aot_code_high && size > 0 && start + size > aot_code_low)
		AotCodeChanged();
}

// Does guest RAM at `base` hold the module's code? Entry opcodes may be
// either the original instruction or the module's entry opcode.
static bool module_matches(uae_u32 index, uae_u32 base)
{
	const AotImage &image = aot_image;
	const AotModule &module = image.modules[index];
	aot_stats.module_checks++;
	if ((base & 1) || base >= RAMSize || module.span > RAMSize - base)
		return false;
	const uae_u8 *host = RAMBaseHost + base;
	uae_u32 sum = 0x811C9DC5u;
	for (uae_u32 i = 0; i < module.range_count; ++i) {
		const AotRange &range = image.ram_ranges[module.first_range + i];
		sum = aot_sum16(sum, host + range.offset, range.length);
	}
	if (sum != module.checksum)
		return false;
	for (uae_u32 i = 0; i < module.entry_count; ++i) {
		const AotEntry &entry = image.ram_entries[module.first_entry + i];
		const uae_u32 word = host_word(host + entry.offset);
		if (word != entry.original &&
		    word != image.patch_opcodes[aot_ram_opcode_base + module.first_entry + i])
			return false;
	}
	return true;
}

static void module_install(uae_u32 index, uae_u32 base)
{
	const AotImage &image = aot_image;
	const AotModule &module = image.modules[index];
	uae_u8 *host = RAMBaseHost + base;
	for (uae_u32 i = 0; i < module.entry_count; ++i) {
		const AotEntry &entry = image.ram_entries[module.first_entry + i];
		const uae_u16 opcode = image.patch_opcodes[aot_ram_opcode_base + module.first_entry + i];
		host[entry.offset] = (uae_u8)(opcode >> 8);
		host[entry.offset + 1] = (uae_u8)opcode;
	}
	image.module_state[index].base = base;
	image.module_state[index].epoch = aot_flush_epoch;
	aot_stats.module_installs++;
	if (base < aot_code_low)
		aot_code_low = base;
	if (base + module.span > aot_code_high)
		aot_code_high = base + module.span;
}

bool AotRamInstallAt(uae_u32 index, uae_u32 base)
{
	if (!aot_ram_enabled || index >= aot_image.module_count || !module_matches(index, base))
		return false;
	module_install(index, base);
	return true;
}

void AotRamForgetAll(void)
{
	aot_code_low = 0xffffffffu;
	aot_code_high = 0;
	for (uae_u32 m = 0; m < aot_image.module_count; ++m) {
		aot_image.module_state[m].base = AOT_NO_BASE;
		aot_image.module_state[m].epoch = 0;
	}
}

uae_u32 AotRamEntrySlow(uae_u32 index, uae_u32 offset)
{
	const AotImage &image = aot_image;
	AotModuleState &state = image.module_state[index];
	const uae_u32 ram_offset = (uae_u32)(regs.pc_p - RAMBaseHost);
	if (!aot_ram_enabled || ram_offset >= RAMSize || ram_offset < offset ||
	    (regs.spcflags & (SPCFLAG_TRACE | SPCFLAG_DOTRACE)))
		return AOT_NO_BASE;
	const uae_u32 base = ram_offset - offset;
	if (module_matches(index, base)) {
		if (state.base != base) {
			// The code moved, or a copy of it runs here: make every entry
			// of this copy an entry point.
			module_install(index, base);
		}
		state.epoch = aot_flush_epoch;
		return base;
	}
	aot_stats.module_rejects++;
	if (state.base == base)
		state.base = AOT_NO_BASE;
	// The word being executed is this module's entry opcode, but the code
	// around it is no longer the module's. It is an instruction, because it
	// is being executed: put the original back.
	const AotModule &module = image.modules[index];
	for (uae_u32 i = 0; i < module.entry_count; ++i) {
		const AotEntry &entry = image.ram_entries[module.first_entry + i];
		if (entry.offset == offset) {
			regs.pc_p[0] = (uae_u8)(entry.original >> 8);
			regs.pc_p[1] = (uae_u8)entry.original;
			break;
		}
	}
	return AOT_NO_BASE;
}

void AotRamSample(void)
{
	const AotImage &image = aot_image;
	if (!aot_ram_enabled)
		return;
	const uae_u32 pc = (uae_u32)(regs.pc_p - RAMBaseHost);
	if (pc >= RAMSize || RAMSize - pc < 8 || (pc & 1))
		return;
	const uae_u32 hash = aot_sum16(0x811C9DC5u, regs.pc_p, 8);
	uae_u32 low = 0, high = image.anchor_count;
	while (low < high) {
		const uae_u32 middle = (low + high) / 2;
		if (image.anchors[middle].hash < hash)
			low = middle + 1;
		else
			high = middle;
	}
	for (; low < image.anchor_count && image.anchors[low].hash == hash; ++low) {
		const AotAnchor &anchor = image.anchors[low];
		if (pc < anchor.offset)
			continue;
		const uae_u32 base = pc - anchor.offset;
		AotModuleState &state = image.module_state[anchor.module];
		if (state.base == base)
			continue;
		if (module_matches(anchor.module, base))
			module_install(anchor.module, base);
	}
}
