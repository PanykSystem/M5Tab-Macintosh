# Power management: battery, sleep and backlight

On a board with a battery (the M5Stack Tab5), the emulated Mac has a
PowerBook-style Power Manager. Mac OS 8's own Control Strip modules then
work as they do on a PowerBook:

- **Battery Monitor** shows the charge level, whether the battery is
  charging, and the time remaining.
- **Sleep Now**, and the Finder's new **Special > Sleep**, turn the display
  off until the screen is touched (or a key is pressed or the mouse moves).
- **Power Settings** offers Better Conservation and Better Performance.
  After the dimming timeout the backlight dims; after the sleep timeout it
  turns off.
- The menu bar clock shows a battery gauge, as on a PowerBook.
- Disk spin-down calls (HD Spin Down) are accepted and do nothing; SD cards
  do not spin.

The emulator also offers its own **power services** to Mac software on
every board. The **Brightness** Control Strip module in
[`guest/control-strip-brightness`](guest/control-strip-brightness) uses them
to set the backlight from the Mac.

This document covers using it, how it works, and how to add battery
support to another board.

## Using it

### Control Strip

The Control Strip control panel must be enabled (Extensions Manager), and
the Control Strip must be showing. Battery Monitor, Sleep Now, Power
Settings and HD Spin Down are part of a standard Mac OS 8.1 install, in
*System Folder:Control Strip Modules*. They only appear when the Mac has a
Power Manager, so they show up once this firmware runs on a Tab5.

### Installing the Brightness module

The module is prebuilt in
[`guest/control-strip-brightness/dist`](guest/control-strip-brightness/dist).
Install it by any of these:

- **Download it on the Mac.** Serve `dist/Brightness.bin` from a computer
  on the same network (for example `python3 -m http.server` in that
  folder) and open `http://<computer>:8000/Brightness.bin` in Internet
  Explorer 3.01 on the Mac. It decodes MacBinary and saves **Brightness**
  (with a sun icon). Drag it onto the System Folder; the Finder offers to
  put it in Control Strip Modules. Restart. This is how it was installed on
  the test Tab5.
- **Shared folder** (not yet tried on hardware). Copy the whole `dist/M5Tab Extras` folder into the SD
  card folder you use as the Mac's shared folder. Copy the folder itself:
  its `.rsrc` and `.finf` subfolders are hidden and hold the module's
  resources and Finder information. On the Mac, drag **Brightness** from the
  shared volume onto the System Folder. Mac OS puts it in Control Strip
  Modules. Restart.
- **hfsutils**, with the disk image on a computer (for example over USB
  Disk mode):

  ```sh
  hmount Macintosh8.dsk
  hcopy -m guest/control-strip-brightness/dist/Brightness.bin ":System Folder:Control Strip Modules:Brightness"
  humount
  ```

Click the sun in the Control Strip to choose a brightness, or **Turn Off
Display**. A touch or key turns the display back on. The level is saved
in the Control Strip preferences and restored at startup. On a Mac without
the power services (another emulator, real hardware) the module does not
load.

The module logs its preference load and save results on the serial console
(`[POWER] Mac: Brightness: load preferences 0`). If they show −39, the
Control Strip Preferences file in *System Folder:Preferences* has an empty
resource fork, which an interrupted write can leave behind (it happened on
the test Tab5 after repeated hard resets). Every module's settings are then
lost on restart. Move that file to the Trash and restart; the Control Strip
creates a new one.

### Turning it off

The Power Manager is on by default on boards with a battery. To turn it
off, set `power_manager=no` in `/basilisk_settings.txt`, or over serial:

```sh
python3 tools/mac_control.py boot-set power_manager false
python3 tools/mac_control.py boot-save
```

There is no preboot checkbox. The power services (and the Brightness
module) stay available either way.

### Serial commands

| Command | Reply |
|---|---|
| `POWER` | One line of state: `pm` (Power Manager emulated), `services`, battery `present`, `percent`, `mv`, `ma`, `charging`, `connected`, `simulated`, `to_empty`/`to_full` (seconds), `backlight` (percent), `display` (`awake`, `dimmed`, `blanked`, `asleep`), the guest's `sleep_timeout`/`dim_timeout` (15 s units), `dimming`, `auto_sleep_off`, `sleep_queue` length, `sleeps` so far, `unknown` calls, and `calls`: counts of `_PowerMgrDispatch`, `_PowerDispatch`, `_PMgrOp`, `_IdleUpdate`, `_IdleState`, `_SerialPower`, `_Sleep`/sleep queue, power services and ROM `'powr'` calls since startup |
| `POWER SIM percent charging connected present` | Report these values instead of the hardware's. Each is a number; `-1` leaves that field live. `POWER SIM 15 0 0 1` is a nearly empty battery on its own power. |
| `POWER SIM OFF` | Back to live readings |
| `POWER BACKLIGHT n` | Set the backlight to `n` percent (5-100) |
| `POWER WAKE` | End a sleep or an idle blank, as a touch would |
| `POWER CHARGE 0` / `POWER CHARGE 1` | Test aid: turn the charger off / on (on again at every power-on) |

