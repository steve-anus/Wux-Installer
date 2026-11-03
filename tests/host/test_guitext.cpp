#include "check.h"
#include "gui/GuiText.h"
#include "gui/FreeTypeGX.h"
#include "video/CVideo.h"

#include <cwchar>
#include <string>
#include <vector>

// A font with no glyph data: control characters are 0 pixels wide (what
// FreeTypeGX reports for a character it cannot draw), everything else 10.
// drawText records each line it is handed; ASan checks every buffer access
// in GuiText's line builders.
static std::vector<std::wstring> g_drawn;

static uint16_t fakeWidth(wchar_t c) { return c < 0x20 ? 0 : 10; }

FreeTypeGX::FreeTypeGX(const uint8_t*, FT_Long, bool) {}
FreeTypeGX::~FreeTypeGX() {}
uint16_t FreeTypeGX::getCharWidth(const wchar_t c, int16_t, const wchar_t) { return fakeWidth(c); }
uint16_t FreeTypeGX::getWidth(const wchar_t* text, int16_t) {
    unsigned w = 0;
    for (; text && *text; ++text) w += fakeWidth(*text);
    return (uint16_t)(w > 0xFFFF ? 0xFFFF : w);
}
uint16_t FreeTypeGX::drawText(CVideo*, int16_t, int16_t, int16_t, const wchar_t* text, int16_t,
                              const glm::vec4&, uint16_t, uint16_t, const float&, const float&,
                              const glm::vec4&) {
    g_drawn.push_back(text);
    return 0;
}

// Linked but not reached: the tests pass wide strings.
wchar_t* FreeTypeGX::charToWideChar(const char*) { return nullptr; }
char* FreeTypeGX::wideCharToUTF8(const wchar_t*) { return nullptr; }
extern "C" void log_printf(const char*, ...) {}

// The reproduction from ticket T10: a long run of undrawable characters,
// then enough wide text that the line must be shortened.
static std::wstring hostileText(size_t zeroWidth) {
    std::wstring t(zeroWidth, (wchar_t)0x01);
    for (int i = 0; i < 4; ++i) t += L"WIDE TEXT THAT DOES NOT FIT ";
    return t;
}

static bool linesFit(int maxWidth) {
    for (const std::wstring& line : g_drawn)
        if ((int)line.size() >= maxWidth) return false;
    return !g_drawn.empty();
}

void testGuiText() {
    FreeTypeGX font(nullptr, 0);
    GuiText::setPresetFont(&font);
    CVideo video;
    const int kMaxWidth = 100;   // pixels, and the line buffers' size in characters

    for (int mode : {(int)GuiText::WRAP, (int)GuiText::DOTTED}) {
        for (size_t zeros : {0, 50, 97, 98, 99, 100, 1000}) {
            std::wstring t = hostileText(zeros);
            GuiText text(t.c_str(), 20, glm::vec4(1.0f));
            text.setMaxWidth(kMaxWidth, mode);
            g_drawn.clear();
            text.draw(&video);
            CHECK(linesFit(kMaxWidth));
        }
    }

    {   // scrolling through every start position, wrapping round twice
        std::wstring t = hostileText(300);
        GuiText text(t.c_str(), 20, glm::vec4(1.0f));
        text.setMaxWidth(kMaxWidth, GuiText::SCROLL_HORIZONTAL);
        g_drawn.clear();
        const unsigned frames = (unsigned)(t.size() + 20) * 6 * 2 + 200;
        for (unsigned f = 0; f < frames; ++f) {
            video.frameCount = f;
            text.draw(&video);
        }
        CHECK(linesFit(kMaxWidth));
        CHECK(g_drawn.size() == frames);
    }

    {   // a box narrower than the 4 characters the wrap code needs draws nothing
        std::wstring t = hostileText(10);
        GuiText text(t.c_str(), 20, glm::vec4(1.0f));
        text.setMaxWidth(3, GuiText::WRAP);
        g_drawn.clear();
        text.draw(&video);
        CHECK(g_drawn.empty());
    }
}
