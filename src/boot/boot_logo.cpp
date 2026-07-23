/**
 * @file boot_logo.cpp
 * @brief GEOPIX boot logo animation implementation
 *
 * Technique (learned from M5Launcher boot screen, adapted):
 *  - Assets are full-frame 240x135 RGB565 images on black background.
 *  - At first use we build a SPARSE pixel list (only non-black pixels),
 *    so each animation frame redraws ~2k pixels instead of 32k.
 *  - Fade = per-pixel RGB565 channel scaling by alpha (0..255).
 *  - Unlike M5Launcher, no button wait: sequence ends and falls straight
 *    through to the main UI.
 */
#include "boot_logo.h"
#include "assets/geopix_logo_img.h"
#include "assets/geopix_text_img.h"
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

static void drawLayer(LGFX_Sprite* cv, const std::vector<SparsePx>& px, uint8_t alpha) {
    if (alpha == 0) return;
    for (const auto& p : px) {
        cv->writePixel(p.x, p.y, alpha == 255 ? p.color : scale565(p.color, alpha));
    }
}

// Render one composed frame: black background + logo layer + text layer
static void renderFrame(LGFX_Sprite* cv,
                        const std::vector<SparsePx>& logo, uint8_t logoA,
                        const std::vector<SparsePx>& text, uint8_t textA) {
    cv->fillScreen(TFT_BLACK);
    drawLayer(cv, logo, logoA);
    drawLayer(cv, text, textA);
    cv->pushSprite(0, 0);
}

// Animate alpha from a0 to a1 over durMs, calling renderFrame each step
static void fadeStep(LGFX_Sprite* cv,
                     const std::vector<SparsePx>& logo,
                     const std::vector<SparsePx>& text,
                     int logoA0, int logoA1, int textA0, int textA1,
                     unsigned long durMs) {
    unsigned long start = millis();
    while (true) {
        unsigned long el = millis() - start;
        if (el >= durMs) break;
        float t = (float)el / (float)durMs;
        // easeOutQuad for a soft feel (M5Launcher-style easing)
        float e = 1.0f - (1.0f - t) * (1.0f - t);
        uint8_t la = (uint8_t)(logoA0 + (logoA1 - logoA0) * e);
        uint8_t ta = (uint8_t)(textA0 + (textA1 - textA0) * e);
        renderFrame(cv, logo, la, text, ta);
        delay(16);   // ~60fps
    }
    renderFrame(cv, logo, (uint8_t)logoA1, text, (uint8_t)textA1);
}

// ============ PUBLIC ============
void bootLogoPlay(LGFX_Sprite* canvas) {
    if (!canvas) return;

    std::vector<SparsePx> logo, text;
    buildSparse(geopix_logo_data, GEOPIX_LOGO_W, GEOPIX_LOGO_H, logo);
    buildSparse(geopix_text_data, GEOPIX_TEXT_W, GEOPIX_TEXT_H, text);

    // 1. Logo fade in (600ms)
    fadeStep(canvas, logo, text, 0, 255, 0, 0, 600);

    // 2. Text fade in, logo stays (600ms)
    fadeStep(canvas, logo, text, 255, 255, 0, 255, 600);

    // 3. Hold both (800ms)
    delay(800);

    // 4. Fade out both (500ms)
    fadeStep(canvas, logo, text, 255, 0, 255, 0, 500);

    // 5. Clear -> caller switches to Main UI
    canvas->fillScreen(TFT_BLACK);
    canvas->pushSprite(0, 0);
}
