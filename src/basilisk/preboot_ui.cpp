/*
 *  preboot_ui.cpp - Mac OS 8 "Platinum" drawing kit for the preboot screens
 *
 *  BasiliskII ESP32 Port
 *
 *  See preboot_ui.h. Control sprites and frame profiles below were measured
 *  pixel by pixel from Mac OS 8.1 running in this emulator, so the preboot
 *  screens match the system the user boots into. Icons and the desktop
 *  texture are original artwork in the Mac OS 8 palette.
 */

#include "preboot_ui.h"

#include <stdlib.h>
#include <string.h>

#include "board_display.h"
#include "panel_surface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "charcoal_font_data.h"   /* generated from assets/fonts/charcoal12.bdf */
#include "asset_bg_tile.h"        /* generated from assets/BgTile.png */

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace PrebootUI {

// ============================================================================
// Canvas and palette
// ============================================================================

static uint8_t *s_canvas = nullptr;   /* what is being drawn */
static uint8_t *s_shown = nullptr;    /* what the panel shows (last Present) */
static int s_w = 0, s_h = 0;
static bool s_invalid = true;
static SemaphoreHandle_t s_lock = nullptr;

static uint16_t s_palette[256];
static uint32_t s_pairs[256];         /* RGB565 in both halfwords for 2x tiles */
static int s_palette_count = 0;

static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((((r * 31 + 127) / 255) << 11) |
                      (((g * 63 + 127) / 255) << 5) |
                      ((b * 31 + 127) / 255));
}

/* Order matches enum Color. Grays are the exact Platinum shades the emulated
 * Mac OS 8 shows on this panel. */
static const uint16_t kFixedPalette[kFirstDynamic] = {
    rgb(0x00, 0x00, 0x00), rgb(0xFF, 0xFF, 0xFF),
    rgb(0x3A, 0x3D, 0x3A), rgb(0x63, 0x65, 0x63), rgb(0x73, 0x75, 0x73),
    rgb(0x94, 0x96, 0x94), rgb(0xA5, 0xA6, 0xA5), rgb(0xB5, 0xB2, 0xB5),
    rgb(0xC5, 0xC2, 0xC5), rgb(0xCE, 0xCE, 0xCE), rgb(0xDE, 0xDB, 0xDE),
    rgb(0xE6, 0xE7, 0xE6), rgb(0xF7, 0xF3, 0xF7),
    rgb(0xDE, 0xDB, 0xFF), rgb(0xB5, 0xB2, 0xFF), rgb(0x84, 0x86, 0xDE),
    rgb(0x52, 0x55, 0xB5),
    rgb(0xB5, 0xB2, 0xFF), rgb(0xB5, 0xB2, 0xDE), rgb(0x84, 0xB2, 0xDE),
    rgb(0xB5, 0xDB, 0xFF),
    rgb(0xE6, 0x00, 0x00), rgb(0x94, 0x00, 0x00), rgb(0xFF, 0x86, 0x00),
    rgb(0xFF, 0xDB, 0x00), rgb(0x00, 0xA6, 0x00), rgb(0x00, 0x65, 0x00),
    rgb(0x00, 0x55, 0xD6), rgb(0x84, 0x30, 0xB5),
};

static void setEntry(int index, uint16_t color)
{
    s_palette[index] = color;
    s_pairs[index] = ((uint32_t)color << 16) | color;
}

static void resetPalette(void)
{
    for (int i = 0; i < 256; ++i) setEntry(i, 0);
    for (int i = 0; i < kFirstDynamic; ++i) setEntry(i, kFixedPalette[i]);
    s_palette_count = kFirstDynamic;
}

uint8_t ColorFor(uint16_t color)
{
    for (int i = 0; i < s_palette_count; ++i)
        if (s_palette[i] == color) return (uint8_t)i;
    if (s_palette_count < 256) {
        setEntry(s_palette_count, color);
        return (uint8_t)s_palette_count++;
    }
    // Full: nearest existing entry by squared RGB565 component distance.
    int best = 0;
    long best_d = -1;
    for (int i = 0; i < 256; ++i) {
        const int dr = ((color >> 11) & 31) - ((s_palette[i] >> 11) & 31);
        const int dg = ((color >> 5) & 63) - ((s_palette[i] >> 5) & 63);
        const int db = (color & 31) - (s_palette[i] & 31);
        const long d = 4L * dr * dr + 1L * dg * dg + 4L * db * db;
        if (best_d < 0 || d < best_d) { best = i; best_d = d; }
    }
    return (uint8_t)best;
}

static void *allocCanvas(size_t bytes)
{
#ifdef ESP_PLATFORM
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
#endif
    return malloc(bytes);
}

static void freeCanvas(void *p)
{
#ifdef ESP_PLATFORM
    heap_caps_free(p);
#else
    free(p);
#endif
}

bool Begin(void)
{
    if (s_canvas) return true;
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return false;
    }
    const int w = BoardDisplay_Width() / kScale;
    const int h = BoardDisplay_Height() / kScale;
    uint8_t *canvas = (uint8_t *)allocCanvas((size_t)w * h);
    uint8_t *shown = (uint8_t *)allocCanvas((size_t)w * h);
    if (!canvas || !shown) {
        if (canvas) freeCanvas(canvas);
        if (shown) freeCanvas(shown);
        return false;
    }
    memset(canvas, kBlack, (size_t)w * h);
    memset(shown, kBlack, (size_t)w * h);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    resetPalette();
    s_w = w;
    s_h = h;
    s_shown = shown;
    s_canvas = canvas;
    s_invalid = true;
    xSemaphoreGive(s_lock);
    return true;
}

