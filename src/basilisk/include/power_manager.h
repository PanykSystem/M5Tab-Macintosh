/*
 *  power_manager.h - Mac Power Manager emulation (battery, sleep, backlight)
 *
 *  The emulated Quadra has no PMU. When the board has a battery, Basilisk
 *  answers the Power Manager traps itself so Mac OS sees a PowerBook-style
 *  Power Manager: Gestalt 'powr', _PowerMgrDispatch ($A09E), _PowerDispatch
 *  ($A09F), _PMgrOp ($A085) and _Sleep/_SleepQInstall/_SleepQRemove ($A08A).
 *  Independently, Gestalt 'M5pw' gives guest software the emulator's power
 *  services (backlight, battery, sleep). POWER_MANAGEMENT.md describes both.
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stddef.h>

struct M68kRegisters;

// Gestalt selector for the emulator power services ('M5pw'). The response
// is the address of a C-callable entry point:
//   long Services(long selector, long argument, void *buffer);
const uint32 kPowerServicesGestalt = 0x4d357077;	// 'M5pw'

enum {	// Services selectors
	kPowerServicesVersion = 0,		// -> interface version (1)
	kPowerServicesCapabilities = 1,	// -> kPowerCap* bits
	kPowerServicesGetBacklight = 2,	// -> backlight percent, 5-100
	kPowerServicesSetBacklight = 3,	// argument: percent -> level set
	kPowerServicesGetBattery = 4,	// buffer: PowerServicesBattery -> 0 or error
	kPowerServicesSleepDisplay = 5,	// turn the backlight off until the next touch or key
	kPowerServicesLog = 6,			// buffer: Pascal string (or NULL), argument: number -> serial log
};

enum {	// kPowerServicesCapabilities bits
	kPowerCapBacklight = 1 << 0,		// backlight level can be set
	kPowerCapBattery = 1 << 1,			// the board measures a battery
	kPowerCapPowerManager = 1 << 2,		// Gestalt 'powr' and the PowerBook traps are emulated
	kPowerCapSleep = 1 << 3,			// _Sleep turns the display off until touched
};

// kPowerServicesGetBattery record (big-endian in guest memory, 16 bytes)
//   +0  uint8  flags (bit 7 present, 6 charging, 5 charger connected, 0 simulated)
//   +1  uint8  percent, 0-100
//   +2  uint16 pack voltage, mV
//   +4  int16  current, mA (> 0 charging)
//   +6  uint16 reserved (0)
//   +8  uint32 seconds to empty (0 = unknown)
//   +12 uint32 seconds to full (0 = unknown)

// Called by PatchROM() for 32-bit ROMs. stub_offset is a free 0x40-byte
// area of the ROM image for the trap and service stubs.
extern void PowerManagerPatchROM(uint32 stub_offset);

// Called at EMUL_OP_INSTALL_DRIVERS on every boot: points the trap table at
// the stubs and registers Gestalt 'M5pw'. Resets all guest-visible state.
extern void PowerManagerInstall(void);

// Registers Gestalt('M5pw') once Gestalt works (boot 3). Mac OS 8.1 then
// rebuilds the table, so PowerManagerCheckLoad registers it again, if it is
// gone, whenever an INIT resource loads.
extern void PowerManagerBootHook(const char *where);

// Called from vCheckLoad for every resource the Mac loads: patches the
// System's Gestalt 'powr' value and re-registers 'M5pw' before extensions.
extern void PowerManagerCheckLoad(uint32 type, int16 id, uint8 *p, uint32 size);

// EMUL_OP handler for every M68K_EMUL_OP_POWER_* opcode.
extern void PowerManagerEmulOp(uint16 opcode, M68kRegisters *r);

// Host side, any task.
extern bool PowerManagerActive(void);		// Mac OS sees a Power Manager
extern bool PowerManagerSleeping(void);		// display asleep (guest sleep or idle)
extern void PowerManagerWake(void);			// end a sleep or idle blank
extern void PowerManagerNoteActivity(void);	// user input; restarts the idle timer
extern void PowerManagerIdlePoll(void);		// input task: idle display-off
extern int PowerManagerDescribe(char *buffer, size_t size);	// one-line state for serial

#endif
