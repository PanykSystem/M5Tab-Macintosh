/*
 *  aot_codegen.h - Definitions for generated region functions
 *
 *  Included only by aot_rom_generated.cpp, after AOT_ROM_BASE is defined.
 *  The instruction bodies in that file are the interpreter's handler bodies,
 *  so this header redirects what they touch:
 *
 *   - Condition codes live in locals for the length of a region call. The
 *     compiler can then discard flag results that the following 68k code
 *     overwrites, which the interpreter has to compute and store every time.
 *   - The emulated PC is not maintained inside a region. Leaving a region
 *     sets it; PC-relative operands were folded to constants by the
 *     translator. The generated file defines AOT_PC(address) and
 *     AOT_EXIT_REL(address) for the code that follows: plain constants for
 *     the ROM, relative to the module's base for RAM.
 *   - Guest registers are accessed exactly as the interpreter accesses them.
 *     Guest memory is too, through out-of-line helpers.
 */

#ifndef AOT_CODEGEN_H
#define AOT_CODEGEN_H

#ifndef AOT_GENERATED_CODE
#error "aot_codegen.h is for generated region code only"
#endif

#define SET_CFLG_ALWAYS(x) SET_CFLG(x)
#define SET_NFLG_ALWAYS(x) SET_NFLG(x)

static ALWAYS_INLINE void aot_setpc(uaecptr address) { m68k_setpc(address); }

#undef CFLG
#undef ZFLG
#undef NFLG
#undef VFLG
#undef XFLG
#define CFLG aot_c
#define ZFLG aot_z
#define NFLG aot_n
#define VFLG aot_v
#define XFLG aot_x

// Same expressions as cctrue() in m68k.h, on the local flags.
static ALWAYS_INLINE int aot_cctrue(const int cc, unsigned int c, unsigned int z,
                                    unsigned int n, unsigned int v)
{
	switch (cc) {
	 case 0: return 1;
	 case 1: return 0;
	 case 2: return !c && !z;
	 case 3: return c || z;
	 case 4: return !c;
	 case 5: return c;
	 case 6: return !z;
	 case 7: return z;
	 case 8: return !v;
	 case 9: return v;
	 case 10:return !n;
	 case 11:return n;
	 case 12:return n == v;
	 case 13:return n != v;
	 case 14:return !z && (n == v);
	 case 15:return z || (n != v);
	}
	return 0;
}
#define cctrue(cc) aot_cctrue((cc), aot_c, aot_z, aot_n, aot_v)

#define AOT_FLAGS_OUT() do { \
	regflags.c = aot_c; regflags.z = aot_z; regflags.n = aot_n; \
	regflags.v = aot_v; regflags.x = aot_x; \
} while (0)
#define AOT_FLAGS_IN() do { \
	aot_c = regflags.c; aot_z = regflags.z; aot_n = regflags.n; \
	aot_v = regflags.v; aot_x = regflags.x; \
} while (0)

// Guest memory is reached through the out-of-line helpers in memory.cpp.
// Translated code runs behind a 16 KB instruction cache, where the inline
// sequences the interpreter's handlers use cost more in fetch stalls than
// the call does. The helpers are called at their absolute address: the code
// may run from a copy in PSRAM (AOT_PSRAM), so it must not reach anything
// outside itself relative to the PC. tools/aot/check_blob.py enforces that.
#if defined(__riscv) && !defined(MEM_SLOW_DIRECT_CALLS) && !defined(AOT_INLINE_MEMORY)
#define AOT_MEM_GET(thunk, addr) ({ \
	register uae_u32 aot_a0 __asm__("a0") = (addr); \
	__asm__ volatile("lui t0, %%hi(" #thunk ")\n\tjalr t0, %%lo(" #thunk ")(t0)" \
	                 : "+r"(aot_a0) : : "t0", "t1", "t2", "memory", MEM_SLOW_FP_CLOBBERS); \
	aot_a0; })
#define AOT_MEM_PUT(thunk, addr, value) do { \
	register uae_u32 aot_a0 __asm__("a0") = (addr); \
	register uae_u32 aot_a1 __asm__("a1") = (value); \
	__asm__ volatile("lui t0, %%hi(" #thunk ")\n\tjalr t0, %%lo(" #thunk ")(t0)" \
	                 : "+r"(aot_a0), "+r"(aot_a1) : : "t0", "t1", "t2", "memory", MEM_SLOW_FP_CLOBBERS); \
} while (0)
static ALWAYS_INLINE uae_u32 aot_get_long(uaecptr a) { return AOT_MEM_GET(mem_lget_thunk, a); }
static ALWAYS_INLINE uae_u32 aot_get_word(uaecptr a) { return AOT_MEM_GET(mem_wget_thunk, a); }
static ALWAYS_INLINE uae_u32 aot_get_byte(uaecptr a) { return AOT_MEM_GET(mem_bget_thunk, a); }
static ALWAYS_INLINE void aot_put_long(uaecptr a, uae_u32 v) { AOT_MEM_PUT(mem_lput_thunk, a, v); }
static ALWAYS_INLINE void aot_put_word(uaecptr a, uae_u32 v) { AOT_MEM_PUT(mem_wput_thunk, a, v); }
static ALWAYS_INLINE void aot_put_byte(uaecptr a, uae_u32 v) { AOT_MEM_PUT(mem_bput_thunk, a, v); }
#define get_long(a) aot_get_long(a)
#define get_word(a) aot_get_word(a)
#define get_byte(a) aot_get_byte(a)
#define put_long(a, v) aot_put_long((a), (v))
#define put_word(a, v) aot_put_word((a), (v))
#define put_byte(a, v) aot_put_byte((a), (v))
#endif