void Release(void)
{
    if (!s_canvas) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    freeCanvas(s_canvas);
    freeCanvas(s_shown);
    s_canvas = s_shown = nullptr;
    s_w = s_h = 0;
    xSemaphoreGive(s_lock);
}

bool Active(void) { return s_canvas != nullptr; }
int Width(void) { return s_w; }
int Height(void) { return s_h; }
void InvalidateAll(void) { s_invalid = true; }

void Present(void)
{
    if (!s_canvas) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // Find the bounding box of logical pixels that differ from the panel.
    int top = s_h, bottom = -1, left = s_w, right = -1;
    for (int y = 0; y < s_h; ++y) {
        const uint8_t *a = s_canvas + (size_t)y * s_w;
        const uint8_t *b = s_shown + (size_t)y * s_w;
        if (!s_invalid && memcmp(a, b, s_w) == 0) continue;
        int l = 0, r = s_w - 1;
        if (!s_invalid) {
            while (a[l] == b[l]) ++l;
            while (a[r] == b[r]) --r;
        }
        if (y < top) top = y;
        bottom = y;
        if (l < left) left = l;
        if (r > right) right = r;
    }
    if (bottom >= 0) {
        const int w = right - left + 1, h = bottom - top + 1;
        bool pushed = false;
        if (BoardDisplay_BeginTiles()) {
            pushed = BoardDisplay_PushIndexedTile(left * kScale, top * kScale, w * kScale,
                                                  h * kScale, s_canvas + (size_t)top * s_w,
                                                  (uint32_t)s_w, 8, left, s_pairs);
            pushed = BoardDisplay_EndTiles() && pushed;
        }
        if (pushed) {
            for (int y = top; y <= bottom; ++y)
                memcpy(s_shown + (size_t)y * s_w + left, s_canvas + (size_t)y * s_w + left, w);
            s_invalid = false;
        }
    }
    xSemaphoreGive(s_lock);
}

bool CaptureFrame(uint8_t *pixels, size_t capacity, uint16_t *palette,
                  uint16_t *width, uint16_t *height)
{
    if (!s_lock || !pixels || !palette || !width || !height) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = s_canvas && capacity >= (size_t)s_w * s_h;
    if (ok) {
        // The shown copy is exactly what the last Present() put on the panel,
        // never a half-drawn frame.
        memcpy(pixels, s_shown, (size_t)s_w * s_h);
        memcpy(palette, s_palette, sizeof(s_palette));
        *width = (uint16_t)s_w;
        *height = (uint16_t)s_h;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

bool VerifyPanel(uint32_t *checked, uint32_t *mismatched, int *first_x, int *first_y)
{
    if (!s_lock || !checked || !mismatched || !first_x || !first_y) return false;
    MiniGfx &gfx = BoardDisplay_Gfx();
    const uint16_t *panel = gfx.scanoutFb();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = s_canvas && panel;
    *checked = *mismatched = 0;
    *first_x = *first_y = -1;
    for (int y = 0; ok && y < s_h * kScale; ++y)
        for (int x = 0; x < s_w * kScale; ++x) {
            const uint16_t want = s_palette[s_shown[(size_t)(y / kScale) * s_w + x / kScale]];
            ++*checked;
            if (readPanelPixel(panel, gfx.panelW(), gfx.panelH(), gfx.flip180(), x, y) != want) {
                if (!*mismatched) { *first_x = x; *first_y = y; }
                ++*mismatched;
            }
        }
    xSemaphoreGive(s_lock);
    return ok;
}

// ============================================================================
// Primitives
// ============================================================================

void Fill(int x, int y, int w, int h, uint8_t color)
{
    if (!s_canvas) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_w) w = s_w - x;
    if (y + h > s_h) h = s_h - y;
    if (w <= 0 || h <= 0) return;
    for (int row = y; row < y + h; ++row) memset(s_canvas + (size_t)row * s_w + x, color, w);
}

void Fill(const Rect &r, uint8_t color) { Fill(r.x, r.y, r.w, r.h, color); }
void HLine(int x, int y, int w, uint8_t color) { Fill(x, y, w, 1, color); }
void VLine(int x, int y, int h, uint8_t color) { Fill(x, y, 1, h, color); }

void Pixel(int x, int y, uint8_t color)
{
    if (s_canvas && x >= 0 && y >= 0 && x < s_w && y < s_h) s_canvas[(size_t)y * s_w + x] = color;
}

void FrameRect(const Rect &r, uint8_t color)
{
    HLine(r.x, r.y, r.w, color);
    HLine(r.x, r.y + r.h - 1, r.w, color);
    VLine(r.x, r.y, r.h, color);
    VLine(r.x + r.w - 1, r.y, r.h, color);
}

void Sprite(int x, int y, int w, int h, const uint8_t *indices)
{
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col) {
            const uint8_t c = indices[row * w + col];
            if (c != 0xFF) Pixel(x + col, y + row, c);
        }
}

void Image565(int x, int y, int w, int h, const uint16_t *pixels,
              const uint8_t *mask, int mask_row_bytes)
{
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col) {
            if (mask && !(mask[row * mask_row_bytes + (col >> 3)] & (0x80 >> (col & 7)))) continue;
            Pixel(x + col, y + row, ColorFor(pixels[row * w + col]));
        }
}

