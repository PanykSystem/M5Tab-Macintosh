// Host test for src/board/board_power.cpp: charge estimate, charger
// inference, time estimates, simulation and backlight state.
#include "board_power.h"

#include <Arduino.h>
#include <cstdio>
#include <cstdlib>

uint32_t g_fake_millis = 1000;
FakeSerial Serial;

namespace {
bool g_has_battery = true;
BoardBatteryRaw g_raw = {};
int g_backlight_hw = -1;
int g_failures = 0;

void check(bool ok, const char *what, long got)
{
    if (!ok) {
        std::printf("FAIL: %s (got %ld)\n", what, got);
        ++g_failures;
    }
}

void sample(uint16_t mv, int16_t ma, bool charging, bool present = true)
{
    g_raw = {};
    g_raw.present = present;
    g_raw.pack_mv = mv;
    g_raw.current_ma = ma;
    g_raw.charging = charging;
    g_fake_millis += 2000;
    BoardPower_Poll();
}

BoardBatteryStatus status()
{
    BoardBatteryStatus st = {};
    BoardPower_GetBattery(&st);
    return st;
}
} // namespace

extern "C" bool BoardPowerHW_Describe(BoardBatteryDescription *out)
{
    out->cells_in_series = 2;
    out->capacity_mah = 2000;
    out->pack_resistance_mohm = 150;
    out->sample_interval_ms = 2000;
    return g_has_battery;
}

extern "C" bool BoardPowerHW_Read(BoardBatteryRaw *out)
{
    *out = g_raw;
    return true;
}

extern "C" bool BoardPowerHW_SetBacklight(int percent)
{
    g_backlight_hw = percent;
    return true;
}

extern "C" int BoardPowerHW_InitialBacklight(void)
{
    return 50;
}

extern "C" bool BoardPowerHW_SetCharging(bool)
{
    return true;
}

int main()
{
    BoardPower_Init();
    check(BoardPower_HasBattery(), "battery hardware reported", 0);
    check(BoardPower_GetBacklight() == 50, "initial backlight from the board", BoardPower_GetBacklight());

    // 8000 mV at 500 mA discharge: 8075 mV open-circuit, 4037 mV per cell, 81%.
    sample(8000, -500, false);
    BoardBatteryStatus st = status();
    check(st.battery_present, "pack present", 0);
    check(st.percent == 81, "level from the OCV curve with IR compensation", st.percent);
    check(!st.charger_connected, "a 500 mA drain means running on battery", 0);
    check(st.seconds_to_empty > 11000 && st.seconds_to_empty < 12500, "time to empty", st.seconds_to_empty);
    check(st.seconds_to_full == 0, "no time to full while discharging", st.seconds_to_full);

    // The load drops and the voltage recovers: the level must not rise.
    for (int i = 0; i < 30; ++i) sample(8150, -100, false);
    check(status().percent == 81, "level does not rise while discharging", status().percent);

    // Heavier load, lower voltage: the level falls, smoothly.
    sample(7500, -900, false);
    const int after_one = status().percent;
    check(after_one < 81 && after_one > 70, "one low sample moves the level a little", after_one);
    for (int i = 0; i < 200; ++i) sample(7500, -900, false);
    // 7635 mV open-circuit is 3818 mV per cell: 44%.
    check(status().percent == 44, "a sustained low voltage pulls the level down", status().percent);

    // Charging: connected, level never falls, time to full is known.
    const int before_charge = status().percent;
    sample(7600, 800, true);
    st = status();
    check(st.charging && st.charger_connected, "charging implies the charger is connected", 0);
    check(st.percent >= before_charge, "level does not fall while charging", st.percent);
    check(st.seconds_to_full > 0, "time to full while charging", st.seconds_to_full);
    check(st.seconds_to_empty == 0, "no time to empty while charging", st.seconds_to_empty);

    // On external power, finished charging, nearly full: report 100%.
    for (int i = 0; i < 200; ++i) sample(8350, 5, false);
    st = status();
    check(st.charger_connected && !st.charging, "small current means external power", 0);
    check(st.percent == 100, "charge finished shows full", st.percent);

    // Pack removed.
    sample(0, 0, false, false);
    st = status();
    check(!st.battery_present && st.percent == 0, "pack removed", st.percent);

    // Simulation overrides and clears.
    BoardPower_Simulate(true, 12, 0, 0, 1);
    st = status();
    check(st.simulated && st.battery_present && st.percent == 12 && !st.charger_connected,
          "simulated low battery", st.percent);
    check(st.seconds_to_empty > 0, "simulated time to empty", st.seconds_to_empty);
    BoardPower_Simulate(false, -1, -1, -1, -1);
    check(!status().simulated, "simulation off", 0);

    // Backlight: never fully dark by choice; dim and sleep keep the level.
    BoardPower_SetBacklight(0);
    check(BoardPower_GetBacklight() == 5 && g_backlight_hw == 5, "backlight floor is 5%", g_backlight_hw);
    BoardPower_SetBacklight(80);
    check(g_backlight_hw == 80, "backlight set", g_backlight_hw);
    BoardPower_SetDisplayState(BOARD_DISPLAY_DIMMED);
    check(g_backlight_hw == 5, "dimmed", g_backlight_hw);
    BoardPower_SetDisplayState(BOARD_DISPLAY_ASLEEP);
    check(g_backlight_hw == 0, "asleep turns the backlight off", g_backlight_hw);
    BoardPower_SetBacklight(60);
    check(g_backlight_hw == 0, "a level chosen while asleep waits for wake", g_backlight_hw);
    BoardPower_SetDisplayState(BOARD_DISPLAY_AWAKE);
    check(g_backlight_hw == 60, "wake restores the chosen level", g_backlight_hw);

    if (g_failures) return EXIT_FAILURE;
    std::printf("board_power: all checks passed\n");
    return EXIT_SUCCESS;
}
