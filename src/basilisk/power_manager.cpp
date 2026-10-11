/*
 *  power_manager.cpp - Mac Power Manager emulation (battery, sleep, backlight)
 *
 *  Basilisk II emulates a Quadra, which has no PMU (Power Manager
 *  microcontroller). Its ROM holds PowerBook code for _Sleep, _PMgrOp and
 *  _PowerDispatch, but StartInit leaves those traps unimplemented on a
 *  machine without a PMU, $A09E (_PowerMgrDispatch) is not in the ROM, and
 *  PowerMgrVars ($D18) is $FFFFFFFF. The ROM's Gestalt('powr') function and
 *  Mac OS 8.1's own Gestalt both answer 0, so nothing calls them.
 *
 *  On a board with a battery this file makes the emulated Mac look like it
 *  has a Power Manager, entirely in C++ (no PMU or VIA emulation):
 *
 *   - Gestalt('powr') reports $1F, including gestaltPMgrDispatchExists:
 *     the ROM's function through M68K_EMUL_OP_POWER_GESTALT, and Mac OS
 *     8.1's own Gestalt (System 'ptch' 5) by patching it as it loads.
 *   - The OS trap table entries for $A09E, $A09F, $A085 and $A08A point at
 *     two-word ROM stubs (EMUL_OP, RTS) handled here.
 *
 *  That is enough for Apple's Control Strip "Battery Monitor" and the other
 *  PowerBook modules (Sleep Now, Power Settings, HD Spin Down) to load. The
 *  exact calls they make were found by disassembling them; see
 *  POWER_MANAGEMENT.md.
 *
 *  Separately, Gestalt('M5pw') returns a C-callable entry point for guest
 *  software that wants the emulator's own services: backlight level,
 *  detailed battery readings and display sleep. The Control Strip
 *  "Brightness" module in guest/ uses it. These services exist on every
 *  board; the PowerBook traps only when the board has a battery.
 */

#include <string.h>
#include <stdio.h>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "emul_op.h"
#include "power_manager.h"
#include "board_power.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define DEBUG 0
#include "debug.h"