void Tile565(const Rect &r, const uint16_t *tile, int tw, int th)
{
    // Map the tile's few colors once, then fill row by row.
    static uint8_t mapped[32 * 32];
    if (tw <= 0 || th <= 0 || tw * th > (int)sizeof(mapped)) return;
    for (int i = 0; i < tw * th; ++i) mapped[i] = ColorFor(tile[i]);
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x)
            Pixel(x, y, mapped[(y % th) * tw + (x % tw)]);
}

/* Character-coded sprites keep measured artwork readable in source. */
static uint8_t codeColor(char c)
{
    switch (c) {
    case 'K': return kBlack;
    case 'W': return kWhite;
    case '3': return kGray3A;
    case '6': return kGray63;
    case '7': return kGray73;
    case '9': return kGray94;
    case 'A': return kGrayA5;
    case 'B': return kGrayB5;
    case 'C': return kGrayC5;
    case 'c': return kGrayCE;
    case 'D': return kGrayDE;
    case 'E': return kGrayE6;
    case 'F': return kGrayF7;
    case 'l': return kAccentLight;
    case 'a': return kAccent;
    case 's': return kAccentShadow;
    case 'd': return kAccentDark;
    case 'r': return kRed;
    case 'R': return kDarkRed;
    case 'o': return kOrange;
    case 'y': return kYellow;
    case 'g': return kGreen;
    case 'G': return kDarkGreen;
    case 'b': return kBlue;
    case 'p': return kPurple;
    default: return 0xFF;   /* transparent */
    }
}

template <size_t N>
static void CodedSprite(int x, int y, const char *const (&rows)[N], bool inverted = false)
{
    for (size_t row = 0; row < N; ++row)
        for (int col = 0; rows[row][col]; ++col) {
            uint8_t c = codeColor(rows[row][col]);
            if (inverted && c == kBlack) c = kWhite;
            if (c != 0xFF) Pixel(x + col, y + (int)row, c);
        }
}

// ============================================================================
// Text
// ============================================================================

static const CharcoalGlyph &glyphFor(char ch)
{
    unsigned c = (unsigned char)ch;
    if (c < CHARCOAL_FIRST_CP || c > CHARCOAL_LAST_CP) c = '?';
    return CHARCOAL_GLYPH_TABLE[c - CHARCOAL_FIRST_CP];
}

int TextWidth(const char *s)
{
    int w = 0;
    for (; s && *s; ++s) w += glyphFor(*s).advance;
    return w;
}

void Text(int x, int baseline, const char *s, uint8_t color)
{
    for (; s && *s; ++s) {
        const CharcoalGlyph &g = glyphFor(*s);
        const int row_bytes = (g.w + 7) / 8;
        const int top = baseline - g.bbx_y - g.h;
        for (int gy = 0; gy < g.h; ++gy)
            for (int gx = 0; gx < g.w; ++gx)
                if (CHARCOAL_GLYPH_BLOB[g.offset + gy * row_bytes + (gx >> 3)] & (0x80 >> (gx & 7)))
                    Pixel(x + g.bbx_x + gx, top + gy, color);
        x += g.advance;
    }
}

void FitText(const char *s, int max_width, char *out, size_t capacity)
{
    if (!out || capacity == 0) return;
    out[0] = 0;
    if (!s) return;
    size_t n = strlen(s);
    if (n < capacity && TextWidth(s) <= max_width) {
        memcpy(out, s, n + 1);
        return;
    }
    const int ellipsis = TextWidth("...");
    int w = 0;
    size_t i = 0;
    while (i < n && i + 4 < capacity) {
        const int adv = glyphFor(s[i]).advance;
        if (w + adv + ellipsis > max_width) break;
        w += adv;
        out[i] = s[i];
        ++i;
    }
    while (i > 0 && out[i - 1] == ' ') --i;
    memcpy(out + i, "...", 4);
}

void TextIn(const Rect &r, const char *s, uint8_t color, Align align)
{
    char fitted[96];
    FitText(s, r.w, fitted, sizeof(fitted));
    int x = r.x;
    const int w = TextWidth(fitted);
    if (align == kCenter) x = r.x + (r.w - w) / 2;
    if (align == kRight) x = r.x + r.w - w;
    Text(x, BaselineIn(r.y, r.h), fitted, color);
}

// ============================================================================
// Desktop, menu bar and screen corners
// ============================================================================

void Desktop(const Rect &r)
{
    // Original speckled periwinkle in Mac OS 8's default desktop colors. A
    // position hash (not a repeating tile) avoids visible seams.
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) {
            uint32_t v = (uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u;
            v ^= v >> 15;
            v *= 0x2C1B3C6Du;
            v ^= v >> 12;
            const uint32_t pick = v & 63;
            uint8_t c = kDesktop;
            if (pick < 12) c = kDesktopSpeck1;
            else if (pick < 16) c = kDesktopSpeck2;
            else if (pick < 18) c = kDesktopSpeck3;
            Pixel(x, y, c);
        }
}

void SplashBackground(void)
{
    // assets/BgTile.png (the classic 50% gray) at one tile pixel per logical
    // pixel, so it shows as 2x2 blocks like the Mac screen.
    Tile565({0, 0, s_w, s_h}, BG_TILE_PIXELS, BG_TILE_W, BG_TILE_H);
}

