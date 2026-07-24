/**
 * @file boot_logo.cpp
 * @brief GEOPIX boot logo — minimal fade in / hold / fade out
 *
 * Single full-frame 240x135 RGB565 logo on black background.
 * A sparse pixel list (non-black only, ~2.5k px) keeps each frame cheap.
 * Sequence: fade in -> hold (2s on screen total) -> fade out -> Main UI.
 */
#include "boot_logo.h"
#include "assets/geopix_logo_img.h"
#include <vector>

// ============ SPARSE PIXEL LIST ============
struct SparsePx {
    uint16_t x, y;
    uint16_t color;
};

static void buildSparse(const uint16_t* img, int w, int h, std::vector<SparsePx>& out) {
    out.clear();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t c = img[y * w + x];
            if (c != 0) out.push_back({(uint16_t)x, (uint16_t)y, c});
        }
    }
}

// Scale an RGB565 color by alpha 0..255
static inline uint16_t scale565(uint16_t c, uint8_t a) {
    uint16_t r = ((c >> 11) & 0x1F) * a / 255;
    uint16_t g = ((c >> 5)  & 0x3F) * a / 255;
    uint16_t b = ( c        & 0x1F) * a / 255;
    return (r << 11) | (g << 5) | b;
}

static void renderFrame(LGFX_Sprite* cv, const std::vector<SparsePx>& px, uint8_t alpha) {
    cv->fillScreen(TFT_BLACK);
    if (alpha > 0) {
        for (const auto& p : px) {
            cv->writePixel(p.x, p.y, alpha == 255 ? p.color : scale565(p.color, alpha));
        }
    }
    cv->pushSprite(0, 0);
}

// Animate alpha a0 -> a1 over durMs with easeOutQuad
static void fade(LGFX_Sprite* cv, const std::vector<SparsePx>& px,
                 int a0, int a1, unsigned long durMs) {
    unsigned long start = millis();
    while (true) {
        unsigned long el = millis() - start;
        if (el >= durMs) break;
        float t = (float)el / (float)durMs;
        float e = 1.0f - (1.0f - t) * (1.0f - t);
        renderFrame(cv, px, (uint8_t)(a0 + (a1 - a0) * e));
        delay(16);   // ~60fps
    }
    renderFrame(cv, px, (uint8_t)a1);
}

// ============ PUBLIC ============
void bootLogoPlay(LGFX_Sprite* canvas) {
    if (!canvas) return;

    std::vector<SparsePx> logo;
    buildSparse(geopix_logo_data, GEOPIX_LOGO_W, GEOPIX_LOGO_H, logo);

    fade(canvas, logo, 0, 255, 600);   // 1. fade in
    delay(1400);                       // 2. hold  (2s on screen total)
    fade(canvas, logo, 255, 0, 500);   // 3. fade out

    // 4. Clear -> caller switches straight to Main UI
    canvas->fillScreen(TFT_BLACK);
    canvas->pushSprite(0, 0);
}
