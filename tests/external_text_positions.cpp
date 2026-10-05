#define CLAY_IMPLEMENTATION
#include "clay.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>

static uint64_t retained_id = 0;
static Clay_TextLayoutLine lines[4];

static Clay_TextIntrinsicDimensions intrinsic(Clay_StringSlice, Clay_TextElementConfig *, void *) {
    return {{50.0f, 80.0f}, 0.0f, 0.0f, false};
}

static Clay_TextLayoutResult paragraph(Clay_StringSlice text, Clay_TextElementConfig *, float, void *) {
    lines[0] = {{20.0f, 10.0f}, {1, text.chars, text.chars}, {7.0f, 3.0f}};
    lines[1] = {{20.0f, 12.0f}, {1, text.chars + 1, text.chars}, {4.0f, 17.0f}};
    lines[2] = {{0.0f, 10.0f}, {0, text.chars + 2, text.chars}, {0.0f, 31.0f}};
    lines[3] = {{20.0f, 10.0f}, {1, text.chars + 2, text.chars}, {6.0f, 45.0f}};
    return {true, {50.0f, 55.0f}, 4, lines, retained_id, 8.0f, true};
}

int main() {
    Clay_SetMaxElementCount(64);
    const uint32_t size = Clay_MinMemorySize();
    void *memory = std::malloc(size);
    if (!memory) return 1;
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(size, memory), {100.0f, 60.0f}, {});
    Clay_SetMeasureTextIntrinsicFunction(intrinsic, nullptr);
    Clay_SetLayoutTextFunction(paragraph, nullptr);
    // Positioning must not depend on whether the caller retains a glyph layout.
    for (const uint64_t id : {uint64_t(0), uint64_t(42)}) {
        retained_id = id;
        Clay_BeginLayout();
        CLAY(CLAY_ID("root"), {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(100), .height = CLAY_SIZING_FIXED(60)}}}) {
            CLAY_TEXT(CLAY_STRING("abc"), CLAY_TEXT_CONFIG({.fontSize = 16}));
        }
        const auto commands = Clay_EndLayout(0.0f);
        int count = 0;
        const float x[] = {7, 4, 6}, y[] = {3, 17, 45};
        const uint32_t line[] = {0, 1, 3};
        for (int i = 0; i < commands.length; ++i) {
            const auto &command = commands.internalArray[i];
            if (command.commandType != CLAY_RENDER_COMMAND_TYPE_TEXT) continue;
            if (count >= 3 || std::abs(command.boundingBox.x - x[count]) > .001f ||
                std::abs(command.boundingBox.y - y[count]) > .001f ||
                command.renderData.text.textLayoutId != id || command.renderData.text.textLineIndex != line[count]) return 2;
            ++count;
        }
        if (count != 3) return 3;
    }
    std::free(memory);
    return 0;
}