static const char *const kCornerTL[7] = {
    "KKKKK7C",
    "KKK7CWW",
    "KK7EWEE",
    "K7EWEEE",
    "KCWEEEE",
    "7WEEEEE",
    "CWEEEEE",
};
static const char *const kCornerTR[8] = {
    "EC7KKKKK",
    "EDcA7KKK",
    "EEEcA7KK",
    "EEEEcA7K",
    "EEEEEcAK",
    "EEEEEEC7",
    "EEEEEEc9",
    "EEEEEEDA",
};

/* Rainbow Apple, original 12x14 artwork in the six classic stripes. */
static const char *const kApple[14] = {
    "       GG   ",
    "      GgG   ",
    "      GG    ",
    "  gggg ggg  ",
    " gggggggggg ",
    "yyyyyyyyyy  ",
    "yyyyyyyyy   ",
    "ooooooooo   ",
    "rrrrrrrrrr  ",
    "rrrrrrrrrrr ",
    " pppppppppp ",
    " pppppppppp ",
    "  bbbbbbbb  ",
    "   bbb bbb  ",
};

/* 16x16 compact Macintosh for the application menu. */
static const char *const kMacSmall[16] = {
    "  KKKKKKKKKKK   ",
    "  KWWWWWWWWWDK  ",
    "  KWKKKKKKKWDK  ",
    "  KWKaaaaaKWDK  ",
    "  KWKaaaaaKWDK  ",
    "  KWKaaaaaKWDK  ",
    "  KWKaaaaaKWDK  ",
    "  KWKKKKKKKWDK  ",
    "  KWWWWWWWWWDK  ",
    "  KWWWWWWWWWDK  ",
    "  KWWWgyobWWDK  ",
    "  KWWWWWWWWWDK  ",
    "  KDDDDDDDDDDK  ",
    "  KKKKKKKKKKKK  ",
    "  KWWWWWWWWWDK  ",
    "  KKKKKKKKKKKK  ",
};

void MenuBar(const char *clock_text)
{
    HLine(0, 0, s_w, kWhite);
    Fill(0, 1, s_w, 17, kGrayE6);
    HLine(0, 18, s_w, kGrayB5);
    HLine(0, 19, s_w, kBlack);
    CodedSprite(15, 3, kApple);
    static const char *const titles[] = {"File", "Edit", "View", "Special"};
    int x = 43;
    for (const char *t : titles) {
        Text(x, 14, t, kBlack);
        x += TextWidth(t) + 13;
    }
    CodedSprite(s_w - 28, 2, kMacSmall);
    if (clock_text && clock_text[0])
        Text(s_w - 46 - TextWidth(clock_text), 14, clock_text, kBlack);
}

void ScreenCorners(void)
{
    CodedSprite(0, 0, kCornerTL);
    CodedSprite(s_w - 8, 0, kCornerTR);
    // Bottom corners: the same curve in black, mirrored, over any background.
    for (int row = 0; row < 7; ++row)
        for (int col = 0; kCornerTL[row][col]; ++col) {
            const char c = kCornerTL[row][col];
            if (c != 'K' && c != '7') continue;
            const uint8_t color = c == 'K' ? kBlack : kGray73;
            Pixel(col, s_h - 1 - row, color);
            Pixel(s_w - 1 - col, s_h - 1 - row, color);
        }
}

// ============================================================================
// Windows
// ============================================================================

Rect WindowContent(const Rect &f, WindowStyle style)
{
    (void)style;
    return {f.x + 6, f.y + kTitleBarHeight + 1, f.w - 12, f.h - kTitleBarHeight - 7};
}

static void pinstripes(int x0, int x1, int y)
{
    // Six raised lines: each white line sits one pixel left of its shadow.
    for (int i = 0; i < 6; ++i) {
        if (x1 - x0 < 2) return;
        HLine(x0 - 1, y + 2 * i, x1 - x0, kWhite);
        HLine(x0, y + 2 * i + 1, x1 - x0, kGray94);
    }
}

Rect Window(const Rect &f, const char *title, WindowStyle style)
{
    const int x0 = f.x, y0 = f.y, x1 = f.x + f.w - 1, y1 = f.y + f.h - 1;
    // Outline plus a one-pixel drop shadow on the right and bottom.
    FrameRect(f, kBlack);
    VLine(x1 + 1, y0 + 1, f.h, kBlack);
    HLine(x0 + 1, y1 + 1, f.w, kBlack);
    // Raised outer bevel: white top/left, B5 bottom/right, two rows of face.
    Fill(x0 + 1, y0 + 1, f.w - 2, f.h - 2, kGrayDE);
    HLine(x0 + 1, y0 + 1, f.w - 2, kWhite);
    VLine(x0 + 1, y0 + 1, f.h - 2, kWhite);
    HLine(x0 + 1, y1 - 1, f.w - 2, kGrayB5);
    VLine(x1 - 1, y0 + 1, f.h - 2, kGrayB5);
    // Inset content: B5 above/left, white below/right, then a black line.
    const Rect c = WindowContent(f, style);
    HLine(c.x - 2, c.y - 2, c.w + 3, kGrayB5);
    VLine(c.x - 2, c.y - 2, c.h + 4, kGrayB5);
    HLine(c.x - 1, c.y + c.h + 1, c.w + 3, kWhite);
    VLine(c.x + c.w + 1, c.y - 2, c.h + 4, kWhite);
    FrameRect({c.x - 1, c.y - 1, c.w + 2, c.h + 2}, kBlack);
    Fill(c, kGrayE6);

    // Title bar: pinstripes from rows y0+3..y0+14, broken around the title.
    const int stripe_l = x0 + 9, stripe_r = x1 - 8;
    if (style == kDialogWindow && title && title[0]) {
        char fitted[96];
        FitText(title, f.w - 60, fitted, sizeof(fitted));
        const int tw = TextWidth(fitted);
        const int tx = x0 + (f.w - tw) / 2;
        pinstripes(stripe_l, tx - 4, y0 + 3);
        pinstripes(tx + tw + 4, stripe_r, y0 + 3);
        Text(tx, y0 + 14, fitted, kBlack);
    } else {
        pinstripes(stripe_l, stripe_r, y0 + 3);
    }
    return c;
}