The `[POWER]` lines in the boot log say whether the Power Manager was
installed and why not, where `'M5pw'` was registered, the final
Gestalt `'powr'` value, and the first calls the emulation does not handle.

With the Power Manager on, the Finder's Special menu has a **Sleep** item
above Restart, so Restart and Shut Down move down one row (on the
640x360 guest: Sleep y 87, Restart y 103, Shut Down y 119). Automation that
clicks fixed menu positions must account for it.

## How it works

```
 Tab5 hardware                    firmware                              Mac OS 8
 ─────────────                    ────────                              ────────
 INA226 (V, I) ─┐                                                       Battery Monitor,
 charger enable ┼─► board_power_tab5.cpp ─► board_power.cpp ─┐          Sleep Now, Power Settings
 backlight PWM ◄┘   (BoardPowerHW_*)        level, charger,   │            │ Gestalt('powr')
                                            times, backlight   ▼            │ _PowerMgrDispatch $A09E
                                                         power_manager.cpp ◄─┤ _PowerDispatch    $A09F
                                                         (EMUL_OP handlers)  │ _PMgrOp           $A085
                                                                ▲            │ _Sleep            $A08A
 touch, keys ─► input task ─► activity, wake ───────────────────┘            │
                                                                            Brightness module
                                                                              Gestalt('M5pw')
```

### Why not emulate a PowerBook

Basilisk II emulates a Quadra (model ID 14, Quadra 900) and emulates no
hardware below the Toolbox: no VIA, no PMU. The Quadra 650 ROM contains
PowerBook code for `_Sleep`, `_PMgrOp` and `_PowerDispatch`, but it reaches
the PMU through `PowerMgrVars` ($D18), which is $FFFFFFFF here, and StartInit
leaves those traps unimplemented on a machine without a PMU.
`_PowerMgrDispatch` ($A09E) is not in the ROM at all. The ROM's Gestalt
`'powr'` function is `moveq #0,d0`, and Mac OS 8.1's own Gestalt reports 0
while `_PMgrOp` is unimplemented. So on the stock firmware Mac OS sees no
Power Manager and calls none of these.

The emulator answers the Power Manager calls itself, in C++, with the
machine still a Quadra. Mac OS 8.1's own Power Manager code is in PowerBook
`gpch` patch resources that a Quadra does not load, so nothing in the
System replaces the emulator's trap handlers (checked on hardware with
`PEEK`: the four trap table entries still point at the stubs at the
Finder).

### What the guest sees

At ROM patch time (`PowerManagerPatchROM`, called from `patch_rom_32`):

- Two-word stubs (`EMUL_OP`, `RTS`) are written at ROM `sony_offset+$E00`,
  the unused tail of the original `.Sony` driver, plus a Gestalt function
  for `'M5pw'` at `+$E20`.
- The ROM Gestalt table is searched for `'powr'`; its function's
  `moveq #0,d0` becomes `M68K_EMUL_OP_POWER_GESTALT`, which returns $1F.

At `EMUL_OP_INSTALL_DRIVERS`, on every boot (`PowerManagerInstall`),
`SetOSTrapAddress` points $A09E, $A09F, $A085 and $A08A at the stubs, and
all guest-visible settings and the sleep queue are reset.

Mac OS 8.1 then does two things the emulator has to follow up on, both
found on hardware:

- **Its Gestalt replaces the ROM's `'powr'`.** System `ptch` 5 is a new
  Gestalt that rebuilds the table at startup. It sets `'powr'` to $0F
  (exists, CPU idle, SCC and sound power) whenever `_PMgrOp` is
  implemented, which it now is, without `gestaltPMgrDispatchExists` (bit 4).
  Without bit 4, Battery Monitor and HD Spin Down read battery and disk
  state straight from `PowerMgrVars`. The resource-load hook
  (`PowerManagerCheckLoad`) finds that code in any loaded `ptch`
  (`move.w #$A89F,d0; _GetToolBoxTrapAddress; … _GetOSTrapAddress; cmp.l
  a0,d2; beq.s; moveq #$0F,d0`) and changes the constant to $1F. The ROM
  function returns $1F as well, so the value is the same before and after.