namespace {

// Gestalt('powr') when the Power Manager is emulated: exists, CPU idle,
// SCC and sound power control (bits 0-3, what Mac OS 8.1 itself reports
// whenever _PMgrOp exists, see PowerManagerCheckLoad) plus
// gestaltPMgrDispatchExists (bit 4), which the PowerBook Control Strip
// modules need to use _PowerMgrDispatch instead of PowerMgrVars.
const uint32 kGestaltPowr = 0x1f;

// Mac OS error codes
const int16 kNoErr = 0;
const int16 kParamErr = -50;
const int16 kQErr = -1;
const int16 kSlpTypeErr = -5;
const int16 kPmReplyTOErr = -13001;

// Stub layout in the ROM image (PowerManagerPatchROM)
enum {
	kStubPowerMgrDispatch = 0x00,	// $A09E
	kStubPowerDispatch = 0x04,		// $A09F
	kStubPMgrOp = 0x08,				// $A085
	kStubSleep = 0x0c,				// $A08A
	kStubServices = 0x10,			// Gestalt('M5pw') response
	kStubServicesGestalt = 0x20,	// Gestalt function for 'M5pw'
	kStubSize = 0x40,
};

// SleepQRec offsets (the procedure is at 6 and flags at 10; neither is used)
const uint32 kSleepQLink = 0, kSleepQType = 4;
const uint16 kSleepQType16 = 16;
enum { kSleepRequest = 1, kSleepDemand = 2, kSleepNow = 6 };	// _Sleep messages

// PMFeatures bits
const uint32 kCanGetBatteryTime = 1 << 7;
const uint32 kHasDimmingSupport = 1 << 9;

// BatteryInfo flags
const uint8 kBatteryInstalled = 0x80, kBatteryCharging = 0x40, kChargerConnected = 0x20;
const int kWarningPercent = 10;

// Timeouts from Power Settings are in 15-second units.
const uint32 kTimeoutUnitMs = 15000;

// Reported processor speed in MHz (a 68040 Quadra).
const int kProcessorMHz = 33;

bool pm_enabled = false;		// Gestalt 'powr' and the PowerBook traps
uint32 stub_mac = 0;			// Mac address of the stubs, 0 = not installed
bool services_installed = false;

// Guest-visible Power Manager settings, reset on every boot.
struct GuestSettings {
	uint8 sleep_timeout;		// 15 s units, 0 = never
	uint8 hd_timeout;
	uint8 dimming_timeout;
	bool spindown_disabled;
	bool dimming_disabled;
	int auto_sleep_off;			// AutoSleepControl(false) nesting count
	bool full_speed;
	bool cycling;
	bool dim_suspend;
	int16 disk_mode_address;
};
GuestSettings settings;
uint32 sleep_queue = 0;			// first SleepQRec, guest address

// Display power, shared with the input task.
enum DisplayIdle { kAwake, kDimmed, kBlanked };
volatile bool guest_sleeping = false;	// inside _Sleep, CPU thread parked
volatile bool wake_requested = false;
volatile uint32 last_activity_ms = 0;
volatile int idle_state = kAwake;

// Counters for POWER STATUS
uint32 unknown_selector_count = 0;
uint32 sleep_count = 0;
enum { kCountDispatch, kCountPrivate, kCountPMgrOp, kCountIdleUpdate, kCountIdleState,
       kCountSerialPower, kCountSleep, kCountServices, kCountGestalt, kCountKinds };
uint32 call_counts[kCountKinds];

void reset_guest_state(void)
{
	settings = GuestSettings();
	settings.full_speed = true;
	settings.disk_mode_address = 2;
	sleep_queue = 0;
	last_activity_ms = millis();
}

inline uint32 error_result(int16 err) { return (uint32)(int32)err; }

// What Gestalt answers for 'powr' and 'M5pw' once Mac OS is running, for
// the boot log (CPU thread only).
void log_gestalt(const char *where)
{
	M68kRegisters r;
	memset(&r, 0, sizeof(r));
	r.d[0] = 0x706f7772;	// 'powr'
	Execute68kTrap(0xa1ad, &r);
	Serial.printf("[POWER] Gestalt('powr') at %s: %08lx\n", where, (unsigned long)r.a[0]);
	memset(&r, 0, sizeof(r));
	r.d[0] = kPowerServicesGestalt;
	Execute68kTrap(0xa1ad, &r);
	Serial.printf("[POWER] Gestalt('M5pw') at %s: err %d, %08lx\n", where,
	              (int16)(r.d[0] & 0xffff), (unsigned long)r.a[0]);
}

// Logs the first calls the emulation does not implement, for diagnosis.
void note_unknown(const char *trap, uint32 selector)
{
	if (unknown_selector_count++ < 32)
		Serial.printf("[POWER] Unhandled %s %04lx\n", trap, (unsigned long)selector);
}

uint32 scaled_battery_info(int which)
{
	BoardBatteryStatus st;
	if (!BoardPower_GetBattery(&st) || which < 0 || which > 1)
		return 0;
	uint8 flags = 0;
	if (st.battery_present) flags |= kBatteryInstalled;
	if (st.charging) flags |= kBatteryCharging;
	if (st.charger_connected) flags |= kChargerConnected;
	const uint8 level = st.battery_present ? (uint8)((st.percent * 255 + 50) / 100) : 0;
	const uint8 warning = (uint8)((kWarningPercent * 255 + 50) / 100);
	return ((uint32)flags << 24) | ((uint32)warning << 16) | level;
}

void battery_times(int which, uint32 rec)
{
	BoardBatteryStatus st = {};
	const bool ok = BoardPower_GetBattery(&st) && which >= 0 && which <= 1 && st.battery_present;
	const uint32 expected = ok && st.seconds_to_empty ? st.seconds_to_empty : 0xffffffff;
	WriteMacInt32(rec + 0, expected);
	WriteMacInt32(rec + 4, expected == 0xffffffff ? expected : expected / 20 * 17);
	WriteMacInt32(rec + 8, expected == 0xffffffff ? expected : expected / 20 * 23);
	WriteMacInt32(rec + 12, ok ? st.seconds_to_full : 0);
}

uint32 power_mgr_dispatch(M68kRegisters *r)
{
	const uint16 selector = r->d[0] & 0xffff;
	const uint16 param = r->d[0] >> 16;
	const uint32 a0 = r->a[0];

	static bool logged = false;
	if (!logged) {
		logged = true;
		log_gestalt("first _PowerMgrDispatch");
	}

	switch (selector) {
	case 0x00: return 0x2c;										// PMSelectorCount
	case 0x01: return kCanGetBatteryTime | kHasDimmingSupport;	// PMFeatures
	case 0x02: return settings.sleep_timeout;					// GetSleepTimeout
	case 0x03: settings.sleep_timeout = param & 0xff; return 0;	// SetSleepTimeout
	case 0x04: return settings.hd_timeout;						// GetHardDiskTimeout
	case 0x05: settings.hd_timeout = param & 0xff; return 0;	// SetHardDiskTimeout
	case 0x06: return 1;										// HardDiskPowered
	case 0x07: return 0;										// SpinDownHardDisk: SD cards do not spin
	case 0x08: return settings.spindown_disabled;				// IsSpindownDisabled
	case 0x09: settings.spindown_disabled = (param & 0xff) != 0; return 0;	// SetSpindownDisable
	case 0x0a: case 0x0b: return kNoErr;						// HardDiskQInstall/Remove: never called back
	case 0x0c: return scaled_battery_info((int16)param);		// GetScaledBatteryInfo (glue stores D0)
	case 0x0d:													// AutoSleepControl
		if (param & 0xff) {
			if (settings.auto_sleep_off > 0) settings.auto_sleep_off--;
		} else {
			settings.auto_sleep_off++;
		}
		return 0;
	case 0x0e: return 0;										// GetIntModemInfo: no internal modem
	case 0x0f: return 0;										// SetIntModemState
	case 0x10: case 0x11: return kProcessorMHz;				// Maximum/CurrentProcessorSpeed
	case 0x12: return settings.full_speed;						// FullProcessorSpeed
	case 0x13: settings.full_speed = (param & 0xff) != 0; return 0;	// SetProcessorSpeed: speed is fixed
	case 0x14: return (uint16)settings.disk_mode_address;		// GetSCSIDiskModeAddress
	case 0x15: settings.disk_mode_address = (int16)param; return 0;
	case 0x16:													// GetWakeupTimer: none
		WriteMacInt32(a0, 0);
		WriteMacInt16(a0 + 4, 0);
		return 0;
	case 0x17: return 0;										// SetWakeupTimer: ignored
	case 0x18: return settings.cycling;							// IsProcessorCyclingEnabled
	case 0x19: settings.cycling = (param & 0xff) != 0; return 0;	// EnableProcessorCycling
	case 0x1a: return 1;										// BatteryCount
	case 0x1b: {												// GetBatteryVoltage -> Fixed volts
		BoardBatteryStatus st;
		if (!BoardPower_GetBattery(&st) || !st.battery_present) return 0;
		return (uint32)(((uint64)st.pack_mv << 16) / 1000);
	}
	case 0x1c: battery_times((int16)param, a0); return 0;		// GetBatteryTimes
	case 0x1d: return settings.dimming_timeout;					// GetDimmingTimeout
	case 0x1e: settings.dimming_timeout = param & 0xff; return 0;	// SetDimmingTimeout
	case 0x1f: settings.dimming_disabled = (param & 0xff) == 0; return 0;	// DimmingControl
	case 0x20: return settings.dimming_disabled;				// IsDimmingControlDisabled
	case 0x21: return settings.auto_sleep_off > 0;				// IsAutoSlpControlDisabled
	case 0x22: case 0x23: return kNoErr;						// PMgrStateQInstall/Remove: no notifications
	case 0x24: case 0x25:										// UpdateSystemActivity, DelaySystemIdle
		PowerManagerNoteActivity();
		return kNoErr;
	case 0x26:													// GetStartupTimer: none
		WriteMacInt32(a0, 0);
		WriteMacInt16(a0 + 4, 0);
		return kNoErr;
	case 0x27: return kNoErr;									// SetStartupTimer: ignored
	case 0x28: {												// GetLastActivity
		const uint32 idle_ticks = (millis() - last_activity_ms) * 60 / 1000;
		WriteMacInt32(a0 + 2, ReadMacInt32(0x16a) - idle_ticks);	// Ticks
		return kNoErr;
	}
	case 0x29: WriteMacInt8(a0, 0); return kNoErr;				// GetSoundMixerState
	case 0x2a: return kNoErr;									// SetSoundMixerState
	case 0x2b: return settings.dim_suspend;						// GetDimSuspendState
	case 0x2c: settings.dim_suspend = (param & 0xff) != 0; return 0;
	default:
		note_unknown("_PowerMgrDispatch", selector);
		return error_result(kParamErr);
	}
}

// $A09F is Apple-private. The Battery Monitor uses selector 9 for battery
// state when gestaltPMgrDispatchExists is clear (the same 4-byte record as
// GetScaledBatteryInfo), and asks for selector $10 (consumption data),
// treating paramErr as "not available". The ROM answers paramErr for
// selectors it does not know, and so does this.
uint32 power_dispatch(M68kRegisters *r)
{
	const uint16 selector = r->d[0] & 0xffff;
	if (selector == 0x09)		// battery info, the same record as GetScaledBatteryInfo(0)
		return scaled_battery_info(0);
	note_unknown("_PowerDispatch", selector);
	return error_result(kParamErr);
}

// Trap $85 is four calls, told apart by the trap word in D1:
//   $A085 _PMgrOp: a command to the PMU. There is none; the Battery
//         Monitor's battery-type query ($6C) is answered so it offers
//         "time remaining".
//   $A285 _IdleUpdate: the system is busy; returns Ticks.
//   $A485 _IdleState: D0 -1 GetCPUSpeed (MHz), 0 EnableIdle, 1 DisableIdle.
//   $A685 _SerialPower: power the serial ports; nothing to do.
uint32 pmgr_op(M68kRegisters *r)
{
	switch (r->d[1] & 0x0600) {
	case 0x0200:
		call_counts[kCountIdleUpdate]++;
		return ReadMacInt32(0x16a);				// Ticks
	case 0x0400:
		call_counts[kCountIdleState]++;
		return (int32)r->d[0] == -1 ? kProcessorMHz : 0;
	case 0x0600:
		call_counts[kCountSerialPower]++;
		return kNoErr;
	}
	call_counts[kCountPMgrOp]++;
	const uint32 pb = r->a[0];
	if (pb == 0) return error_result(kParamErr);
	const uint16 command = ReadMacInt16(pb);
	if (command == 0x6c) {
		const uint32 reply = ReadMacInt32(pb + 8);
		if (reply) {
			static const uint8 battery_type[6] = {0x00, 0x03, 0x00, 0x00, 0x00, 0x00};
			for (int i = 0; i < 6; ++i) WriteMacInt8(reply + i, battery_type[i]);
			WriteMacInt16(pb + 2, 6);
			return kNoErr;
		}
	}
	note_unknown("_PMgrOp command", command);
	return error_result(kPmReplyTOErr);
}

void display_sleep_until_wake(void)
{
	sleep_count++;
	Serial.println("[POWER] Sleep: display off until touched");
	wake_requested = false;
	guest_sleeping = true;
	BoardPower_SetDisplayState(BOARD_DISPLAY_ASLEEP);
	while (!wake_requested)
		vTaskDelay(pdMS_TO_TICKS(50));
	BoardPower_SetDisplayState(BOARD_DISPLAY_AWAKE);
	idle_state = kAwake;
	last_activity_ms = millis();
	guest_sleeping = false;
	Serial.println("[POWER] Wake");
}

uint32 sleep_trap(M68kRegisters *r)
{
	const uint16 trap = r->d[1] & 0xffff;
	const uint32 element = r->a[0];

	if (trap & 0x0200) {						// _SleepQInstall
		if (ReadMacInt16(element + kSleepQType) != kSleepQType16) return error_result(kSlpTypeErr);
		WriteMacInt32(element + kSleepQLink, 0);
		if (sleep_queue == 0) {
			sleep_queue = element;
		} else {
			uint32 tail = sleep_queue;
			for (int guard = 0; guard < 256; ++guard) {
				if (tail == element) return kNoErr;	// already queued
				const uint32 next = ReadMacInt32(tail + kSleepQLink);
				if (next == 0) break;
				tail = next;
			}
			WriteMacInt32(tail + kSleepQLink, element);
		}
		return kNoErr;
	}
	if (trap & 0x0400) {						// _SleepQRemove
		if (ReadMacInt16(element + kSleepQType) != kSleepQType16) return error_result(kSlpTypeErr);
		uint32 prev = 0, cur = sleep_queue;
		for (int guard = 0; cur != 0 && guard < 256; ++guard) {
			const uint32 next = ReadMacInt32(cur + kSleepQLink);
			if (cur == element) {
				if (prev) WriteMacInt32(prev + kSleepQLink, next);
				else sleep_queue = next;
				return kNoErr;
			}
			prev = cur;
			cur = next;
		}
		return error_result(kQErr);
	}

	// _Sleep. A real Power Manager now runs the sleep queue protocol
	// (sleepRequest, sleepRevoke on a refusal, sleepDemand, sleep,
	// sleepWakeUp). The emulator does not call the procedures: some of them
	// expect PowerBook hardware. Mac OS 8.1's Display Manager procedure
	// (_DisplayDispatch $08E7) jumped to unmapped memory on a Tab5 on
	// sleepDemand. Nothing actually loses power here: the backlight goes
	// off and the 68k CPU waits in this trap, so to Mac software a sleep
	// looks like a pause. The queue is still kept, so SleepQInstall and
	// SleepQRemove behave as documented.
	const int32 message = (int32)r->d[0];
	if (message != kSleepRequest && message != kSleepDemand && message != kSleepNow)
		return kNoErr;
	display_sleep_until_wake();
	return kNoErr;
}

uint32 services_call(M68kRegisters *r)
{
	// long Services(long selector, long argument, void *buffer), C calling convention
	const uint32 sp = r->a[7];
	const int32 selector = (int32)ReadMacInt32(sp + 4);
	const int32 argument = (int32)ReadMacInt32(sp + 8);
	const uint32 buffer = ReadMacInt32(sp + 12);

	switch (selector) {
	case kPowerServicesVersion:
		return 1;
	case kPowerServicesCapabilities: {
		uint32 caps = kPowerCapSleep;
		if (BoardPower_HasBacklight()) caps |= kPowerCapBacklight;
		if (BoardPower_HasBattery()) caps |= kPowerCapBattery;
		if (pm_enabled) caps |= kPowerCapPowerManager;
		return caps;
	}
	case kPowerServicesGetBacklight:
		return BoardPower_HasBacklight() ? BoardPower_GetBacklight() : 100;
	case kPowerServicesSetBacklight:
		if (!BoardPower_HasBacklight()) return error_result(kParamErr);
		BoardPower_SetBacklight(argument);
		Serial.printf("[POWER] Backlight %d%% (set by the Mac)\n", BoardPower_GetBacklight());
		return BoardPower_GetBacklight();
	case kPowerServicesGetBattery: {
		BoardBatteryStatus st;
		if (buffer == 0) return error_result(kParamErr);
		if (!BoardPower_GetBattery(&st)) return error_result(kParamErr);
		uint8 flags = 0;
		if (st.battery_present) flags |= 0x80;
		if (st.charging) flags |= 0x40;
		if (st.charger_connected) flags |= 0x20;
		if (st.simulated) flags |= 0x01;
		WriteMacInt8(buffer + 0, flags);
		WriteMacInt8(buffer + 1, st.percent);
		WriteMacInt16(buffer + 2, st.pack_mv);
		WriteMacInt16(buffer + 4, (uint16)st.current_ma);
		WriteMacInt16(buffer + 6, 0);
		WriteMacInt32(buffer + 8, st.seconds_to_empty);
		WriteMacInt32(buffer + 12, st.seconds_to_full);
		return kNoErr;
	}
	case kPowerServicesSleepDisplay:		// the Mac keeps running; a touch or key wakes the display
		idle_state = kBlanked;
		BoardPower_SetDisplayState(BOARD_DISPLAY_ASLEEP);
		return kNoErr;
	case kPowerServicesLog: {			// a debugging aid for guest software
		char text[256] = "";
		if (buffer) {
			const uint8 length = ReadMacInt8(buffer);
			for (int i = 0; i < length; ++i) {
				const uint8 c = ReadMacInt8(buffer + 1 + i);
				text[i] = c >= 0x20 && c < 0x7f ? (char)c : '?';
			}
			text[length] = 0;
		}
		Serial.printf("[POWER] Mac: %s %ld\n", text, (long)argument);
		return kNoErr;
	}
	default:
		return error_result(kParamErr);
	}
}

// Finds the ROM Gestalt table entry for 'powr' and returns the ROM offset of
// its function, or 0. Universal ROMs keep a table of (selector, offset)
// pairs whose offsets are relative to the start of the table.
uint32 find_rom_powr_function(void)
{
	const uint8 *rom = ROMBaseHost;
	const uint32 size = ROMSize;
	auto be16 = [&](uint32 at) { return (uint32)(rom[at] << 8 | rom[at + 1]); };
	auto be32 = [&](uint32 at) { return be16(at) << 16 | be16(at + 2); };
	auto entry_ok = [&](uint32 at) {
		if (at + 8 > size) return false;
		for (int i = 0; i < 4; ++i)
			if (rom[at + i] < 0x20 || rom[at + i] > 0x7e) return false;
		const int32 ofs = (int32)be32(at + 4);
		return ofs < 0 && ofs > -0x10000;
	};
	for (uint32 pos = 8; pos + 16 <= size; pos += 2) {
		if (memcmp(rom + pos, "powr", 4) != 0) continue;
		if (!entry_ok(pos) || !entry_ok(pos + 8)) continue;
		uint32 start = pos;
		while (start >= 8 && entry_ok(start - 8)) start -= 8;
		const int64 func = (int64)start + (int32)be32(pos + 4);
		if (func <= 0 || func + 4 > size) continue;
		// Expect "moveq #0,d0; bra.w store_result" (no Power Manager).
		if (be16((uint32)func) == 0x7000 && be16((uint32)func + 2) == 0x6000)
			return (uint32)func;
	}
	return 0;
}

} // namespace


