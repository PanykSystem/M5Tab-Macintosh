/*
 * brightness.c - "Brightness" Control Strip module (sdev) for M5Tab-Macintosh
 *
 * Shows a sun icon in the Control Strip. Clicking it pops up a menu of
 * backlight levels and "Turn Off Display". The level is saved in the
 * Control Strip preferences and restored at startup.
 *
 * The module talks to the emulator through Gestalt('M5pw'), whose response
 * is the address of a C function:
 *     long Services(long selector, long argument, void *buffer);
 * (see src/basilisk/include/power_manager.h). On a Mac without it the
 * module declines to load.
 *
 * Built with Retro68 as a flat code resource; see build.sh.
 */
#include <ControlStrip.h>
#include <Gestalt.h>
#include <Icons.h>
#include <Menus.h>
#include <Resources.h>
#include <Retro68Runtime.h>

#define kServicesGestalt   'M5pw'
#define kVersion           0
#define kCapabilities      1
#define kGetBacklight      2
#define kSetBacklight      3
#define kSleepDisplay      5
#define kLog               6
#define kCapBacklight      1

#define kIconLow           256     /* sun icons: dim, medium, bright */
#define kIconCount         3
#define kArrowLeft         20      /* the pop-up arrow, after the 16-pixel icon */
#define kModuleWidth       26
#define kMenuID            -16200  /* private menu ID for the popup */

typedef long (*ServicesProc)(long selector, long argument, void *buffer);

static const unsigned char kLevels[] = {100, 85, 70, 55, 40, 25, 15, 5};
#define kLevelCount (sizeof(kLevels) / sizeof(kLevels[0]))
#define kOffItem (kLevelCount + 2)  /* after a separator */

typedef struct {
    ServicesProc services;
    Handle icons[kIconCount];
    MenuHandle menu;
    short shownIcon;        /* icon last drawn, -1 = none */
    short savedLevel;       /* level last written to preferences */
} Globals;

typedef struct {
    short version;
    short level;
} Prefs;

static const unsigned char kPrefsName[] = "\pM5Tab Brightness";
static const unsigned char kHelp[] =
    "\pBrightness\r\rClick to choose how bright the screen is, or to "
    "turn the display off until you touch it.";

static short IconForLevel(long level)
{
    if (level >= 70) return 2;
    if (level >= 30) return 1;
    return 0;
}

static long CurrentLevel(Globals *g)
{
    long level = g->services(kGetBacklight, 0, NULL);
    return level < 0 ? 100 : level;
}

/* The small right-pointing triangle other modules show for a pop-up menu. */
static void DrawArrow(const Rect *statusRect)
{
    short x = statusRect->left + kArrowLeft;
    short middle = (statusRect->top + statusRect->bottom) / 2;
    short i;
    PenNormal();
    ForeColor(blackColor);
    for (i = 0; i < 4; ++i) {
        MoveTo(x + i, middle - 3 + i);
        LineTo(x + i, middle + 3 - i);
    }
}

static void DrawModule(Globals *g, const Rect *statusRect)
{
    short icon = IconForLevel(CurrentLevel(g));
    Rect r;
    r.left = statusRect->left;
    r.right = r.left + 16;
    r.top = statusRect->top + ((statusRect->bottom - statusRect->top) - 16) / 2;
    r.bottom = r.top + 16;
    if (g->icons[icon] != NULL)
        PlotIconSuite(&r, kAlignNone, kTransformNone, g->icons[icon]);
    g->shownIcon = icon;
    DrawArrow(statusRect);
}

/* Redraw in place when the icon no longer matches the level, as Battery
   Monitor does: the module's width never changes. */
static void RedrawIfChanged(Globals *g, const Rect *statusRect)
{
    Rect icon;
    if (statusRect == NULL || g->shownIcon < 0 || IconForLevel(CurrentLevel(g)) == g->shownIcon)
        return;
    icon = *statusRect;
    icon.right = icon.left + 16;
    EraseRect(&icon);
    DrawModule(g, statusRect);
}

static MenuHandle BuildMenu(void)
{
    MenuHandle menu = NewMenu(kMenuID, "\pBrightness");
    unsigned short i;
    if (menu == NULL) return NULL;
    for (i = 0; i < kLevelCount; ++i) {
        Str255 item;
        unsigned char *p = item + 1;
        unsigned char v = kLevels[i];
        if (v >= 100) *p++ = '0' + v / 100;
        if (v >= 10) *p++ = '0' + (v / 10) % 10;
        *p++ = '0' + v % 10;
        *p++ = '%';
        item[0] = (unsigned char)(p - item - 1);
        AppendMenu(menu, "\px");        /* placeholder text avoids menu metacharacters */
        SetMenuItemText(menu, i + 1, item);
    }
    AppendMenu(menu, "\p(-");
    AppendMenu(menu, "\pTurn Off Display");
    return menu;
}

