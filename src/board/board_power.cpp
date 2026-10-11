/*
 * board_power.cpp - board-independent battery estimation and backlight state
 *
 * Boards supply raw pack voltage, current and charger state through the
 * BoardPowerHW_* hooks. This file turns them into what a Mac Power Manager
 * reports: a smoothed charge level, whether the charger is connected, and
 * time-to-empty / time-to-full estimates.
 *
 * The level comes from a Li-ion open-circuit-voltage curve. Pack voltage is
 * first corrected for the voltage drop across the pack's internal
 * resistance at the measured current, then smoothed, and the shown level is
 * only allowed to fall while discharging and rise while charging so the
 * Control Strip gauge does not flicker with load.
 */
#include "board_power.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

// Per-cell open-circuit voltage (mV) to state of charge (%), typical
// LiCoO2/NMC curve. Descending voltage.
struct OcvPoint { uint16_t mv; uint8_t percent; };
const OcvPoint kOcvCurve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75},
    {3950, 70},  {3910, 65}, {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45},
    {3800, 40},  {3790, 35}, {3770, 30}, {3750, 25}, {3730, 20}, {3710, 15},
    {3690, 10},  {3610, 5},  {3270, 0},
};

// Smoothing time constants for level and current, and the current below
// which the pack counts as idle.
constexpr float kSmoothingSeconds = 60.0f;
constexpr float kCurrentSmoothingSeconds = 20.0f;
constexpr int kIdleCurrentMa = 20;
// A Tab5-class device draws far more than this from its pack whenever it
// runs on battery, so a smaller drain means external power is present.
constexpr int kOnBatteryDrainMa = 40;

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
bool s_inited = false;
bool s_has_battery = false;
BoardBatteryDescription s_desc = {};
BoardBatteryStatus s_status = {};
uint32_t s_last_read_ms = 0;
float s_level = -1.0f;        // smoothed level, < 0 before the first sample
float s_current = 0.0f;       // smoothed current, mA
int s_shown = -1;             // level reported, with the monotonic rules applied
bool s_was_connected = false; // charger state at the previous sample

int s_charge_request = -1;     // BoardPower_SetChargingEnabled, applied by Poll

struct Simulation { bool on; int percent, charging, connected, present; };
Simulation s_sim = {false, -1, -1, -1, -1};

// Backlight state changes come from the CPU thread (guest sleep, services),
// the input task (wake, idle) and the serial task. The mutex keeps the
// state and the physical level in step.
SemaphoreHandle_t s_backlight_mutex = nullptr;
bool s_has_backlight = false;
int s_backlight = 100;
BoardDisplayPowerState s_display_state = BOARD_DISPLAY_AWAKE;
constexpr int kDimPercent = 5;

int displayLevel(void)
{
    switch (s_display_state) {
    case BOARD_DISPLAY_ASLEEP: return 0;
    case BOARD_DISPLAY_DIMMED: return s_backlight < kDimPercent ? s_backlight : kDimPercent;
    default: return s_backlight;
    }
}

float levelFromCellMv(float mv)
{
    const size_t n = sizeof(kOcvCurve) / sizeof(kOcvCurve[0]);
    if (mv >= kOcvCurve[0].mv) return 100.0f;
    if (mv <= kOcvCurve[n - 1].mv) return 0.0f;
    for (size_t i = 1; i < n; ++i) {
        if (mv >= kOcvCurve[i].mv) {
            const OcvPoint &hi = kOcvCurve[i - 1], &lo = kOcvCurve[i];
            return lo.percent + (mv - lo.mv) * (hi.percent - lo.percent) / float(hi.mv - lo.mv);
        }
    }
    return 0.0f;
}

