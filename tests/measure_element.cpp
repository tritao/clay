#define CLAY_IMPLEMENTATION
#include "clay.h"

#include <cmath>
#include <cstdlib>

struct TestState {
    int calls = 0;
    Clay_ElementId id{};
    Clay_MeasureConstraints constraints{};
};

static Clay_MeasureResult measure_element(Clay_ElementId id,
                                          Clay_MeasureConstraints constraints,
                                          void *userData) {
    auto *state = static_cast<TestState *>(userData);
    state->calls++;
    state->id = id;
    state->constraints = constraints;
    return {{36.0f, 18.0f}, 12.0f, true};
}

static bool close_enough(float left, float right) {
    return std::abs(left - right) < 0.001f;
}

int main() {
    Clay_SetMaxElementCount(64);
    const uint32_t memorySize = Clay_MinMemorySize();
    void *memory = std::malloc(memorySize);
    if (!memory)
        return 2;

    Clay_Context *context = Clay_Initialize(
        Clay_CreateArenaWithCapacityAndMemory(memorySize, memory),
        {100.0f, 80.0f}, {});
    if (!context) {
        std::free(memory);
        return 3;
    }

    TestState state;
    Clay_SetMeasureElementFunction(measure_element, &state);
    Clay_SetLayoutDimensions({100.0f, 80.0f});
    Clay_BeginLayout();
    CLAY(CLAY_ID("root"), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_FIXED(100.0f),
                       .height = CLAY_SIZING_FIXED(80.0f)}}
    }) {
        CLAY(CLAY_ID("custom"), {
            .layout = {.sizing = {.width = CLAY_SIZING_FIT(),
                                   .height = CLAY_SIZING_FIT()}},
            .custom = {.customData = &state}
        }) {}
    }
    Clay_EndLayout(0.0f);

    const Clay_ElementData data = Clay_GetElementData(CLAY_ID("custom"));
    const bool valid = state.calls == 1 && state.id.id == CLAY_ID("custom").id &&
                       close_enough(state.constraints.minWidth, 0.0f) &&
                       close_enough(state.constraints.maxWidth, 100.0f) &&
                       close_enough(state.constraints.minHeight, 0.0f) &&
                       close_enough(state.constraints.maxHeight, 80.0f) && data.found &&
                       close_enough(data.boundingBox.width, 36.0f) &&
                       close_enough(data.boundingBox.height, 18.0f);
    std::free(memory);
    return valid ? 0 : 4;
}
