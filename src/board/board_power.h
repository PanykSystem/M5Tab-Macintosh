/*
 * board_power.h - battery, backlight and display-sleep HAL
 *
 * The emulated Mac's Power Manager (src/basilisk/power_manager.cpp) reads
 * everything it reports about the battery from here, and drives the
 * backlight through here. A board adds battery support by implementing the
 * three BoardPowerHW_* hooks at the bottom of this file in its own
 * src/board/<board>/board_power_<board>.cpp; the shared code in
 * board_power.cpp turns raw readings into a smoothed charge level, time
 * estimates and charger state. See POWER_MANAGEMENT.md.
 *
 * Threading: BoardPower_Poll() is the only function that touches battery
 * hardware and runs on the input task (the task that already owns the
 * touch controller's I2C bus). Every other function may be called from any
 * task and only reads or writes cached state.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     has_battery_hardware;  // the board can measure a battery at all
    bool     battery_present;       // a pack is connected
    bool     charger_connected;     // external power is present
    bool     charging;              // the charger reports charging
    bool     simulated;             // values come from BoardPower_Simulate()
    uint8_t  percent;               // state of charge, 0-100, smoothed
    uint16_t pack_mv;               // pack voltage (all cells), mV
    int16_t  current_ma;            // pack current, > 0 charging, < 0 discharging
    uint32_t seconds_to_empty;      // 0 when unknown or not discharging
    uint32_t seconds_to_full;       // 0 when unknown or not charging
    uint32_t sample_ms;             // millis() of the reading, 0 = none yet
} BoardBatteryStatus;

/** Initialise state; call once after Board_Init(). Safe without a battery. */
void BoardPower_Init(void);

/**
 * Read the battery hardware if the sample interval has passed and update
 * the cached status. Input task only.
 */
void BoardPower_Poll(void);

/** Copy the latest status. Returns false when the board has no battery. */
bool BoardPower_GetBattery(BoardBatteryStatus *out);

/** True when the board has battery measurement hardware. */
bool BoardPower_HasBattery(void);

/**
 * Replace live readings with fixed values for testing (percent 0-100,
 * negative leaves that field live). BoardPower_Simulate(false, ...) returns
 * to live readings.
 */
void BoardPower_Simulate(bool enable, int percent, int charging, int connected, int present);

/* Backlight. Levels are percent, 5-100 (software never chooses fully dark;
 * that is what display sleep is for). The chosen level is kept while the
 * display is dimmed or asleep and restored when it wakes. */
bool BoardPower_HasBacklight(void);
int  BoardPower_GetBacklight(void);
void BoardPower_SetBacklight(int percent);

/**
 * Test aid: stop (false) or allow (true) charging. Applied on the next
 * BoardPower_Poll(). Boards reset this at power-on. Returns false if the
 * board cannot control its charger.
 */
bool BoardPower_SetChargingEnabled(bool enable);

typedef enum {
    BOARD_DISPLAY_AWAKE = 0,    // backlight at the chosen level
    BOARD_DISPLAY_DIMMED = 1,   // backlight at the dim level
    BOARD_DISPLAY_ASLEEP = 2,   // backlight off
} BoardDisplayPowerState;

void BoardPower_SetDisplayState(BoardDisplayPowerState state);
BoardDisplayPowerState BoardPower_GetDisplayState(void);

/* --------------------------------------------------------------------------
 * Board hooks (implemented once per board)
 * ------------------------------------------------------------------------ */

typedef struct {
    uint8_t  cells_in_series;       // 1 for a single Li-ion cell, 2 for 2S, ...
    uint16_t capacity_mah;          // nominal pack capacity, for time estimates
    uint16_t pack_resistance_mohm;  // pack internal resistance, for load compensation
    uint16_t sample_interval_ms;    // how often BoardPower_Poll() reads the hardware
} BoardBatteryDescription;

typedef struct {
    bool     present;               // pack detected
    bool     charging;              // charger status pin or register
    bool     charger_known;         // charger_connected below is measured, not inferred
    bool     charger_connected;
    uint16_t pack_mv;
    int16_t  current_ma;            // > 0 into the pack, < 0 out of it
} BoardBatteryRaw;

/** Fill in the pack description. Return false when there is no battery hardware. */
bool BoardPowerHW_Describe(BoardBatteryDescription *out);

/** Read the hardware (input task). Return false on a failed read. */
bool BoardPowerHW_Read(BoardBatteryRaw *out);

/** Physical backlight control, 0-100 (0 = off). Return false if unsupported. */
bool BoardPowerHW_SetBacklight(int percent);

/** The level the backlight was left at by board bring-up, or -1 if unknown. */
int BoardPowerHW_InitialBacklight(void);

/** Enable or disable the charger (input task). Return false if unsupported. */
bool BoardPowerHW_SetCharging(bool enable);

#ifdef __cplusplus
}
#endif
