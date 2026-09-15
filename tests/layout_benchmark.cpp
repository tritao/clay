#define CLAY_IMPLEMENTATION
#include "clay.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

enum class Scenario {
    FlatStatic,
    WidthsChanging,
    WeightedGrow,
    WrappedFlow,
    IntrinsicElements,
    TextHeavy,
    DeepNesting,
};

struct ErrorState {
    int count = 0;
    Clay_ErrorType lastType = CLAY_ERROR_TYPE_INTERNAL_ERROR;
};

struct MeasureState {
    uint64_t calls = 0;
};

struct RunResult {
    std::string name;
    int nodeCount = 0;
    int frames = 0;
    double milliseconds = 0.0;
    size_t arenaBytes = 0;
    size_t arenaCapacity = 0;
    uint32_t arenaAllocations = 0;
    uint64_t measureCalls = 0;
    int renderCommands = 0;
    int errors = 0;
};

void report_error(Clay_ErrorData data) {
    // The active benchmark state is installed through the context error
    // handler, so this callback only exists to satisfy Clay's C ABI.
    auto *state = static_cast<ErrorState *>(data.userData);
    if (state) {
        state->count++;
        state->lastType = data.errorType;
    }
}

Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig *config,
                             void *) {
    const float fontSize = config && config->fontSize > 0 ? config->fontSize : 14.0f;
    return {static_cast<float>(text.length) * fontSize * 0.52f, fontSize * 1.25f};
}

Clay_MeasureResult measure_element(Clay_ElementId, Clay_MeasureConstraints, void *userData) {
    auto *state = static_cast<MeasureState *>(userData);
    state->calls++;
    return {{32.0f, 18.0f}, 13.0f, true};
}

Clay_ElementDeclaration root_declaration(Scenario scenario, float width) {
    Clay_ElementDeclaration declaration{};
    declaration.layout.sizing.width = CLAY_SIZING_FIXED(width);
    declaration.layout.sizing.height = CLAY_SIZING_FIXED(
        scenario == Scenario::WrappedFlow ? 10000.0f : 100000.0f);
    declaration.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    declaration.layout.padding = {8.0f, 8.0f, 8.0f, 8.0f};
    declaration.layout.childGap = 4.0f;
    declaration.layout.columnGap = 4.0f;
    declaration.layout.rowGap = 3.0f;
    declaration.layout.childDistribution = CLAY_DISTRIBUTE_SPACE_EVENLY;
    if (scenario == Scenario::WrappedFlow)
        declaration.layout.wrapMode = CLAY_WRAP_WRAP;
    return declaration;
}

Clay_ElementDeclaration leaf_declaration(Scenario scenario, int index,
                                         MeasureState *measureState) {
    Clay_ElementDeclaration declaration{};
    declaration.layout.sizing.height = CLAY_SIZING_FIXED(20.0f);
    switch (scenario) {
        case Scenario::WeightedGrow:
            declaration.layout.sizing.width.size.minMax = {0.0f, 0.0f};
            declaration.layout.sizing.width.growWeight =
                1.0f + static_cast<float>(index % 7);
            declaration.layout.sizing.width.type = CLAY__SIZING_TYPE_GROW;
            break;
        case Scenario::IntrinsicElements:
            declaration.layout.sizing.width = CLAY_SIZING_FIT();
            declaration.layout.sizing.height = CLAY_SIZING_FIT();
            declaration.custom.customData = measureState;
            break;
        case Scenario::WrappedFlow:
            declaration.layout.sizing.width =
                CLAY_SIZING_FIXED(24.0f + static_cast<float>(index % 4) * 8.0f);
            declaration.layout.sizing.height = CLAY_SIZING_FIXED(20.0f);
            break;
        default:
            declaration.layout.sizing.width = CLAY_SIZING_FIXED(12.0f);
            break;
    }
    return declaration;
}