// Guest registers. The generated file defines AOT_REGS for each region:
// the interpreter's registers, or a local copy in regions that run long
// enough per entry to repay loading and storing it (AOT_REGS_IN/_OUT).
#undef m68k_dreg
#undef m68k_areg
#define m68k_dreg(r, num) (AOT_REGS[(num)])
#define m68k_areg(r, num) (AOT_REGS[(num) + 8])

// get_disp_ea_020() of newcpu.h on the region's registers.
static ALWAYS_INLINE uae_u32 aot_disp_ea_020(const uae_u32 *r, uae_u32 base, uae_u32 dp)
{
	uae_s32 regd = r[(dp >> 12) & 15];
	if ((dp & 0x800) == 0)
		regd = (uae_s32)(uae_s16)regd;
	regd <<= (dp >> 9) & 3;
	return base + (uae_s32)((uae_s8)dp) + regd;
}

// get_disp_ea_020_full() with the displacements it would read through the
// PC passed in. The translator calls this form for full extension words.
static ALWAYS_INLINE uae_u32 aot_disp_ea_020(const uae_u32 *r, uae_u32 base, uae_u32 dp, uae_s32 bd, uae_s32 od)
{
	uae_s32 regd = r[(dp >> 12) & 15];
	if ((dp & 0x800) == 0)
		regd = (uae_s32)(uae_s16)regd;
	regd <<= (dp >> 9) & 3;
	if (dp & 0x80) base = 0;
	if (dp & 0x40) regd = 0;
	base += bd;
	if ((dp & 0x4) == 0) base += regd;
	if (dp & 0x3) base = get_long(base);
	if (dp & 0x4) base += regd;
	return base + od;
}
#define get_disp_ea_020(...) aot_disp_ea_020(AOT_REGS, __VA_ARGS__)

// The entry number from an entry opcode in the form handlers are called
// with. A region's entry opcodes are consecutive and share their high byte;
// `first_opcode` is the first of them as the guest sees it.
#ifdef HAVE_GET_WORD_UNSWAPPED
#define AOT_ENTRY_NUMBER(opcode, first_opcode) (((opcode) >> 8) - ((first_opcode) & 0xff))
#else
#define AOT_ENTRY_NUMBER(opcode, first_opcode) (((opcode) & 0xff) - ((first_opcode) & 0xff))
#endif

// One per entry point of a region, in entry-opcode order.
struct AotEntryInfo {
	uae_u32 offset;          // of the entry instruction: in the ROM, or in its module
	uae_u32 original;        // its opcode, in the form handlers are called with
};

#define AOT_LOCALS \
	unsigned int aot_c, aot_z, aot_n, aot_v, aot_x; \
	uae_u32 aot_icount = 0; \
	uae_u32 aot_target

#define AOT_RUN_ORIGINAL(info) do { \
	(*cpufunctbl[(info).original])((info).original); \
	return; \
} while (0)

// A region function is the handler of all its entry opcodes; the opcode
// tells which entry. An entry opcode means "this region" only at the address
// it was installed at: a guest that copies code copies the opcode with it.
// Trace mode single-steps. Both cases run the original instruction instead.
#define AOT_ROM_ENTER(first_opcode, table) \
	AOT_LOCALS; \
	const uae_u32 aot_entry = AOT_ENTRY_NUMBER(opcode, first_opcode); \
	do { \
		const AotEntryInfo &aot_info = (table)[aot_entry]; \
		if (unlikely(regs.pc_p != ROMBaseHost + aot_info.offset || \
		             (regs.spcflags & (SPCFLAG_TRACE | SPCFLAG_DOTRACE)))) \
			AOT_RUN_ORIGINAL(aot_info); \
	} while (0); \
	AOT_FLAGS_IN()

