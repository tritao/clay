#define CLAY_IMPLEMENTATION
#include "clay.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string_view>

static Clay_ErrorData lastError{};
static int errors = 0;
static void error(Clay_ErrorData data) { lastError = data; ++errors; }
static bool complete(Clay_TransitionCallbackArguments) { return true; }
static Clay_ElementId id(uint32_t index) { return Clay_GetElementIdWithIndex(CLAY_STRING("state-test"), index); }

static Clay_RenderCommandArray declare(int count, uint32_t base, bool transitions = false) {
    Clay_BeginLayout();
    Clay__OpenElementWithId(id(base));
    Clay_ElementDeclaration root{};
    root.layout.sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(300)};
    root.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    Clay__ConfigureOpenElement(root);
    for (int i = 0; i < count; ++i) {
        Clay__OpenElementWithId(id(base + 1 + i * 2));
        Clay_ElementDeclaration clip{};
        clip.layout.sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(20)};
        clip.clip.vertical = true;
        clip.clip.childOffset = {0, -7};
        if (transitions) clip.transition.handler = complete;
        Clay__ConfigureOpenElement(clip);
        Clay__OpenElementWithId(id(base + 2 + i * 2));
        Clay_ElementDeclaration child{};
        child.layout.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(100)};
        child.backgroundColor = {10, 20, 30, 255};
        Clay__ConfigureOpenElement(child);
        Clay__CloseElement();
        Clay__CloseElement();
    }
    Clay__CloseElement();
    return Clay_EndLayout(1.0f / 60);
}

static bool capacityError(const char *name, int capacity, uint32_t element) {
    return errors == 1 && lastError.errorType == CLAY_ERROR_TYPE_STATE_CAPACITY_EXCEEDED &&
        lastError.capacity == capacity && lastError.elementId == element &&
        std::string_view(lastError.arrayName.chars, lastError.arrayName.length) == name;
}

int main() {
    Clay_SetCurrentContext(nullptr);
    Clay_SetMaxElementCount(16000);
    std::vector<unsigned char> memory(Clay_MinMemorySize());
    Clay_Context *context = Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(memory.size(), memory.data()),
        {320, 300}, {error, nullptr});
    if (!context || errors) return 1;
    // Default native scrolling supports more than the historical 100 records.
    for (int frame = 0; frame < 8; ++frame) {
        Clay_UpdateScrollContainers(false, {}, 1.0f / 60);
        auto commands = declare(500, 1 + frame * 2000);
        if (errors || !commands.length || context->scrollContainerDatas.length > 1000) return 2;
    }
    // Every stale record, including swapback replacements, retires in one pass.
    Clay_UpdateScrollContainers(false, {}, 0);
    declare(0, 30000);
    Clay_UpdateScrollContainers(false, {}, 0);
    if (context->scrollContainerDatas.length != 0 || errors) return 3;

    // Hosts that own scrolling retain no native records, even with many clips.
    Clay_SetScrollTrackingEnabled(false);
    for (int frame = 0; frame < 8; ++frame) {
        auto commands = declare(3000, 40000 + frame * 7000);
        if (errors || context->scrollContainerDatas.length != 0) return 4;
        auto child = Clay_GetElementData(id(40002 + frame * 7000));
        if (!child.found || std::abs(child.boundingBox.y + 7) > 0.01f) return 5;
        int starts = 0, ends = 0;
        for (int i = 0; i < commands.length; ++i) {
            starts += commands.internalArray[i].commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
            ends += commands.internalArray[i].commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
        }
        if (!starts || starts != ends || Clay_GetScrollContainerData(id(40001 + frame * 7000)).found) return 6;
    }
    // Tracking is reversible; explicit offsets and native scroll queries still work.
    Clay_SetScrollTrackingEnabled(true);
    declare(1, 110000);
    auto native = Clay_GetScrollContainerData(id(110001));
    if (!native.found || native.contentDimensions.height < 100) return 7;
    Clay_SetPointerState({10, 10}, false);
    Clay_UpdateScrollContainers(false, {0, -1}, 1.0f / 60);
    if (native.scrollPosition->y >= 0) return 8;

    // Transition state also scales beyond the historical 200-record limit.
    Clay_SetScrollTrackingEnabled(false);
    for (int frame = 0; frame < 8; ++frame) {
        declare(600, 120000 + frame * 2000, true);
        if (errors || context->transitionDatas.length > 1200) return 9;
    }
    declare(0, 140000);
    if (context->transitionDatas.length != 0 || errors) return 10;

    // Exhaustion is typed, identifies the element/table, and leaves defaults intact.
    Clay_SetScrollTrackingEnabled(true);
    int scrollCapacity = context->scrollContainerDatas.capacity;
    context->scrollContainerDatas.capacity = 2;
    auto scrollDefault = Clay__ScrollContainerDataInternal_DEFAULT;
    auto commands = declare(3, 150000);
    if (!capacityError("scrollContainerDatas", 2, id(150005).id) || commands.length ||
        std::memcmp(&scrollDefault, &Clay__ScrollContainerDataInternal_DEFAULT, sizeof(scrollDefault))) return 11;
    context->scrollContainerDatas.capacity = scrollCapacity;
    Clay_SetScrollTrackingEnabled(false);
    errors = 0;
    int transitionCapacity = context->transitionDatas.capacity;
    context->transitionDatas.capacity = 1;
    auto transitionDefault = Clay__TransitionDataInternal_DEFAULT;
    commands = declare(2, 160000, true);
    if (!capacityError("transitionDatas", 1, id(160003).id) || commands.length ||
        std::memcmp(&transitionDefault, &Clay__TransitionDataInternal_DEFAULT, sizeof(transitionDefault))) return 12;
    context->transitionDatas.capacity = transitionCapacity;
    errors = 0;
    declare(1, 170000);
    if (errors || context->stateCapacityExceeded) return 13;
    Clay_SetCurrentContext(nullptr);
    std::puts("PASS: clipping-only layouts, native scrolling, cache retirement, transitions and safe capacity errors");
}