void build_flat(Scenario scenario, int nodeCount, float width, MeasureState *measureState,
                std::vector<Clay_ElementId> *ids) {
    Clay__OpenElementWithId(Clay_GetElementId(CLAY_STRING("benchmark-root")));
    Clay__ConfigureOpenElement(root_declaration(scenario, width));
    ids->push_back(Clay_GetElementId(CLAY_STRING("benchmark-root")));
    for (int index = 0; index < nodeCount; ++index) {
        Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("benchmark-leaf"),
                                                       static_cast<uint32_t>(index));
        Clay__OpenElementWithId(id);
        Clay__ConfigureOpenElement(leaf_declaration(scenario, index, measureState));
        Clay__CloseElement();
        ids->push_back(id);
    }
    Clay__CloseElement();
}

void build_text(int nodeCount, float width) {
    Clay__OpenElementWithId(Clay_GetElementId(CLAY_STRING("benchmark-root")));
    Clay__ConfigureOpenElement(root_declaration(Scenario::TextHeavy, width));
    Clay_TextElementConfig textConfig{};
    textConfig.fontSize = 14;
    textConfig.wrapMode = CLAY_TEXT_WRAP_WORDS;
    for (int index = 0; index < nodeCount; ++index)
        Clay__OpenTextElement(CLAY_STRING("benchmark text content with intrinsic metrics"),
                              textConfig);
    Clay__CloseElement();
}

void build_deep(int depth, float width, std::vector<Clay_ElementId> *ids) {
    Clay__OpenElementWithId(Clay_GetElementId(CLAY_STRING("benchmark-root")));
    Clay_ElementDeclaration root = root_declaration(Scenario::DeepNesting, width);
    root.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    root.layout.sizing.height = CLAY_SIZING_FIXED(static_cast<float>(depth * 12 + 16));
    Clay__ConfigureOpenElement(root);
    ids->push_back(Clay_GetElementId(CLAY_STRING("benchmark-root")));
    for (int index = 0; index < depth; ++index) {
        Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("benchmark-deep"),
                                                       static_cast<uint32_t>(index));
        Clay__OpenElementWithId(id);
        Clay_ElementDeclaration declaration{};
        declaration.layout.sizing.width = CLAY_SIZING_FIXED(32.0f);
        declaration.layout.sizing.height = CLAY_SIZING_FIXED(12.0f);
        Clay__ConfigureOpenElement(declaration);
        ids->push_back(id);
    }
    for (int index = 0; index < depth; ++index)
        Clay__CloseElement();
    Clay__CloseElement();
}

bool finite_commands(const Clay_RenderCommandArray &commands) {
    for (int index = 0; index < commands.length; ++index) {
        const Clay_RenderCommand &command = commands.internalArray[index];
        const Clay_BoundingBox &box = command.boundingBox;
        if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width) ||
            !std::isfinite(box.height) || box.width < 0.0f || box.height < 0.0f) {
            return false;
        }
    }
    return true;
}