void applySimulation(const Simulation &sim, BoardBatteryStatus *st)
{
    if (!sim.on) return;
    st->simulated = true;
    if (sim.present >= 0) st->battery_present = sim.present != 0;
    if (sim.connected >= 0) st->charger_connected = sim.connected != 0;
    if (sim.charging >= 0) st->charging = sim.charging != 0;
    if (sim.percent >= 0) st->percent = (uint8_t)(sim.percent > 100 ? 100 : sim.percent);
    if (!st->battery_present) {
        st->percent = 0;
        st->charging = false;
    }
    // Plausible estimates for the simulated state.
    const uint32_t capacity = s_desc.capacity_mah ? s_desc.capacity_mah : 2000;
    st->seconds_to_empty = (!st->charger_connected && st->battery_present)
        ? capacity * 36u * st->percent / 1000u * 2u : 0;    // as if drawing 500 mA
    st->seconds_to_full = (st->charging && st->percent < 100)
        ? capacity * 36u * (100u - st->percent) / 1000u * 2u : 0;
}

} // namespace

extern "C" void BoardPower_Init(void)
{
    if (s_inited) return;
    BoardBatteryDescription desc = {};
    const bool battery = BoardPowerHW_Describe(&desc);
    if (battery && desc.cells_in_series == 0) desc.cells_in_series = 1;
    if (battery && desc.sample_interval_ms == 0) desc.sample_interval_ms = 2000;
    const int initial = BoardPowerHW_InitialBacklight();
    if (s_backlight_mutex == nullptr) s_backlight_mutex = xSemaphoreCreateMutex();

    portENTER_CRITICAL(&s_lock);
    s_desc = desc;
    s_has_battery = battery;
    s_status = {};
    s_status.has_battery_hardware = battery;
    s_has_backlight = initial >= 0;
    s_backlight = initial >= 0 ? initial : 100;
    s_inited = true;
    portEXIT_CRITICAL(&s_lock);

    Serial.printf("[POWER] Battery hardware: %s", battery ? "yes" : "no");
    if (battery) {
        Serial.printf(" (%uS, %u mAh)", desc.cells_in_series, desc.capacity_mah);
    }
    Serial.printf(", backlight %s", s_has_backlight ? "adjustable" : "fixed");
    if (s_has_backlight) Serial.printf(" at %d%%", s_backlight);
    Serial.println();
}

extern "C" void BoardPower_Poll(void)
{
    if (!s_inited || !s_has_battery) return;
    const int charge_request = __atomic_exchange_n(&s_charge_request, -1, __ATOMIC_ACQ_REL);
    if (charge_request >= 0) {
        BoardPowerHW_SetCharging(charge_request != 0);
        s_last_read_ms = 0;     // sample again now
    }
    const uint32_t now = millis();
    if (s_last_read_ms != 0 && now - s_last_read_ms < s_desc.sample_interval_ms) return;
    const float dt = s_last_read_ms ? (now - s_last_read_ms) / 1000.0f : 0.0f;
    s_last_read_ms = now;

    BoardBatteryRaw raw = {};
    if (!BoardPowerHW_Read(&raw)) return;

    BoardBatteryStatus st = {};
    st.has_battery_hardware = true;
    st.battery_present = raw.present;
    st.charging = raw.present && raw.charging;
    st.pack_mv = raw.pack_mv;
    st.current_ma = raw.current_ma;
    st.charger_connected = raw.charger_known
        ? raw.charger_connected
        : (raw.charging || raw.current_ma > -kOnBatteryDrainMa);
    st.sample_ms = now;

    if (raw.present) {
        // Undo the IR drop (discharging) or rise (charging), then look up
        // the per-cell open-circuit voltage.
        const float ocv_pack = raw.pack_mv - raw.current_ma * (float)s_desc.pack_resistance_mohm / 1000.0f;
        const float level = levelFromCellMv(ocv_pack / s_desc.cells_in_series);
        if (s_level < 0.0f || dt <= 0.0f) {
            s_level = level;
        } else {
            s_level += dt / (kSmoothingSeconds + dt) * (level - s_level);
        }
        // Plugging or unplugging the charger reverses the current; start the
        // estimate again instead of averaging across the change.
        if (s_shown < 0 || dt <= 0.0f || st.charger_connected != s_was_connected) {
            s_current = raw.current_ma;
        } else {
            s_current += dt / (kCurrentSmoothingSeconds + dt) * (raw.current_ma - s_current);
        }
        s_was_connected = st.charger_connected;

        // The shown level only falls on battery and only rises while
        // charging. Hold the smoothed level at the shown one too, so a
        // voltage that recovers under a lighter load does not build up a
        // lead that later delays a real fall.
        if (s_shown >= 0) {
            if (!st.charger_connected && s_level > s_shown) s_level = (float)s_shown;
            if (st.charging && s_level < s_shown) s_level = (float)s_shown;
        }
        int shown = (int)(s_level + 0.5f);
        if (st.charger_connected && !st.charging && shown >= 90) {
            shown = 100;    // the charger has finished
        }
        if (shown < 0) shown = 0;
        if (shown > 100) shown = 100;
        s_shown = shown;
        st.percent = (uint8_t)shown;

        const uint32_t capacity = s_desc.capacity_mah;
        if (!st.charger_connected && s_current < -kIdleCurrentMa && capacity) {
            st.seconds_to_empty = (uint32_t)(capacity * (shown / 100.0f) / -s_current * 3600.0f);
        }
        if (st.charging && s_current > kIdleCurrentMa && capacity && shown < 100) {
            st.seconds_to_full = (uint32_t)(capacity * ((100 - shown) / 100.0f) / s_current * 3600.0f);
        }
    } else {
        s_level = -1.0f;
        s_shown = -1;
        s_current = 0.0f;
        s_was_connected = st.charger_connected;
    }

    portENTER_CRITICAL(&s_lock);
    s_status = st;
    portEXIT_CRITICAL(&s_lock);
}