- **The rebuilt table drops `'M5pw'`.** `NewGestalt` fails at
  `INSTALL_DRIVERS` (Gestalt is not set up yet, error -108), works at the
  boot-3 hook (`EMUL_OP_FIX_BOOTSTACK`), and the entry is then lost when
  `ptch` 5 rebuilds the table. So the resource-load hook checks
  `Gestalt('M5pw')` whenever an `INIT` resource loads, which is after the
  System's patches and before each extension runs (the Control Strip loads
  its modules from its `INIT`), and registers it again if it is missing.
  Only `NewGestalt` allocates memory, and only once per boot. This happens
  on every board, with or without a battery.

The new EMUL_OPs are $713A-$713F (`M68K_EMUL_OP_POWER_*` in
`emul_op.h`).

### `_PowerMgrDispatch` ($A09E)

Selectors and records are those of Universal Interfaces 3.4 `Power.h`. The
selector is in the low word of D0, a one-word parameter in the high word,
a pointer in A0, and the result in D0.

| Selector | Call | Behavior |
|---|---|---|
| $00 | PMSelectorCount | $2C |
| $01 | PMFeatures | `canGetBatteryTime`, `hasDimmingSupport` |
| $02/$03 | Get/SetSleepTimeout | Stored; the idle display-off time (15 s units, 0 = never) |
| $04/$05 | Get/SetHardDiskTimeout | Stored only |
| $06 | HardDiskPowered | true |
| $07 | SpinDownHardDisk | Nothing to do |
| $08/$09 | IsSpindownDisabled/SetSpindownDisable | Stored only |
| $0A/$0B | HardDiskQInstall/Remove | noErr; never called back |
| $0C | GetScaledBatteryInfo | Battery 0 or 1: flags (installed $80, charging $40, charger connected $20), warning level (10%), level 0-255 |
| $0D | AutoSleepControl | Nesting count; while disabled there is no idle display-off |
| $0E/$0F | Get/SetIntModemInfo | No internal modem |
| $10/$11 | Maximum/CurrentProcessorSpeed | 33 (MHz) |
| $12/$13 | FullProcessorSpeed/SetProcessorSpeed | Stored; speed does not change |
| $14/$15 | Get/SetSCSIDiskModeAddress | Stored only |
| $16/$17 | Get/SetWakeupTimer | No wakeup timer |
| $18/$19 | IsProcessorCyclingEnabled/EnableProcessorCycling | Stored only |
| $1A | BatteryCount | 1 |
| $1B | GetBatteryVoltage | Pack voltage (Fixed, volts) |
| $1C | GetBatteryTimes | Time to empty (min/max ±15%), time to full; $FFFFFFFF when unknown |
| $1D/$1E | Get/SetDimmingTimeout | Stored; the idle dim time |
| $1F/$20 | DimmingControl/IsDimmingControlDisabled | Stored; gates dimming |
| $21 | IsAutoSlpControlDisabled | From AutoSleepControl |
| $22/$23 | PMgrStateQInstall/Remove | noErr; no notifications are sent |
| $24/$25 | UpdateSystemActivity/DelaySystemIdle | Count as user activity |
| $26/$27 | Get/SetStartupTimer | No startup timer |
| $28 | GetLastActivity | Ticks of the last activity |
| $29/$2A | Get/SetSoundMixerState | 0 / ignored |
| $2B/$2C | Get/SetDimSuspendState | Stored only |
| other | | paramErr |

### `_PowerDispatch` ($A09F) and `_PMgrOp` ($A085)

$A09F is Apple-private. Battery Monitor uses selector 9 for battery state
when `gestaltPMgrDispatchExists` is clear; it returns the same record as
`GetScaledBatteryInfo`. Battery Monitor also asks for selector $10 (power
consumption) and treats paramErr as "not available", which is also what
the ROM returns for selectors it does not know. Every other selector
returns paramErr.

Trap $85 is four calls, told apart by the trap word in D1:

| Trap | Call | Behavior |
|---|---|---|
| $A085 | `_PMgrOp` | A command for the PMU. Only $6C, which Battery Monitor uses to read the battery type, is answered: type 3, which turns on its time-remaining display. Everything else returns `pmReplyTOErr`. |
| $A285 | `_IdleUpdate` | Returns Ticks |
| $A485 | `_IdleState` | D0 −1 (`GetCPUSpeed`): 33; 0/1 (`EnableIdle`/`DisableIdle`): 0 |
| $A685 | `_SerialPower` | noErr |

Mac OS 8.1 calls the last three at startup once `'powr'` has its bits 1-3
set.

### Sleep ($A08A)