RunResult run(Scenario scenario, int nodeCount, int frames) {
    // Text can emit more render commands than layout elements, so reserve a
    // second slot per text item for Clay's shared capacity setting.
    const int elementCount =
        scenario == Scenario::TextHeavy ? nodeCount * 2 + 8 : nodeCount + 8;
    Clay_SetCurrentContext(nullptr);
    Clay_SetMaxElementCount(elementCount);
    // Clay_MinMemorySize covers its fixed arrays. Keep headroom for render
    // commands and frame-local data generated by pathological trees.
    const size_t memorySize = static_cast<size_t>(Clay_MinMemorySize()) + 16 * 1024 * 1024;
    void *memory = std::malloc(memorySize);
    RunResult result;
    result.name = scenario == Scenario::FlatStatic       ? "flat-static"
                  : scenario == Scenario::WidthsChanging ? "widths-changing"
                  : scenario == Scenario::WeightedGrow   ? "weighted-grow"
                  : scenario == Scenario::WrappedFlow    ? "wrapped-flow"
                  : scenario == Scenario::IntrinsicElements ? "intrinsic-elements"
                  : scenario == Scenario::TextHeavy      ? "text-heavy"
                                                          : "deep-nesting";
    result.nodeCount = nodeCount;
    result.frames = frames;
    if (!memory) {
        result.errors = 1;
        return result;
    }

    ErrorState errors;
    MeasureState measureState;
    Clay_Context *context = Clay_Initialize(
        Clay_CreateArenaWithCapacityAndMemory(memorySize, memory),
        {1200.0f, 100000.0f}, {report_error, &errors});
    if (!context) {
        std::free(memory);
        result.errors = 1;
        return result;
    }
    if (scenario == Scenario::IntrinsicElements)
        Clay_SetMeasureElementFunction(measure_element, &measureState);
    else if (scenario == Scenario::TextHeavy)
        Clay_SetMeasureTextFunction(measure_text, nullptr);

    const auto start = std::chrono::steady_clock::now();
    Clay_RenderCommandArray commands{};
    bool valid = true;
    for (int frame = 0; frame < frames; ++frame) {
        const float width = 1200.0f +
                            (scenario == Scenario::WidthsChanging ? (frame & 1) * 137.0f : 0.0f);
        Clay_SetLayoutDimensions({width, 100000.0f});
        Clay_BeginLayout();
        std::vector<Clay_ElementId> ids;
        ids.reserve(static_cast<size_t>(nodeCount) + 1);
        switch (scenario) {
            case Scenario::TextHeavy:
                build_text(nodeCount, width);
                break;
            case Scenario::DeepNesting:
                build_deep(nodeCount, width, &ids);
                break;
            default:
                build_flat(scenario, nodeCount, width, &measureState, &ids);
                break;
        }
        commands = Clay_EndLayout(0.0f);
        valid = valid && errors.count == 0 && finite_commands(commands);
        if (frame == frames - 1 && scenario != Scenario::TextHeavy) {
            for (const Clay_ElementId id : ids)
                valid = valid && Clay_GetElementData(id).found;
        }
    }
    const auto end = std::chrono::steady_clock::now();

    result.milliseconds =
        std::chrono::duration<double, std::milli>(end - start).count();
    result.arenaBytes = context->internalArena.nextAllocation;
    result.arenaCapacity = context->internalArena.capacity;
    // The context itself is the one arena allocation that happens before the
    // context pointer exists; Clay counts all subsequent arena suballocations.
    result.arenaAllocations = context->arenaAllocationCount + 1;
    result.measureCalls = measureState.calls;
    result.renderCommands = commands.length;
    result.errors = errors.count + (valid ? 0 : 1);

    Clay_SetMeasureElementFunction(nullptr, nullptr);
    Clay_SetMeasureTextFunction(nullptr, nullptr);
    Clay_SetCurrentContext(nullptr);
    std::free(memory);
    return result;
}

void print(const RunResult &result) {
    std::cout << "clay-benchmark scenario=" << result.name << " nodes=" << result.nodeCount
              << " frames=" << result.frames << " layout_ms=" << std::fixed
              << std::setprecision(3) << result.milliseconds << " arena_bytes="
              << result.arenaBytes << " arena_capacity=" << result.arenaCapacity
              << " arena_allocations=" << result.arenaAllocations
              << " measure_callbacks=" << result.measureCalls
              << " render_commands=" << result.renderCommands << " errors=" << result.errors
              << '\n';
}

} // namespace

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const int counts[] = {100, 1000, 10000, 100000};
    bool valid = true;
    for (int count : counts) {
        for (Scenario scenario : {Scenario::FlatStatic, Scenario::WidthsChanging,
                                   Scenario::WeightedGrow, Scenario::WrappedFlow,
                                   Scenario::IntrinsicElements, Scenario::TextHeavy}) {
            const int frames = scenario == Scenario::WidthsChanging ? 3 : 1;
            const RunResult result = run(scenario, count, frames);
            print(result);
            valid = valid && result.errors == 0;
        }
    }
    const RunResult deep = run(Scenario::DeepNesting, 4096, 1);
    print(deep);
    return valid && deep.errors == 0 ? 0 : 1;
}
