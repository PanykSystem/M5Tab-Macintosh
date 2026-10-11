/*
 * board_power_waveshare.cpp - Waveshare ESP32-P4 10.1" power hooks
 *
 * The board has no battery, so the Mac sees no Power Manager. The backlight
 * is the BSP's LEDC PWM channel, which board_display_waveshare.cpp sets to
 * 100% at start-up.
 */
#include "../board_power.h"

#include "bsp/display.h"

extern "C" bool BoardPowerHW_Describe(BoardBatteryDescription *out)
{
    (void)out;
    return false;
}

extern "C" bool BoardPowerHW_Read(BoardBatteryRaw *out)
{
    (void)out;
    return false;
}

extern "C" bool BoardPowerHW_SetBacklight(int percent)
{
    return bsp_display_brightness_set(percent) == ESP_OK;
}

extern "C" int BoardPowerHW_InitialBacklight(void)
{
    return 100;
}

extern "C" bool BoardPowerHW_SetCharging(bool enable)
{
    (void)enable;
    return false;
}