// A RAM entry opcode means "this module" only where the module's code is.
// The fast path is two comparisons: the module is installed at the address
// being executed, and the guest has not flushed its instruction cache since
// the module's bytes were last checked.
#define AOT_RAM_ENTER(module, first_opcode, table) \
	AOT_LOCALS; \
	const uae_u32 aot_entry = AOT_ENTRY_NUMBER(opcode, first_opcode); \
	AotModuleState *const aot_state = &aot_module_state[module]; \
	uae_u32 aot_found = aot_state->base; \
	do { \
		const AotEntryInfo &aot_info = (table)[aot_entry]; \
		if (unlikely(regs.pc_p != RAMBaseHost + aot_found + aot_info.offset || \
		             aot_state->epoch != aot_flush_epoch || \
		             (regs.spcflags & (SPCFLAG_TRACE | SPCFLAG_DOTRACE)))) { \
			aot_found = aot_far.ram_entry_slow((module), aot_info.offset); \
			if (aot_found == AOT_NO_BASE) \
				AOT_RUN_ORIGINAL(aot_info); \
		} \
	} while (0); \
	const uae_u32 aot_base = aot_found; \
	(void)aot_base; \
	AOT_FLAGS_IN()

// Every way out of a region ends here. The exits set aot_target and jump.
#define AOT_EPILOGUE \
	aot_exit_rom: { \
		uae_u8 *aot_host = ROMBaseHost + aot_target; \
		regs.pc_p = aot_host; \
		regs.pc_oldp = aot_host; \
		regs.pc = AOT_ROM_BASE + aot_target; \
		goto aot_exit; \
	} \
	aot_exit_ram: { \
		uae_u8 *aot_host = RAMBaseHost + aot_target; \
		regs.pc_p = aot_host; \
		regs.pc_oldp = aot_host; \
		regs.pc = aot_target; \
		goto aot_exit; \
	} \
	aot_exit_dyn: \
		aot_setpc(aot_target); \
	aot_exit: \
		AOT_REGS_OUT(); \
		AOT_FLAGS_OUT(); \
		aot_extra_instructions += (int32)aot_icount - 1; \
		aot_region_calls++

#define AOT_EXIT_ROM(off) do { aot_target = (off); goto aot_exit_rom; } while (0)
// A RAM address the translated code itself lies at.
#define AOT_EXIT_RAM(address) do { aot_target = (address); goto aot_exit_ram; } while (0)
#define AOT_EXIT_DYN(address) do { aot_target = (address); goto aot_exit_dyn; } while (0)

// A region must hand control back for pending interrupts and must not run
// unbounded, so every backward branch polls. The special flags are written
// by other tasks; the volatile read keeps the poll inside guest loops that
// make no store.
#define AOT_POLL() \
	((*(volatile spcflags_t *)&regs.spcflags) | (aot_icount >> 16))
#define AOT_BACK(label, address) do { \
	if (unlikely(AOT_POLL())) AOT_EXIT_REL(address); \
	goto label; \
} while (0)

#define AOT_PUSH_RETURN(address) do { \
	m68k_areg(regs, 7) -= 4; \
	put_long(m68k_areg(regs, 7), (address)); \
} while (0)

#define AOT_RTS() do { \
	uaecptr aot_return = get_long(m68k_areg(regs, 7)); \
	m68k_areg(regs, 7) += 4; \
	AOT_EXIT_DYN(aot_return); \
} while (0)

// Handler bodies for JMP, JSR and RTD end in these.
#undef m68k_setpc_rte
#define m68k_setpc(address) AOT_EXIT_DYN(address)
#define m68k_setpc_rte(address) AOT_EXIT_DYN(address)
#define m68k_do_jsr(oldpc, dest) do { \
	const uaecptr aot_dest = (dest); \
	AOT_PUSH_RETURN(oldpc); \
	AOT_EXIT_DYN(aot_dest); \
} while (0)

// m68k_mull() is ordinary interpreter code working on the global flags.
#define AOT_MULL(opcode, dst, extra) do { \
	const uae_u32 aot_operand = (dst); \
	AOT_REGS_OUT(); \
	AOT_FLAGS_OUT(); \
	aot_far.mull((opcode), aot_operand, (extra)); \
	AOT_FLAGS_IN(); \
	AOT_REGS_IN(); \
} while (0)

// Exception() builds its stack frame from the global flags and the emulated
// PC, and leaves the PC at the exception handler. `remaining` is the number
// of instructions of the current block that follow the faulting one; they
// were counted on entry to the block and do not run.
#define AOT_EXCEPTION(nr, oldpc, pc, remaining) do { \
	AOT_REGS_OUT(); \
	AOT_FLAGS_OUT(); \
	aot_setpc(pc); \
	aot_far.exception((nr), (oldpc)); \
	aot_extra_instructions += (int32)aot_icount - 1 - (remaining); \
	aot_region_calls++; \
	return; \
} while (0)

// All region functions lie together, between aot_blob_begin and
// aot_blob_end, in the order the generated file defines them.
#if defined(__riscv)
#ifndef AOT_REGION_ATTR
#define AOT_REGION_ATTR __attribute__((section(".text.aot_regions"), noinline))
#endif
#define AOT_BLOB_MARK(name) \
	__asm__(".pushsection .text.aot_regions,\"ax\",@progbits\n" \
	        ".balign 4\n.globl " #name "\n" #name ":\n.popsection\n")
#else
#ifndef AOT_REGION_ATTR
#define AOT_REGION_ATTR
#endif
#define AOT_BLOB_MARK(name)
#endif

#endif /* AOT_CODEGEN_H */
