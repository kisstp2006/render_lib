#pragma once

#include "engine/backend/IRenderBackend.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace engine {
class Application;
}

namespace engine::diagnostics {

struct StabilityStressConfig
{
    uint32_t Cycles = 4;
    uint32_t FramesPerStage = 8;
    int BaseWidth = 1280;
    int BaseHeight = 720;
    int SmallWidth = 800;
    int SmallHeight = 450;
    int TallWidth = 960;
    int TallHeight = 720;
    int Monitor = -1;
    bool ExerciseExclusiveFullscreen = true;
    uint64_t MaximumCpuGrowthBytes = 8ull * 1024ull * 1024ull;
    uint64_t MaximumGpuGrowthBytes = 0;
};

struct StabilityStressStageResult
{
    std::string Name;
    uint32_t Cycle = 0;
    int Width = 0;
    int Height = 0;
    bool Minimized = false;
    uint64_t CpuBytes = 0;
    BackendResourceStats Resources;
    bool Passed = true;
    std::string Message;
};

struct StabilityStressReport
{
    std::string Backend;
    uint32_t RequestedCycles = 0;
    uint32_t CompletedCycles = 0;
    uint32_t HotReloadCount = 0;
    bool Completed = false;
    bool Passed = true;
    BackendResourceStats BaselineResources;
    BackendResourceStats FinalResources;
    uint64_t BaselineCpuBytes = 0;
    uint64_t FinalCpuBytes = 0;
    std::vector<StabilityStressStageResult> Stages;
    std::vector<std::string> Errors;
};

// Runs from Application's update callback. The optional callback invalidates
// and rebinds application assets immediately before the renderer itself is
// recreated, so samples can test their real hot-reload path without coupling
// the engine diagnostics module to a particular asset database.
class StabilityStressRunner
{
  public:
    using AssetReloadCallback = std::function<void()>;

    StabilityStressRunner(Application& application, StabilityStressConfig config,
                          AssetReloadCallback assetReload = {});

    void Update(float deltaSeconds);
    [[nodiscard]] bool Finished() const { return m_report.Completed; }
    [[nodiscard]] bool Succeeded() const { return m_report.Completed && m_report.Passed; }
    [[nodiscard]] const StabilityStressReport& Report() const { return m_report; }
    bool WriteJsonReport(const std::filesystem::path& path) const;

  private:
    enum class Phase : uint8_t
    {
        Warmup,
        ResizeSmall,
        ResizeTall,
        ResizeBase,
        Minimize,
        Restore,
        Borderless,
        WindowedAfterBorderless,
        Exclusive,
        WindowedAfterExclusive,
        HotReload,
        Validate
    };

    void EnterPhase();
    void CompletePhase();
    void RecordStage(const char* name);
    void ValidateCycle();
    void Fail(std::string message);
    void Finish();
    static const char* PhaseName(Phase phase);

    Application& m_application;
    StabilityStressConfig m_config;
    AssetReloadCallback m_assetReload;
    StabilityStressReport m_report;
    Phase m_phase = Phase::Warmup;
    uint32_t m_phaseFrame = 0;
    uint32_t m_cycle = 0;
    bool m_phaseEntered = false;
};

} // namespace engine::diagnostics