// ============================================================================
// Push buttons
// ============================================================================

/* One row of a horizontally stretchable control: fixed left/right edge
 * pixels around a repeated fill. ' ' is transparent. */
struct EdgeRow { const char *left; char fill; const char *right; };

/* Normal Platinum push button, measured from a 20-pixel-high button.
 * Rows 0-3 and the last four are fixed; kButtonMiddle repeats. */
static const EdgeRow kButtonTop[4] = {
    {"  3K", 'K', "K3  "},
    {" KcE", 'E', "EcK "},
    {"3cWW", 'W', "WEc3"},
    {"KEWW", 'E', "EC9K"},
};
static const EdgeRow kButtonMiddle = {"KEWE", 'E', "EC9K"};
static const EdgeRow kButtonBottom[4] = {
    {"KEWE", 'E', "CC9K"},
    {"3cEC", 'C', "C993"},
    {" Kc9", '9', "99K "},
    {"  3K", 'K', "K3  "},
};
/* The default-button ring, 3 pixels outside the button on every side. The
 * button's own transparent corners show the ring's shading pixels. */
static const EdgeRow kRingTop[5] = {
    {"   3", 'K', "3   "},
    {"  KE", 'E', "DK  "},
    {" KEE", 'C', "CcK "},
    {"3EEC9", ' ', "9CCA3"},
    {"KEC9", ' ', "9C9K"},
};
static const EdgeRow kRingMiddle = {"KEC", ' ', "C9K"};
static const EdgeRow kRingBottom[5] = {
    {"KEC9", ' ', "9C9K"},
    {"3DCC9", ' ', "9CA93"},
    {" KcC", 'C', "A9K "},
    {"  KA", '9', "9K  "},
    {"   3", 'K', "3   "},
};

static uint8_t mapState(uint8_t c, ButtonState state)
{
    if (state == kButtonPressed) {
        switch (c) {
        case kWhite: return kGray3A;
        case kGrayE6: return kGray73;
        case kGrayCE: return kGray63;
        case kGrayC5: return kGray94;
        case kGray94: return kGrayA5;
        default: return c;
        }
    }
    if (state == kButtonDisabled) {
        switch (c) {
        case kBlack: return kGray94;
        case kGray3A: return kGrayB5;
        case kGray94: return kGrayC5;
        default: return c;
        }
    }
    return c;
}

static void edgeRow(int x, int y, int w, const EdgeRow &row, ButtonState state)
{
    const int nl = (int)strlen(row.left), nr = (int)strlen(row.right);
    for (int i = 0; i < nl; ++i) {
        const uint8_t c = codeColor(row.left[i]);
        if (c != 0xFF) Pixel(x + i, y, mapState(c, state));
    }
    const uint8_t fill = codeColor(row.fill);
    if (fill != 0xFF) HLine(x + nl, y, w - nl - nr, mapState(fill, state));
    for (int i = 0; i < nr; ++i) {
        const uint8_t c = codeColor(row.right[i]);
        if (c != 0xFF) Pixel(x + w - nr + i, y, mapState(c, state));
    }
}

static void stretch(const Rect &r, const EdgeRow *top, int ntop, const EdgeRow &middle,
                    const EdgeRow *bottom, int nbottom, ButtonState state)
{
    for (int i = 0; i < ntop; ++i) edgeRow(r.x, r.y + i, r.w, top[i], state);
    for (int y = r.y + ntop; y < r.y + r.h - nbottom; ++y) edgeRow(r.x, y, r.w, middle, state);
    for (int i = 0; i < nbottom; ++i) edgeRow(r.x, r.y + r.h - nbottom + i, r.w, bottom[i], state);
}

void Button(const Rect &r, const char *label, ButtonState state, bool is_default)
{
    if (is_default)
        stretch({r.x - 3, r.y - 3, r.w + 6, r.h + 6}, kRingTop, 5, kRingMiddle,
                kRingBottom, 5, state == kButtonDisabled ? kButtonDisabled : kButtonNormal);
    stretch(r, kButtonTop, 4, kButtonMiddle, kButtonBottom, 4, state);
    const uint8_t fg = state == kButtonPressed ? kWhite
                     : state == kButtonDisabled ? kGray94 : kBlack;
    TextIn({r.x + 4, r.y, r.w - 8, r.h}, label, fg, kCenter);
}

void KeyCap(const Rect &r, const char *label, bool pressed, bool latched)
{
    stretch(r, kButtonTop, 4, kButtonMiddle, kButtonBottom, 4,
            pressed || latched ? kButtonPressed : kButtonNormal);
    TextIn({r.x + 2, r.y, r.w - 4, r.h}, label, pressed || latched ? kWhite : kBlack, kCenter);
}

// ============================================================================
// Checkboxes and radio buttons (measured 12x12 Platinum controls)
// ============================================================================

