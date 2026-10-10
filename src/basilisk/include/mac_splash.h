/*
 * mac_splash.h - Classic-Mac themed pre-boot splash.
 *
 * This replaces the old BasiliskII text startup screen and the 3-second
 * "Change Settings" countdown inside boot_gui. The splash is the very
 * first visual once BoardDisplay_Init() returns and stays up until we
 * transition into the emulator. Tapping anywhere in the first 2 seconds
 * signals that the user wants the settings UI; otherwise the splash
 * hands off silently.
 *
 * Drawing uses the preboot canvas (preboot_ui.h): logical pixels at the
 * emulated Mac's resolution, shown pixel-doubled like the Mac screen.
 *
 * Touch polling reuses the BootGUI touch task so we don't stand up a
 * second FreeRTOS task just for the splash.
 */
#pragma once

#include <stdint.h>

namespace MacSplash {

/**
 * @brief Warm up the touch panel, start the shared touch task, and paint
 *        the splash (tiled BgTile background + centered Happy Mac).
 *
 * Safe to call once, after BoardDisplay_Init().
 */
void Begin();

/**
 * @brief Block up to `ms` milliseconds and return true if the user
 *        tapped-and-released the screen during that window.
 *
 * Any consumed press/release is cleared from the touch queue so it
 * doesn't get reinterpreted by the settings screen that follows.
 */
bool WaitForTapOrTimeout(uint32_t ms);

/**
 * @brief Repaint the splash, then free the preboot canvas so the emulator
 *        starts with that memory available. The splash stays on the panel
 *        until the emulator's first frame replaces it.
 */
void TransitionToEmulator();

/**
 * @brief Paint a Mac OS 8 stop alert with `msg` on a black screen. Used by
 *        main.cpp for the pre-SD / pre-boot-GUI fatal errors.
 */
void ShowErrorOverlay(const char *msg);

/**
 * @brief Paint the classic "It is now safe to switch off your Macintosh"
 *        alert on a black screen. Called after the emulator cleanly exits
 *        so the user isn't left with the stale last Mac framebuffer.
 */
void ShowSafeToPowerOff();

} // namespace MacSplash