`_SleepQInstall` ($A28A) and `_SleepQRemove` ($A48A) share the trap number
with `_Sleep` and are told apart by the trap word in D1. The emulator keeps
the sleep queue (checking `sleepQType`, returning `slpTypeErr` and `qErr`
as the ROM does). Open Transport and others install seven procedures at
startup.

A real Power Manager now runs the sleep protocol from *Inside Macintosh:
Devices*: `sleepRequest`, `sleepRevoke` on a refusal, `sleepDemand`, sleep,
`sleepWakeUp`. The emulator does **not** call the procedures. An early
version did, and on the Tab5 the Display Manager's procedure
(`_DisplayDispatch` selector $08E7) jumped to unmapped memory on
`sleepDemand` and the firmware reset: it expects PowerBook display
hardware. Nothing loses power in the emulator's sleep, so the procedures
have nothing to prepare, and to Mac software the sleep looks like a pause.

"Sleep" turns the backlight off and parks the 68k CPU in the trap until the
input task sees a touch, a key or a mouse movement (or `POWER WAKE`). The
waking touch is not passed to the Mac. Other firmware tasks, including
Wi-Fi, keep running, and the Mac's clock is read from the host when it
resumes. Open Transport kept working across a sleep in testing.

### Idle dimming and display-off

The input task notes activity on every touch, key and mouse movement, and
on `UpdateSystemActivity`/`DelaySystemIdle`. With Power Settings'
timeouts set, `PowerManagerIdlePoll` dims the backlight after the dimming
timeout and turns it off after the sleep timeout. The Mac keeps running;
it is not told. Activity restores the backlight, and a touch that turns the
display back on is not passed to the Mac.

### Power services (`'M5pw'`)

Any Mac program can use these. `Gestalt('M5pw', &response)` returns the
address of a C function:

```c
typedef long (*M5PowerServices)(long selector, long argument, void *buffer);
```

Arguments are pushed right to left and the result is in D0, which is the C
convention of MPW C, CodeWarrior (with 4-byte `int`s or `long` parameters)
and Retro68.

| Selector | Call | Result |
|---|---|---|
| 0 | Version | 1 |
| 1 | Capabilities | Bits: 0 backlight, 1 battery, 2 Power Manager emulated, 3 sleep |
| 2 | GetBacklight | Percent, 5-100 |
| 3 | SetBacklight (`argument` = percent) | The level set |
| 4 | GetBattery (`buffer` = 16 bytes) | 0, or paramErr without a battery |
| 5 | SleepDisplay | 0. The display turns off until a touch or key; the Mac keeps running |
| 6 | Log (`buffer` = Pascal string or NULL, `argument` = number) | 0. Prints `[POWER] Mac: <string> <number>` on the serial console, for debugging guest software |

The GetBattery record: byte 0 flags (bit 7 present, 6 charging, 5 charger
connected, 0 simulated), byte 1 percent, word 2 pack mV, word 4 current mA
(signed, positive when charging), word 6 reserved, long 8 seconds to empty,
long 12 seconds to full (0 when unknown).

Software should check Gestalt first. The services only exist in this
emulator, and the call is an emulator trap.

## The battery estimate

`board_power.cpp` turns raw readings into a level, without a fuel gauge:

1. The pack voltage is corrected for the drop across the pack's internal
   resistance at the measured current (`pack_resistance_mohm`), giving an
   estimate of its open-circuit voltage.
2. The per-cell voltage is looked up on a typical Li-ion open-circuit
   curve (4.20 V = 100%, 3.84 V = 50%, 3.27 V = 0%).
3. The result is smoothed with a 60 s time constant. The shown level only
   falls on battery and only rises while charging, so the gauge does not
   jump with the load.
4. With external power and the charger finished, a level of 90% or more
   shows as 100%.

External power is the charger's status bit, or a pack current above
−40 mA when the board cannot measure it directly (a Tab5 on battery draws
far more). Times are capacity × level ÷ smoothed current (20 s time
constant, restarted when the charger is plugged or unplugged).

### Tab5 specifics

The Tab5 has no fuel-gauge IC. M5Unified configures an INA226 (5 mΩ
shunt) on the internal I2C bus that measures the 2S NP-F550 pack (7.4 V,
2000 mAh). The firmware reads it through `M5.Power.getBatteryVoltage()` and
`getBatteryCurrent()` every 2 s on the input task, which already owns that
bus for touch. There is no VBUS sense.

Measured on a Tab5 with `POWER CHARGE` (below):

