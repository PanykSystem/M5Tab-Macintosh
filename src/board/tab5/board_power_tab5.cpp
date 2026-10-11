/*
 * board_power_tab5.cpp - Tab5 battery and backlight hooks
 *
 * The Tab5 has no fuel-gauge IC. An INA226 on the internal I2C bus measures
 * the 2S pack (NP-F550, 7.4 V nominal, 2000 mAh) across a 5 mOhm shunt.
 * M5Unified configures it in M5.begin(); this file only reads it.
 *
 * Measured on a Tab5 (2026-10-10, POWER CHARGE test): on USB power the
 * system runs from USB through a power path, so the INA226 sees only the
 * pack's own current, and M5Unified reports charging current as negative
 * (about -200 mA near full; exactly 0 with the charger disabled, at any
 * backlight level). The charger's CHG_STAT line (IO expander 0x44 pin 6,
 * M5.Power.isCharging()) read "charging" even with the charger disabled,
 * so it is not used. Charging and external power both come from the
 * current: board_power.cpp treats a drain under 40 mA as external power,
 * since the Tab5 draws far more than that whenever it runs on its battery.
 *
 * The backlight is the LEDC PWM channel M5GFX set up (0-255).
 */
#include "../board_power.h"

#include <M5Unified.h>

namespace {
// Below 2.5 V per cell there is no usable 2S pack on the terminals.
constexpr int kMinPresentPackMv = 5000;
// Current into the pack above which it counts as charging.
constexpr int kChargingMa = 20;
}

extern "C" bool BoardPowerHW_Describe(BoardBatteryDescription *out)
{
    out->cells_in_series = 2;
    out->capacity_mah = 2000;
    out->pack_resistance_mohm = 150;
    out->sample_interval_ms = 2000;
    return true;
}

extern "C" bool BoardPowerHW_Read(BoardBatteryRaw *out)
{
    const int mv = M5.Power.getBatteryVoltage();
    if (mv < 0) return false;
    out->pack_mv = (uint16_t)(mv > 65535 ? 65535 : mv);
    // Positive into the pack, as BoardBatteryRaw expects.
    const int32_t ma = -M5.Power.getBatteryCurrent();
    out->current_ma = (int16_t)(ma > 32767 ? 32767 : (ma < -32768 ? -32768 : ma));
    out->present = mv >= kMinPresentPackMv;
    out->charging = out->present && out->current_ma > kChargingMa;
    out->charger_known = false;
    out->charger_connected = false;
    return true;
}

extern "C" bool BoardPowerHW_SetBacklight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    M5.Display.setBrightness((uint8_t)((percent * 255 + 50) / 100));
    return true;
}

extern "C" int BoardPowerHW_InitialBacklight(void)
{
    return (M5.Display.getBrightness() * 100 + 127) / 255;
}

extern "C" bool BoardPowerHW_SetCharging(bool enable)
{
    M5.Power.setBatteryCharge(enable);  // CHG_EN, IO expander 0x44 pin 7
    return true;
}