static const char *const kCheckOff[12] = {
    "KKKKKKKKKKKK",
    "KWWWWWWWWWEK",
    "KWEEEEEEEEAK",
    "KWEEEEEEEEAK",
    "KWEEEEEEEEAK",
    "KWEEEEEEEEAK",
    "KWEEEEEEEEAK",
    "KWEEEEEEEEAK",
    "KWEEEEEEEEAK",
    "KWEEEEEEEEAK",
    "KEAAAAAAAAAK",
    "KKKKKKKKKKKK",
};
static const char *const kCheckOn[12] = {
    "KKKKKKKKKKKK  ",
    "KWWWWWWWWWEKK ",
    "KWEEEEEEEEKK9C",
    "KWEEEEEEEKKKC ",
    "KWEEEEEEKK7K  ",
    "KWKKEEEKK99K  ",
    "KWEKKEKK9CAK  ",
    "KWECKKK9CEAK  ",
    "KWEECK9CEEAK  ",
    "KWEEE9CEEEAK  ",
    "KEAAAAAAAAAK  ",
    "KKKKKKKKKKKK  ",
};
static const char *const kRadioOff[12] = {
    "    6KK6    ",
    "  K7EEEc6K  ",
    " KcEFWWWEAK ",
    " 7EFWWFFEc6 ",
    "6EFWWFFEEcA6",
    "KEWWFFEEccAK",
    "KEWFFEEccCAK",
    "6cWFEEccCCA6",
    " 7EEEccCCA6 ",
    " KAcccCCAAK ",
    "  K7AAAA6K  ",
    "    6KK6    ",
};
static const char *const kRadioOn[12] = {
    "    3KK3    ",
    "  3K67776K  ",
    " 367999AABK ",
    " K79KKKKBB6 ",
    "369KKKKKKBc6",
    "K79KKKKKKccK",
    "K79KKKKKKcEK",
    "37AKKKKKKEW6",
    " 3ABKKKKEW6 ",
    " 3BBBccEWWK ",
    "  36ccEW6K  ",
    "    6KK6    ",
};

/* A pressed control darkens its face the way Mac OS 8 does while tracking. */
template <size_t N>
static void controlSprite(int x, int y, const char *const (&rows)[N], bool pressed)
{
    for (size_t row = 0; row < N; ++row)
        for (int col = 0; rows[row][col]; ++col) {
            uint8_t c = codeColor(rows[row][col]);
            if (c == 0xFF) continue;
            if (pressed) {
                if (c == kWhite || c == kGrayF7) c = kGray94;
                else if (c == kGrayE6 || c == kGrayCE) c = kGray73;
                else if (c == kGrayC5 || c == kGrayA5) c = kGray63;
            }
            Pixel(x + col, y + (int)row, c);
        }
}

void Checkbox(int x, int y, bool checked, bool pressed)
{
    if (checked) controlSprite(x, y, kCheckOn, pressed);
    else controlSprite(x, y, kCheckOff, pressed);
}

void Radio(int x, int y, bool selected, bool pressed)
{
    if (selected) controlSprite(x, y, kRadioOn, pressed);
    else controlSprite(x, y, kRadioOff, pressed);
}

Rect LabeledCheckbox(int x, int y, const char *label, bool checked, bool pressed)
{
    Checkbox(x, y + 2, checked, pressed);
    Text(x + 18, y + 13, label, kBlack);
    return {x - 4, y - 6, 18 + TextWidth(label) + 10, 28};
}

Rect LabeledRadio(int x, int y, const char *label, bool selected, bool pressed)
{
    Radio(x, y + 2, selected, pressed);
    Text(x + 18, y + 13, label, kBlack);
    return {x - 4, y - 6, 18 + TextWidth(label) + 10, 28};
}

// ============================================================================
// Group boxes, edit fields, lists and scroll bars
// ============================================================================

void GroupBox(const Rect &r, const char *title)
{
    // Etched frame: a dark line with a white line just inside/below it.
    const Rect outer = {r.x, r.y + 6, r.w - 1, r.h - 7};
    FrameRect({outer.x + 1, outer.y + 1, outer.w, outer.h}, kWhite);
    FrameRect(outer, kGray94);
    if (title && title[0]) {
        const int tw = TextWidth(title);
        Fill(r.x + 8, r.y, tw + 6, 14, kGrayE6);
        Text(r.x + 11, r.y + 11, title, kBlack);
    }
}

void TextField(const Rect &r, const char *text, bool placeholder, bool focused)
{
    // Sunken: gray above/left, white below/right, around a black frame.
    HLine(r.x - 1, r.y - 1, r.w + 1, kGray94);
    VLine(r.x - 1, r.y - 1, r.h + 1, kGray94);
    HLine(r.x - 1, r.y + r.h, r.w + 2, kWhite);
    VLine(r.x + r.w, r.y - 1, r.h + 2, kWhite);
    FrameRect(r, kBlack);
    Fill(r.x + 1, r.y + 1, r.w - 2, r.h - 2, kWhite);
    HLine(r.x + 1, r.y + 1, r.w - 2, kGrayE6);
    VLine(r.x + 1, r.y + 1, r.h - 2, kGrayE6);
    char fitted[96];
    FitText(text, r.w - 14, fitted, sizeof(fitted));
    const int baseline = BaselineIn(r.y, r.h);
    Text(r.x + 5, baseline, fitted, placeholder ? kGray94 : kBlack);
    if (focused) VLine(r.x + 6 + TextWidth(placeholder ? "" : fitted), baseline - 11, 13, kBlack);
}

