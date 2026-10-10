/*
 * mac_splash.cpp - see mac_splash.h.
 */

#include <Arduino.h>

#include "mac_splash.h"
#include "board.h"
#include "board_display.h"
#include "boot_gui.h"            /* touch task + BootGUITouch event */
#include "preboot_screens.h"

namespace {

/* Paint into the preboot canvas and show it. Without memory for the canvas
 * (only possible after the emulator ran), fall back to a black screen. */
bool paint(void (*painter)(void *), void *ctx)
{
    if (!PrebootUI::Begin()) {
        BoardDisplay_Gfx().fillScreen(0x0000);
        BoardDisplay_Present();
        return false;
    }
    PrebootUI::InvalidateAll();
    painter(ctx);
    PrebootUI::Present();
    return true;
}

void paint_splash()
{
    paint([](void *) { PrebootScreens::PaintSplash(); }, nullptr);
}

void paint_alert(PrebootScreens::AlertView *view)
{
    paint([](void *ctx) {
        auto *v = static_cast<PrebootScreens::AlertView *>(ctx);
        PrebootScreens::PaintAlert(
            PrebootScreens::LayoutAlert(PrebootUI::Width(), PrebootUI::Height(), *v), *v);
    }, view);
}

void drain_touch_edges()
{
    /* Consume any buffered edges so the splash's leftover press/release
     * doesn't instantly trigger the settings screen's own handlers. The
     * touch task overwrites the queue at ~60 Hz, so a couple of short
     * delays between polls is enough to see the cleared state land. */
    BootGUITouch t;
    for (int i = 0; i < 4; ++i) {
        if (!BootGUI_PollTouch(&t)) break;
        if (!t.was_pressed && !t.was_released) break;
        delay(20);  /* allow the 60 Hz touch task to refresh the queue */
    }
}

} // namespace

namespace MacSplash {

void Begin()
{
    /* Paint the splash FIRST so the touch-warmup happens while the
     * user is looking at the Happy Mac, not at a plain black screen. */
    Serial.printf("[MAC_SPLASH] Painting splash (%dx%d)\n",
                  BoardDisplay_Width(), BoardDisplay_Height());
    paint_splash();

    /* Start the touch task BEFORE the warmup so any taps the user
     * lands while the Happy Mac is on screen are actually captured.
     * Previously the warmup ran first as a synchronous Board_Update
     * loop, which meant ~1 s of splash-visible time during which the
     * tap-detection path was completely deaf - users reasonably
     * tapping the splash had a window of just SPLASH_TAP_WINDOW_MS
     * minus reaction time and ended up booting straight to Mac OS.
     * The touch task itself calls Board_Update() every poll, so it
     * naturally warms up the GT911 once it starts running. */
    if (!BootGUI_StartTouchTask()) {
        Serial.println("[MAC_SPLASH] WARNING: failed to start touch task");
    }

    /* Brief delay so the GT911 has time to produce its first valid
     * report before WaitForTapOrTimeout consumes events. The touch
     * task's own ~16 ms poll cadence handles most of the warm-up;
     * we just need a short head start. */
    Serial.println("[MAC_SPLASH] Touch task primed");
    delay(150);
}

bool WaitForTapOrTimeout(uint32_t ms)
{
    uint32_t start = millis();
    BootGUITouch t;

    /* Discard any spurious edge flagged before the window opened. */
    drain_touch_edges();

    bool tapped = false;
    while (millis() - start < ms) {
        if (BootGUI_PollTouch(&t)) {
            if (t.was_released) {
                Serial.printf("[MAC_SPLASH] Tap at (%d,%d) - opening settings\n",
                              t.x, t.y);
                tapped = true;
                break;
            }
        }
        delay(8);  /* ~120 Hz poll, plenty for human tap reaction */
    }

    /* Swallow any remaining edge so the settings UI starts fresh. */
    drain_touch_edges();
    return tapped;
}

void TransitionToEmulator()
{
    /* The last thing the user sees before Mac OS boots is the same splash
     * they saw at power-on. The emulator's first frame replaces it; free
     * the canvas now so that memory is available to the emulator. */
    paint_splash();
    PrebootUI::Release();
}

void ShowErrorOverlay(const char *msg)
{
    PrebootScreens::AlertView view = {};
    view.icon = PrebootUI::kIconStop;
    view.heading = "Basilisk II can't start.";
    view.body = msg ? msg : "Unknown error";
    view.dark_backdrop = true;
    paint_alert(&view);
}

void ShowSafeToPowerOff()
{
    PrebootScreens::AlertView view = {};
    view.icon = PrebootUI::kIconNote;
    view.body = "It is now safe to switch off your Macintosh.";
    view.dark_backdrop = true;
    paint_alert(&view);
}

} // namespace MacSplash