static void CheckLevel(Globals *g, long level)
{
    unsigned short i, best = 0;
    long bestDistance = 1000;
    for (i = 0; i < kLevelCount; ++i) {
        long d = level - kLevels[i];
        if (d < 0) d = -d;
        if (d < bestDistance) { bestDistance = d; best = i; }
    }
    for (i = 0; i < kLevelCount; ++i)
        SetItemMark(g->menu, i + 1, i == best ? sdevMenuItemMark : noMark);
}

static void LoadPreferences(Globals *g)
{
    Handle h = NULL;
    OSErr err = SBLoadPreferences((ConstStr255Param)kPrefsName, &h);
    g->services(kLog, err, (void *)"\pBrightness: load preferences");
    if (h != NULL) g->services(kLog, GetHandleSize(h), (void *)"\pBrightness: preference size");
    if (err == noErr && h != NULL) {
        if (GetHandleSize(h) >= (long)sizeof(Prefs)) {
            Prefs *p = (Prefs *)*h;
            if (p->version == 1 && p->level >= 5 && p->level <= 100) {
                g->services(kSetBacklight, p->level, NULL);
                g->savedLevel = p->level;
            }
        }
        DisposeHandle(h);
    }
}

static void SavePreferences(Globals *g)
{
    Handle h = NewHandle(sizeof(Prefs));
    if (h == NULL) return;
    ((Prefs *)*h)->version = 1;
    ((Prefs *)*h)->level = (short)CurrentLevel(g);
    {
        OSErr err = SBSavePreferences((ConstStr255Param)kPrefsName, h);
        g->services(kLog, err, (void *)"\pBrightness: save preferences");
        if (err == noErr)
            g->savedLevel = ((Prefs *)*h)->level;
    }
    DisposeHandle(h);
}

static long InitModule(void)
{
    long response = 0;
    ServicesProc services;
    Handle globals;
    Globals *g;
    short i;

    if (Gestalt(kServicesGestalt, &response) != noErr || response == 0)
        return -1;
    services = (ServicesProc)response;
    if (services(kVersion, 0, NULL) < 1 || !(services(kCapabilities, 0, NULL) & kCapBacklight))
        return -1;

    globals = NewHandleSysClear(sizeof(Globals));
    if (globals == NULL)
        return -1;
    HLock(globals);
    g = (Globals *)*globals;
    g->services = services;
    g->shownIcon = -1;
    g->savedLevel = -1;
    for (i = 0; i < kIconCount; ++i)
        SBGetDetachIconSuite(&g->icons[i], kIconLow + i, svAllSmallData);
    g->menu = BuildMenu();
    LoadPreferences(g);
    HUnlock(globals);
    return (long)globals;
}

static void CloseModule(Handle globals)
{
    Globals *g = (Globals *)*globals;
    short i;
    for (i = 0; i < kIconCount; ++i)
        if (g->icons[i] != NULL) DisposeIconSuite(g->icons[i], true);
    if (g->menu != NULL) DisposeMenu(g->menu);
    DisposeHandle(globals);
}

static long MouseClick(Globals *g, const Rect *statusRect)
{
    long item;
    long result = 0;
    if (g->menu == NULL) return 0;
    CheckLevel(g, CurrentLevel(g));
    item = SBTrackPopupMenu(statusRect, g->menu);
    if (item >= 1 && item <= (long)kLevelCount) {
        g->services(kSetBacklight, kLevels[item - 1], NULL);
        RedrawIfChanged(g, statusRect);
        if (CurrentLevel(g) != g->savedLevel)
            result |= 1L << sdevNeedToSave;
    } else if (item == kOffItem) {
        g->services(kSleepDisplay, 0, NULL);
    }
    return result;
}

pascal long ModuleMain(long message, long params, Rect *statusRect, GrafPtr statusPort)
{
    Handle globals = (Handle)params;
    Globals *g;
    long result = 0;
    SignedByte state;

    RETRO68_RELOCATE();
    (void)statusPort;

    if (message == sdevInitModule)
        return InitModule();
    if (globals == NULL)
        return 0;

    state = HGetState(globals);
    HLock(globals);
    g = (Globals *)*globals;

    switch (message) {
    case sdevCloseModule:
        CloseModule(globals);
        return 0;
    case sdevFeatures:
        /* Track the click ourselves, so the menu opens on mouse-down. */
        result = (1L << sdevWantMouseClicks) | (1L << sdevDontAutoTrack) | (1L << sdevHasCustomHelp);
        break;
    case sdevGetDisplayWidth:
        result = kModuleWidth;
        break;
    case sdevPeriodicTickle:
        /* The level can change from outside (serial console, startup). */
        RedrawIfChanged(g, statusRect);
        break;
    case sdevDrawStatus:
        DrawModule(g, statusRect);
        break;
    case sdevMouseClick:
        result = MouseClick(g, statusRect);
        break;
    case sdevSaveSettings:
        SavePreferences(g);
        break;
    case sdevShowBalloonHelp:
        SBShowHelpString(statusRect, (StringPtr)kHelp);
        break;
    }

    HSetState(globals, state);
    return result;
}