Rect ListFrame(const Rect &r)
{
    HLine(r.x - 1, r.y - 1, r.w + 1, kGray94);
    VLine(r.x - 1, r.y - 1, r.h + 1, kGray94);
    HLine(r.x - 1, r.y + r.h, r.w + 2, kWhite);
    VLine(r.x + r.w, r.y - 1, r.h + 2, kWhite);
    FrameRect(r, kBlack);
    const Rect inner = r.inset(1);
    Fill(inner, kWhite);
    return inner;
}

static const char *const kScrollUp[16] = {
    "KKKKKKKKKKKKKKKK",
    "KWWWWWWWWWWWWWEK",
    "KWEEEEEEEEEEEEcK",
    "KWEEEEEEEEEEEEcK",
    "KWEEEEEEEEEEEEcK",
    "KWEEEEEEEEEEEEcK",
    "KWEEEEEKKEEEEEcK",
    "KWEEEEKKKKEEEEcK",
    "KWEEEKKKKKKEEEcK",
    "KWEEKKKKKKKKEEcK",
    "KWEEEEEEEEEEEEcK",
    "KWEEEEEEEEEEEEcK",
    "KWEEEEEEEEEEEEcK",
    "KWEEEEEEEEEEEEcK",
    "KEcccccccccccccK",
    "KKKKKKKKKKKKKKKK",
};
static const char *const kThumb[17] = {
    "KKKKKKKKKKKKKKKK",
    "KFllllllllllllaK",
    "KlaaaaaaaaaaaasK",
    "KlaaaaaaaaaaaasK",
    "KlaaFllllllaaasK",
    "KlaaadddddddaasK",
    "KlaaFllllllaaasK",
    "KlaaadddddddaasK",
    "KlaaFllllllaaasK",
    "KlaaadddddddaasK",
    "KlaaFllllllaaasK",
    "KlaaadddddddaasK",
    "KlaaaaaaaaaaaasK",
    "KlaaaaaaaaaaaasK",
    "KlaaaaaaaaaaaasK",
    "KasssssssssssssK",
    "KKKKKKKKKKKKKKKK",
};

static void arrowBox(int x, int y, bool up, bool active, bool pressed)
{
    for (int row = 0; row < 16; ++row)
        for (int col = 0; col < 16; ++col) {
            // The down arrow box is the up box with its arrow flipped.
            const int src_row = (!up && row >= 6 && row <= 9) ? 15 - row : row;
            uint8_t c = codeColor(kScrollUp[src_row][col]);
            if (row >= 6 && row <= 9 && c == kBlack && col > 1 && col < 14) {
                if (!active) c = kGrayA5;
                else if (pressed) c = kWhite;
            } else if (pressed && c != kBlack) {
                c = c == kWhite ? kGray63 : kGray73;
            } else if (!active && c != kBlack) {
                c = kGrayF7;
            } else if (!active && col > 0 && col < 15 && row == (up ? 15 : 0)) {
                c = kGray73;   // inactive boxes are divided from the track in gray
            }
            Pixel(x + col, y + row, c);
        }
}

void ScrollBar(const Rect &r, int total, int visible, int first, int pressed_arrow)
{
    const bool active = total > visible && visible > 0;
    arrowBox(r.x, r.y, true, active, pressed_arrow < 0);
    arrowBox(r.x, r.y + r.h - 16, false, active, pressed_arrow > 0);
    const int track_y = r.y + 16, track_h = r.h - 32;
    VLine(r.x, track_y, track_h, kBlack);
    VLine(r.x + 15, track_y, track_h, kBlack);
    if (!active) {
        Fill(r.x + 1, track_y, 14, track_h, kGrayF7);
        return;
    }
    // Sunken channel, then the lavender thumb with its grip ridges.
    for (int y = track_y; y < track_y + track_h; ++y) {
        Pixel(r.x + 1, y, kGray94);
        Pixel(r.x + 2, y, kGrayA5);
        HLine(r.x + 3, y, 10, kGrayC5);
        Pixel(r.x + 13, y, kGrayCE);
        Pixel(r.x + 14, y, kGrayDE);
    }
    HLine(r.x + 1, track_y, 13, kGray94);
    HLine(r.x + 2, track_y + 1, 11, kGrayA5);
    const int thumb_h = 17;
    const int span = track_h - thumb_h;
    const int max_first = total - visible;
    const int ty = track_y + (span > 0 ? (first * span + max_first / 2) / max_first : 0);
    CodedSprite(r.x, ty, kThumb);
}

// ============================================================================
// Icons (original artwork)
// ============================================================================

