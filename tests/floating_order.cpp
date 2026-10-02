#define CLAY_IMPLEMENTATION
#include "clay.h"

#include <cmath>
#include <cstdlib>

// A floating child that paints below its floating parent sorts ahead of the
// parent's root. It must still be positioned from the parent's bounding box
// in the current layout, not the one left over from the previous layout.

static bool close_enough(float left, float right) {
    return std::abs(left - right) < 0.001f;
}

static void declare(float panelX, float panelY) {
    Clay_SetLayoutDimensions({200.0f, 120.0f});
    Clay_BeginLayout();
    CLAY(CLAY_ID("root"), {
        .layout = {.sizing = {.width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_GROW()}}
    }) {
        CLAY(CLAY_ID("panel"), {
            .layout = {.sizing = {.width = CLAY_SIZING_FIXED(60.0f),
                                  .height = CLAY_SIZING_FIXED(40.0f)}},
            .backgroundColor = {10, 20, 30, 255},
            .floating = {.offset = {panelX, panelY}, .zIndex = 2,
                         .attachTo = CLAY_ATTACH_TO_PARENT}
        }) {
            CLAY(CLAY_ID("underlay"), {
                .layout = {.sizing = {.width = CLAY_SIZING_GROW(),
                                      .height = CLAY_SIZING_GROW()}},
                .backgroundColor = {40, 50, 60, 255},
                .floating = {.zIndex = 1, .attachTo = CLAY_ATTACH_TO_PARENT}
            }) {}
        }
    }
}

static int check(Clay_RenderCommandArray commands, float panelX, float panelY, int failure) {
    const Clay_ElementData panel = Clay_GetElementData(CLAY_ID("panel"));
    const Clay_ElementData underlay = Clay_GetElementData(CLAY_ID("underlay"));
    if (!panel.found || !underlay.found)
        return failure;
    if (!close_enough(panel.boundingBox.x, panelX) || !close_enough(panel.boundingBox.y, panelY))
        return failure + 1;
    if (!close_enough(underlay.boundingBox.x, panelX) ||
        !close_enough(underlay.boundingBox.y, panelY))
        return failure + 2;
    // Paint order still follows z-index: the underlay is emitted before the panel.
    int32_t panelCommand = -1;
    int32_t underlayCommand = -1;
    for (int32_t index = 0; index < commands.length; ++index) {
        const Clay_RenderCommand *command = Clay_RenderCommandArray_Get(&commands, index);
        if (command->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END)
            return failure + 3;
        if (command->id == CLAY_ID("panel").id)
            panelCommand = index;
        if (command->id == CLAY_ID("underlay").id)
            underlayCommand = index;
    }
    if (panelCommand < 0 || underlayCommand < 0 || underlayCommand > panelCommand)
        return failure + 4;
    return 0;
}

int main() {
    Clay_SetMaxElementCount(64);
    const uint32_t memorySize = Clay_MinMemorySize();
    void *memory = std::malloc(memorySize);
    if (!memory)
        return 2;
    Clay_Context *context = Clay_Initialize(
        Clay_CreateArenaWithCapacityAndMemory(memorySize, memory), {200.0f, 120.0f}, {});
    if (!context) {
        std::free(memory);
        return 3;
    }

    declare(120.0f, 70.0f);
    int result = check(Clay_EndLayout(0.0f), 120.0f, 70.0f, 10);
    if (result == 0) {
        // Move the parent between layouts, as edge clamping does.
        declare(15.0f, 5.0f);
        result = check(Clay_EndLayout(0.0f), 15.0f, 5.0f, 20);
    }
    std::free(memory);
    return result;
}