- On USB power the system runs from USB through a power path. The INA226
  sees only the pack's own current: about −200 mA in M5Unified's sign
  convention near full charge, and exactly 0 with the charger disabled,
  whatever the backlight level. M5Unified reports charging as negative, so
  the Tab5 hook inverts it.
- The charger's CHG_STAT line (IO expander 0x44 pin 6,
  `M5.Power.isCharging()`) read "charging" with the charger disabled, so it
  is not used. Charging is a current into the pack above 20 mA.

Not yet measured: readings on battery power alone, which needs the USB
cable out and so a log on the SD card or a display of `POWER` on the Mac,
and with the pack removed.

`POWER CHARGE 0` turns the charger off (CHG_EN, IO expander 0x44 pin 7) and
`POWER CHARGE 1` back on; M5Unified turns it on at every power-on. This is
for testing the charger-connected-but-not-charging state.

## Adding battery support to a board

1. Create `src/board/<board>/board_power_<board>.cpp` and implement the
   four hooks in [`src/board/board_power.h`](src/board/board_power.h):
   - `BoardPowerHW_Describe`: cells in series, capacity, pack resistance
     and sample interval. Return false if the board has no battery; the Mac
     then sees no Power Manager.
   - `BoardPowerHW_Read`: pack millivolts, signed milliamps (positive into
     the pack), presence, charging, and the charger state if the board can
     measure it (`charger_known`). It runs on the input task.
   - `BoardPowerHW_SetBacklight` and `BoardPowerHW_InitialBacklight`
     (return −1 from the latter if the backlight cannot be set).
2. Board files are picked up by the per-board `build_src_filter` in
   `platformio.ini`; nothing else needs to change.
3. Check the sign of the current and the voltage scale on hardware with
   `POWER`, on battery and on the charger. Check the level against a full
   and a nearly empty pack; adjust `pack_resistance_mohm` if the level drops
   sharply under load and recovers when idle.
4. Run the host test (`python3 -m unittest discover -s test -p test_power.py`).

A board with a fuel gauge can report the gauge's own level by returning a
voltage from the gauge's state of charge, or by extending
`BoardBatteryRaw`. Change the estimator in one place, `board_power.cpp`.

## Testing

- Host: `python3 -m unittest discover -s test -p 'test_*.py'` builds and
  runs `test/test_board_power.cpp` (estimator, charger inference, times,
  simulation, backlight states) and checks the committed module build.
- Hardware: the boot log's `[POWER]` lines; `PEEK 678 4` (and `67C`, `614`,
  `628`) return the stub addresses; `POWER` on battery and on the charger;
  `POWER SIM` for low and charging states; Sleep Now and `POWER WAKE`;
  the Brightness menu; Speedometer for performance.

### Performance

The emulation adds no measurable load. Across a full Speedometer rating
Mac OS made about 260 `_PowerMgrDispatch` calls, one `_IdleUpdate`, two
`_IdleState` and 100 power-services calls; the input task reads the INA226
every 2 s.

The Control Strip is another matter. It floats over the bottom of the
screen, and Speedometer's Graphics test draws underneath it. On the Tab5
(USB-Debug image, 24 MB, Mac OS 8.1 with networking), one rating each:

| Control Strip | Graphics | PR |
|---|---|---|
| 5.2, stock strip (3 modules), 2 runs | 1.015-1.018 | 1.000-1.002 |
| Power Manager off (4 modules) | 1.001-1.002 | 0.997-0.998 |
| Power Manager on (9 modules), 4 runs | 0.944-0.947 | 0.977-0.982 |
| Power Manager on, strip collapsed | 1.406 | 1.091 |

CPU, Disk and Math do not change. Compare Speedometer results only with
the Control Strip in the same state (collapse it, or keep the same modules).

## Limitations

- Sleep keeps the ESP32-P4, Wi-Fi and the display controller powered; only
  the backlight is off and the 68k CPU is parked. Idle display-off does not
  park the CPU at all.
- Sleep procedures are not called (see Sleep above), so software that
  wants to know about sleep is not told.
- No low-battery warnings, and no automatic shutdown on an empty battery.
  The Mac shows the level but is not told when it becomes critical.
- State-change notifications (`PMgrStateQInstall`) are accepted and never
  sent.
- Battery Monitor's "consumption" display is not available ($A09F).
- The wakeup and startup timers, processor cycling and speed changes are
  stored but have no effect.
- Power Settings' timeouts last until the Mac restarts. A PowerBook keeps
  them in PMU PRAM; storing them in Basilisk's XPRAM would need bytes that
  no Mac OS component uses on a Quadra, which has not been established.
- The Waveshare board has no battery; there the Mac sees no Power Manager,
  and only the power services (backlight) are available. It is
  build-tested only.