static const char *const kHardDisk[16] = {
    "                ",
    "                ",
    "                ",
    "                ",
    "  KKKKKKKKKKKK  ",
    " KWWWWWWWWWWWDK ",
    " KWEEEEEEEEEECK ",
    " KWEEEEEEEEEECK ",
    " KWEEEEEEEgEECK ",
    " KWEEEEEEEEEECK ",
    " KDCCCCCCCCCCCK ",
    "  KKKKKKKKKKKK  ",
    "                ",
    "                ",
    "                ",
    "                ",
};
static const char *const kCD[16] = {
    "                ",
    "     KKKKKK     ",
    "   KKWWllaaKK   ",
    "  KWWWWllaaasK  ",
    " KWWWWWllaaassK ",
    " KWWWWKKKKaassK ",
    "KWWWWKWWWWKassdK",
    "KllllKWKKWKssddK",
    "KaaaaKWKKWKddddK",
    "KaaaaKWWWWKddddK",
    " KaassKKKKddddK ",
    " KaassssddddddK ",
    "  KssssdddddKK  ",
    "   KKddddddKK   ",
    "     KKKKKK     ",
    "                ",
};
static const char *const kFolder[16] = {
    "                ",
    "                ",
    "  KKKKK         ",
    " KlllllK        ",
    "KlaaaaaaKKKKKKK ",
    "KlllllllllllllsK",
    "KlaaaaaaaaaaaasK",
    "KlaaaaaaaaaaaasK",
    "KlaaaaaaaaaaaasK",
    "KlaaaaaaaaaaaasK",
    "KlaaaaaaaaaaaasK",
    "KlaaaaaaaaaaaasK",
    "KsssssssssssssdK",
    " KKKKKKKKKKKKKK ",
    "                ",
    "                ",
};
static const char *const kLock[11] = {
    "  KKKKK  ",
    " KK   KK ",
    " K     K ",
    " K     K ",
    "KKKKKKKKK",
    "KEEEEEEEK",
    "KEEEKEEEK",
    "KEEEKEEEK",
    "KEEEEEEEK",
    "KCCCCCCCK",
    "KKKKKKKKK",
};

static void caution(int x, int y)
{
    // Yellow triangle with a black outline and exclamation mark.
    for (int row = 0; row < 28; ++row) {
        const int half = row / 2 + 1;
        const int cx = x + 16;
        HLine(cx - half, y + 2 + row, 2 * half, row == 27 ? kBlack : kYellow);
        Pixel(cx - half, y + 2 + row, kBlack);
        Pixel(cx + half - 1, y + 2 + row, kBlack);
    }
    Fill(x + 15, y + 11, 3, 10, kBlack);
    Fill(x + 15, y + 23, 3, 3, kBlack);
}

static void stopSign(int x, int y)
{
    // Red octagon with a white exclamation mark.
    for (int row = 0; row < 30; ++row) {
        int inset = 0;
        if (row < 9) inset = 9 - row;
        else if (row > 20) inset = row - 20;
        const bool edge = row == 0 || row == 29;
        HLine(x + 1 + inset, y + 1 + row, 30 - 2 * inset, edge ? kBlack : kRed);
        Pixel(x + 1 + inset, y + 1 + row, kBlack);
        Pixel(x + 30 - inset, y + 1 + row, kBlack);
    }
    Fill(x + 14, y + 7, 4, 12, kWhite);
    Fill(x + 14, y + 21, 4, 4, kWhite);
}

static void note(int x, int y)
{
    // Rounded speech balloon with an "i".
    Fill(x + 3, y + 3, 26, 20, kWhite);
    FrameRect({x + 2, y + 2, 28, 22}, kBlack);
    Pixel(x + 2, y + 2, kGrayE6);
    Pixel(x + 29, y + 2, kGrayE6);
    Pixel(x + 2, y + 23, kGrayE6);
    Pixel(x + 29, y + 23, kGrayE6);
    for (int i = 0; i < 5; ++i) HLine(x + 8, y + 24 + i, 5 - i, kBlack);
    Fill(x + 15, y + 6, 3, 3, kBlue);
    Fill(x + 15, y + 11, 3, 9, kBlue);
}

static void card(int x, int y)
{
    // SD card: dark body with a clipped corner, gold contacts and a label.
    const uint8_t gold = ColorFor(rgb(0xD6, 0xAE, 0x4A));
    for (int row = 0; row < 30; ++row) {
        const int clip = row < 6 ? 6 - row : 0;
        HLine(x + 5 + clip, y + 1 + row, 22 - clip, row == 0 || row == 29 ? kBlack : kGray63);
        Pixel(x + 5 + clip, y + 1 + row, kBlack);
        Pixel(x + 26, y + 1 + row, kBlack);
    }
    for (int i = 0; i < 5; ++i) Fill(x + 11 + i * 3, y + 3, 2, 6, gold);
    Fill(x + 8, y + 13, 16, 13, kWhite);
    FrameRect({x + 8, y + 13, 16, 13}, kBlack);
    Text(x + 9, y + 24, "SD", kBlack);
}

void DrawIcon(Icon icon, int x, int y, bool inverted)
{
    switch (icon) {
    case kIconHardDisk: CodedSprite(x, y, kHardDisk, inverted); break;
    case kIconCD: CodedSprite(x, y, kCD, inverted); break;
    case kIconFolder: CodedSprite(x, y, kFolder, inverted); break;
    case kIconLock: CodedSprite(x, y, kLock, inverted); break;
    case kIconMac: CodedSprite(x, y, kMacSmall, inverted); break;
    case kIconCaution: caution(x, y); break;
    case kIconStop: stopSign(x, y); break;
    case kIconNote: note(x, y); break;
    case kIconCard: card(x, y); break;
    case kIconNone: break;
    }
}

void SignalBars(int x, int y, int bars, bool inverted)
{
    const uint8_t on = inverted ? kWhite : kBlack;
    const uint8_t off = inverted ? kGray73 : kGrayA5;
    for (int i = 0; i < 4; ++i) {
        const int h = 3 * (i + 1);
        const Rect bar = {x + i * 4, y + 12 - h, 3, h};
        if (i < bars) Fill(bar, on);
        else FrameRect(bar, off);
    }
}

} // namespace PrebootUI
