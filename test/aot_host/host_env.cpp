// Host environment for native tests of the CPU core: guest memory and the
// few emulator services newcpu.cpp links against.
#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "emul_op.h"
#include "m68k.h"
#include "memory.h"
#include "readcpu.h"
#include "newcpu.h"
#include "host_env.h"

HostSerial Serial;

int CPUType = 4;
int FPUType = 1;
uint32 RAMBaseMac = 0;
uint8 *RAMBaseHost = NULL;
uint32 RAMSize = 0;
uint32 ROMBaseMac = 0;
uint8 *ROMBaseHost = NULL;
uint32 ROMSize = 0;
uint32 InterruptFlags = 0;
int32 emulated_ticks = 1 << 30;
bool boot_control_pending = false;

void cpu_do_check_control(void) {}
void FlushCodeCache(void *, uint32) {}
void cpu_do_check_ticks(void) {}
int intlev(void) { return -1; }

// Translated code holds none of these, so the interpreter only gets here
// when the two runs have already parted. Skip the instruction's first word;
// the comparison that follows reports the mismatch.
static void unexpected(const char *what)
{
    fprintf(stderr, "host test reached %s at pc %08x\n", what, (unsigned)m68k_getpc());
    m68k_incpc(2);
}

void EmulOp(uint16, M68kRegisters *) { unexpected("EmulOp"); }
void fpu_dump_flags(void) {}
void fpu_dump_registers(void) {}
void fpu_exit(void) {}
void fpu_init(bool) {}
void fpu_reset(void) {}
void fpuop_arithmetic(uae_u32, uae_u32) { unexpected("fpuop_arithmetic"); }
void fpuop_bcc(uae_u32, uaecptr, uae_u32) { unexpected("fpuop_bcc"); }
void fpuop_dbcc(uae_u32, uae_u32) { unexpected("fpuop_dbcc"); }
void fpuop_restore(uae_u32) { unexpected("fpuop_restore"); }
void fpuop_save(uae_u32) { unexpected("fpuop_save"); }
void fpuop_scc(uae_u32, uae_u32) { unexpected("fpuop_scc"); }
void fpuop_trapcc(uae_u32, uaecptr) { unexpected("fpuop_trapcc"); }

int MacFrameLayout = 0;
uint8 *MacFrameBaseHost = NULL;
uint32 MacFrameSize = 0;

// The CPU core reaches everything but RAM through these, as memory.cpp's do:
// ROM directly, the rest through the bank table that the test installs.
static addrbank *bank_table[65536];
addrbank **mem_banks = bank_table;

void HostSetRam(uint8_t *ram, uint32_t size)
{
    RAMBaseHost = ram;
    RAMSize = size;
}

void HostSetRom(uint8_t *rom, uint32_t base, uint32_t size)
{
    ROMBaseHost = rom;
    ROMBaseMac = base;
    ROMSize = size;
}

// The direct-layout frame buffer, as memory.cpp maps it. Damage tracking is
// the display's business and is not modelled here.
static uae_u8 *frame_at(uaecptr addr, uae_u32 extra)
{
    const uae_u32 offset = addr - BASILISK_FRAME_BASE_MAC;
    if (MacFrameLayout == FLAYOUT_DIRECT && offset < MacFrameSize && offset + extra < MacFrameSize)
        return MacFrameBaseHost + offset;
    return NULL;
}

extern "C" {

uae_u32 REGPARAM2 mem_slow_lget(uaecptr addr)
{
    if (addr - ROMBaseMac < ROMSize)
        return do_get_mem_long((uae_u32 *)(ROMBaseHost + (addr - ROMBaseMac)));
    if (uae_u8 *frame = frame_at(addr, 3))
        return do_get_mem_long((uae_u32 *)frame);
    return call_mem_get_func(get_mem_bank(addr).lget, addr);
}

uae_u32 REGPARAM2 mem_slow_wget(uaecptr addr)
{
    if (addr - ROMBaseMac < ROMSize)
        return do_get_mem_word((uae_u16 *)(ROMBaseHost + (addr - ROMBaseMac)));
    if (uae_u8 *frame = frame_at(addr, 1))
        return do_get_mem_word((uae_u16 *)frame);
    return call_mem_get_func(get_mem_bank(addr).wget, addr);
}

uae_u32 REGPARAM2 mem_slow_bget(uaecptr addr)
{
    if (addr - ROMBaseMac < ROMSize)
        return ROMBaseHost[addr - ROMBaseMac];
    if (uae_u8 *frame = frame_at(addr, 0))
        return *frame;
    return call_mem_get_func(get_mem_bank(addr).bget, addr);
}

void REGPARAM2 mem_slow_lput(uaecptr addr, uae_u32 l)
{
    if (uae_u8 *frame = frame_at(addr, 3))
        do_put_mem_long((uae_u32 *)frame, l);
    else
        call_mem_put_func(get_mem_bank(addr).lput, addr, l);
}

void REGPARAM2 mem_slow_wput(uaecptr addr, uae_u32 w)
{
    if (uae_u8 *frame = frame_at(addr, 1))
        do_put_mem_word((uae_u16 *)frame, w);
    else
        call_mem_put_func(get_mem_bank(addr).wput, addr, w);
}

void REGPARAM2 mem_slow_bput(uaecptr addr, uae_u32 b)
{
    if (uae_u8 *frame = frame_at(addr, 0))
        *frame = (uae_u8)b;
    else
        call_mem_put_func(get_mem_bank(addr).bput, addr, b);
}

}  // extern "C"