extern "C" bool BoardPower_GetBattery(BoardBatteryStatus *out)
{
    portENTER_CRITICAL(&s_lock);
    BoardBatteryStatus st = s_status;
    const Simulation sim = s_sim;
    const bool battery = s_has_battery;
    portEXIT_CRITICAL(&s_lock);
    applySimulation(sim, &st);
    if (out) *out = st;
    return battery;
}

extern "C" bool BoardPower_HasBattery(void)
{
    return s_has_battery;
}

extern "C" void BoardPower_Simulate(bool enable, int percent, int charging, int connected, int present)
{
    portENTER_CRITICAL(&s_lock);
    s_sim = {enable, percent, charging, connected, present};
    portEXIT_CRITICAL(&s_lock);
}

extern "C" bool BoardPower_SetChargingEnabled(bool enable)
{
    if (!s_has_battery) return false;
    __atomic_store_n(&s_charge_request, enable ? 1 : 0, __ATOMIC_RELEASE);
    return true;
}

extern "C" bool BoardPower_HasBacklight(void)
{
    return s_has_backlight;
}

extern "C" int BoardPower_GetBacklight(void)
{
    return s_backlight;
}

extern "C" void BoardPower_SetBacklight(int percent)
{
    // Never let software turn the panel fully dark; that is what sleep is for.
    if (percent < 5) percent = 5;
    if (percent > 100) percent = 100;
    if (s_backlight_mutex) xSemaphoreTake(s_backlight_mutex, portMAX_DELAY);
    s_backlight = percent;
    if (s_display_state == BOARD_DISPLAY_AWAKE) BoardPowerHW_SetBacklight(percent);
    if (s_backlight_mutex) xSemaphoreGive(s_backlight_mutex);
}

extern "C" void BoardPower_SetDisplayState(BoardDisplayPowerState state)
{
    if (s_backlight_mutex) xSemaphoreTake(s_backlight_mutex, portMAX_DELAY);
    if (state != s_display_state) {
        s_display_state = state;
        BoardPowerHW_SetBacklight(displayLevel());
    }
    if (s_backlight_mutex) xSemaphoreGive(s_backlight_mutex);
}

extern "C" BoardDisplayPowerState BoardPower_GetDisplayState(void)
{
    return s_display_state;
}