void PowerManagerPatchROM(uint32 stub_offset)
{
	pm_enabled = false;
	stub_mac = 0;

	// Trap and service stubs: each is EMUL_OP followed by RTS.
	uint16 *wp = (uint16 *)(ROMBaseHost + stub_offset);
	const uint16 stubs[] = {
		M68K_EMUL_OP_POWER_DISPATCH, M68K_RTS,
		M68K_EMUL_OP_POWER_PRIVATE, M68K_RTS,
		M68K_EMUL_OP_PMGR_OP, M68K_RTS,
		M68K_EMUL_OP_SLEEP, M68K_RTS,
		M68K_EMUL_OP_POWER_SERVICES, M68K_RTS,
	};
	memset(wp, 0, kStubSize);
	for (size_t i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
		wp[i] = htons(stubs[i]);

	// Gestalt function for 'M5pw': *response = services entry, return noErr.
	const uint32 services = ROMBaseMac + stub_offset + kStubServices;
	wp = (uint16 *)(ROMBaseHost + stub_offset + kStubServicesGestalt);
	*wp++ = htons(0x206f); *wp++ = htons(0x0004);			// movea.l 4(sp),a0
	*wp++ = htons(0x20bc);									// move.l #services,(a0)
	*wp++ = htons(services >> 16); *wp++ = htons(services & 0xffff);
	*wp++ = htons(0x426f); *wp++ = htons(0x000c);			// clr.w 12(sp)
	*wp++ = htons(0x205f);									// movea.l (sp)+,a0
	*wp++ = htons(0x508f);									// addq.l #8,sp
	*wp = htons(M68K_JMP_A0);								// jmp (a0)
	stub_mac = ROMBaseMac + stub_offset;

	const bool wanted = PrefsFindBool("powermgr");
	if (!BoardPower_HasBattery() || !wanted) {
		Serial.printf("[POWER] Power Manager emulation off (%s)\n",
		              !BoardPower_HasBattery() ? "no battery hardware" : "disabled in settings");
		return;
	}
	const uint32 powr = find_rom_powr_function();
	if (powr == 0) {
		Serial.println("[POWER] Power Manager emulation off: ROM Gestalt 'powr' not recognised");
		return;
	}
	*(uint16 *)(ROMBaseHost + powr) = htons(M68K_EMUL_OP_POWER_GESTALT);
	pm_enabled = true;
	Serial.printf("[POWER] Power Manager emulation on (ROM 'powr' at +%05x, stubs at %08x)\n",
	              (unsigned)powr, (unsigned)stub_mac);
}

void PowerManagerInstall(void)
{
	reset_guest_state();
	guest_sleeping = false;
	idle_state = kAwake;
	BoardPower_SetDisplayState(BOARD_DISPLAY_AWAKE);
	if (stub_mac == 0) return;

	M68kRegisters r;
	if (pm_enabled) {
		static const struct { uint16 trap; uint32 stub; } traps[] = {
			{0xa09e, kStubPowerMgrDispatch},
			{0xa09f, kStubPowerDispatch},
			{0xa085, kStubPMgrOp},
			{0xa08a, kStubSleep},
		};
		for (size_t i = 0; i < sizeof(traps) / sizeof(traps[0]); ++i) {
			memset(&r, 0, sizeof(r));
			r.d[0] = traps[i].trap;
			r.a[0] = stub_mac + traps[i].stub;
			Execute68kTrap(0xa247, &r);		// SetOSTrapAddress()
		}
	}

	Serial.printf("[POWER] Installed: Power Manager %s\n", pm_enabled ? "on" : "off");

	// Gestalt is not set up yet here; Gestalt('M5pw') is registered by the
	// boot hooks below.
	services_installed = false;
}

// Gestalt('M5pw') is registered at two points, because the table is
// rebuilt during startup:
//   boot 3       Gestalt works, but Mac OS 8.1's Gestalt (System 'ptch' 5)
//                then replaces the table and drops entries added so far;
//   INIT loads   after the System's patches, before each extension runs
//                (the Control Strip loads its modules from its INIT). This
//                checks first whether the entry is still there.
// With extensions off (Shift at startup) only the first applies, which is
// enough for systems that do not rebuild the table.
static bool services_registered(void)
{
	M68kRegisters r;
	memset(&r, 0, sizeof(r));
	r.d[0] = kPowerServicesGestalt;
	Execute68kTrap(0xa1ad, &r);				// Gestalt()
	return (r.d[0] & 0xffff) == 0 && r.a[0] == stub_mac + kStubServices;
}

static void register_services(const char *where, bool check_first)
{
	if (stub_mac == 0) return;
	if (check_first && services_registered()) {
		services_installed = true;
		return;
	}
	M68kRegisters r;
	memset(&r, 0, sizeof(r));
	r.d[0] = kPowerServicesGestalt;
	r.a[0] = stub_mac + kStubServicesGestalt;
	Execute68kTrap(0xa3ad, &r);				// NewGestalt()
	int16 err = (int16)(r.d[0] & 0xffff);
	if (err == -5552) {						// gestaltDupSelectorErr: someone else's 'M5pw'
		memset(&r, 0, sizeof(r));
		r.d[0] = kPowerServicesGestalt;
		r.a[0] = stub_mac + kStubServicesGestalt;
		Execute68kTrap(0xa5ad, &r);			// ReplaceGestalt()
		err = (int16)(r.d[0] & 0xffff);
	}
	services_installed = err == 0;
	Serial.printf("[POWER] Gestalt('M5pw') at %s: %s (%d)\n", where,
	              services_installed ? "registered" : "failed", err);
}

void PowerManagerBootHook(const char *where)
{
	register_services(where, false);
}

void PowerManagerEmulOp(uint16 opcode, M68kRegisters *r)
{
	switch (opcode) {
	case M68K_EMUL_OP_POWER_GESTALT:	// replaces "moveq #0,d0" in the ROM 'powr' function
		call_counts[kCountGestalt]++;
		r->d[0] = pm_enabled ? kGestaltPowr : 0;
		break;
	case M68K_EMUL_OP_POWER_DISPATCH:
		call_counts[kCountDispatch]++;
		r->d[0] = power_mgr_dispatch(r);
		break;
	case M68K_EMUL_OP_POWER_PRIVATE:
		call_counts[kCountPrivate]++;
		r->d[0] = power_dispatch(r);
		break;
	case M68K_EMUL_OP_PMGR_OP:
		r->d[0] = pmgr_op(r);
		break;
	case M68K_EMUL_OP_SLEEP:
		call_counts[kCountSleep]++;
		r->d[0] = sleep_trap(r);
		break;
	case M68K_EMUL_OP_POWER_SERVICES:
		call_counts[kCountServices]++;
		r->d[0] = services_call(r);
		break;
	}
}

bool PowerManagerActive(void)
{
	return pm_enabled;
}

bool PowerManagerSleeping(void)
{
	return guest_sleeping || idle_state == kBlanked;
}

void PowerManagerWake(void)
{
	last_activity_ms = millis();
	if (guest_sleeping) wake_requested = true;
	if (idle_state != kAwake) {
		idle_state = kAwake;
		BoardPower_SetDisplayState(BOARD_DISPLAY_AWAKE);
	}
}

void PowerManagerNoteActivity(void)
{
	last_activity_ms = millis();
	if (guest_sleeping || idle_state != kAwake) PowerManagerWake();
}

void PowerManagerIdlePoll(void)
{
	if (!pm_enabled || guest_sleeping) return;
	const uint32 idle_ms = millis() - last_activity_ms;
	const uint32 dim_ms = !settings.dimming_disabled ? settings.dimming_timeout * kTimeoutUnitMs : 0;
	const uint32 blank_ms = settings.auto_sleep_off == 0 ? settings.sleep_timeout * kTimeoutUnitMs : 0;
	int want = kAwake;
	if (blank_ms && idle_ms >= blank_ms) want = kBlanked;
	else if (dim_ms && idle_ms >= dim_ms) want = kDimmed;
	if (want == idle_state || (want == kAwake)) return;	// waking happens on activity
	idle_state = want;
	BoardPower_SetDisplayState(want == kBlanked ? BOARD_DISPLAY_ASLEEP : BOARD_DISPLAY_DIMMED);
	Serial.printf("[POWER] Idle %lu s: display %s\n", (unsigned long)(idle_ms / 1000),
	              want == kBlanked ? "off" : "dimmed");
}

int PowerManagerDescribe(char *buffer, size_t size)
{
	BoardBatteryStatus st = {};
	const bool battery = BoardPower_GetBattery(&st);
	int queue = 0;
	if (stub_mac) {
		for (uint32 e = sleep_queue; e && queue < 256; e = ReadMacInt32(e + kSleepQLink)) queue++;
	}
	const char *display = guest_sleeping ? "asleep" :
		idle_state == kBlanked ? "blanked" : idle_state == kDimmed ? "dimmed" : "awake";
	return snprintf(buffer, size,
		"pm=%d services=%d battery=%d present=%d percent=%u mv=%u ma=%d charging=%d "
		"connected=%d simulated=%d to_empty=%lu to_full=%lu backlight=%d display=%s "
		"sleep_timeout=%u dim_timeout=%u dimming=%d auto_sleep_off=%d sleep_queue=%d sleeps=%lu unknown=%lu "
		"calls=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
		pm_enabled ? 1 : 0, services_installed ? 1 : 0, battery ? 1 : 0,
		st.battery_present ? 1 : 0, st.percent, st.pack_mv, st.current_ma,
		st.charging ? 1 : 0, st.charger_connected ? 1 : 0, st.simulated ? 1 : 0,
		(unsigned long)st.seconds_to_empty, (unsigned long)st.seconds_to_full,
		BoardPower_HasBacklight() ? BoardPower_GetBacklight() : -1, display,
		settings.sleep_timeout, settings.dimming_timeout, settings.dimming_disabled ? 0 : 1,
		settings.auto_sleep_off, queue, (unsigned long)sleep_count,
		(unsigned long)unknown_selector_count,
		(unsigned long)call_counts[kCountDispatch], (unsigned long)call_counts[kCountPrivate],
		(unsigned long)call_counts[kCountPMgrOp], (unsigned long)call_counts[kCountIdleUpdate],
		(unsigned long)call_counts[kCountIdleState], (unsigned long)call_counts[kCountSerialPower],
		(unsigned long)call_counts[kCountSleep], (unsigned long)call_counts[kCountServices],
		(unsigned long)call_counts[kCountGestalt]);
}

// Mac OS 8.1's Gestalt (System 'ptch' 5) rebuilds the table at startup and
// sets 'powr' to $0F if _PMgrOp ($A085) is implemented, 0 otherwise. On the
// emulated Quadra it is implemented only because this file installs it, and
// $0F lacks gestaltPMgrDispatchExists:
//     move.w #$A89F,d0; _GetToolBoxTrapAddress; move.l a0,d2
//     move.l #$A085,d0; _GetOSTrapAddress; cmp.l a0,d2; beq.s +4
//     moveq #$0F,d0 ...
// Make that moveq report the dispatch bit too.
void PowerManagerCheckLoad(uint32 type, int16 id, uint8 *p, uint32 size)
{
	if (type == 0x494e4954) {					// 'INIT': an extension is about to run
		register_services("extension load", true);
		return;
	}
	if (!pm_enabled || type != 0x70746368 || p == NULL) return;	// 'ptch'
	static const uint8 powr_code[] = {
		0x30, 0x3c, 0xa8, 0x9f, 0xa7, 0x46, 0x24, 0x08, 0x20, 0x3c, 0x00, 0x00, 0xa0, 0x85,
		0xa3, 0x46, 0xb4, 0x88, 0x67, 0x04, 0x70, 0x0f,
	};
	for (uint32 i = 0; i + sizeof(powr_code) <= size; i += 2) {
		if (memcmp(p + i, powr_code, sizeof(powr_code)) != 0) continue;
		p[i + sizeof(powr_code) - 1] = (uint8)kGestaltPowr;
		FlushCodeCache(p + i, sizeof(powr_code));
		Serial.printf("[POWER] Patched Gestalt 'powr' in System ptch %d\n", id);
		return;
	}
}
