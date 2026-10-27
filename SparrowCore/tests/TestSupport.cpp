#include "JaguarCore/Version.h"
#include "SparrowCore/Version.h"

#include <iostream>
#include <string_view>

bool RunRngTests(std::ostream& output);
bool RunSamplingTests(std::ostream& output);
bool RunLbfBudgetSplitRuntimeTest(std::ostream& output);
bool RunLbfPinnedSearchTraceProbe(std::ostream& output);
bool RunLbfPinnedContinuousSearchTraceProbe(std::ostream& output);
bool RunLbfPinnedDiscreteSearchTraceProbe(std::ostream& output);
bool RunLbfPinnedNormalComponentProbe(std::ostream& output);
bool RunLbfLossTests(std::ostream& output);
bool RunSppLbfTests(std::ostream& output);
bool RunBppLbfTests(std::ostream& output);
bool RunBppQualityZonePinnedReferenceTest(std::ostream& output);
bool RunQuantifyTests(std::ostream& output);
bool RunSeparatorTests(std::ostream& output);
bool RunWorkerLossSelectorTests(std::ostream& output);
bool RunObserverBoundaryTests(std::ostream& output);
bool RunCompressionMinimumWidthBoundaryTest(std::ostream& output);
bool RunOptimizeEmptyLargeErrorTest(std::ostream& output);
bool RunS07CompressionExtremeRotationError(std::ostream& output);
bool RunS07CompressionDiscreteRotationError(std::ostream& output);
bool RunS07CompressionStripShiftTransformError(std::ostream& output);
bool RunS07CompressionZeroCapacityHandoff(std::ostream& output);
bool RunE5FixedNonFiniteWorkerRounds(std::ostream& output);
bool RunE5SameSeedWorkerTrajectory(std::ostream& output);
bool RunE5Seed4SameSeedWorkerTrajectory(std::ostream& output);
bool RunE5Seed4TopLevelOptimizeCompress(std::ostream& output);
bool RunE5Seed4TopLevelTwoWorkerCompression(std::ostream& output);
bool RunE5Seed10TopLevelTwoWorkerCompression(std::ostream& output);
bool RunE5PlacedItemWeightOverflowWithInfiniteBound(std::ostream& output);
bool RunE5Round24PairedWorkerSelection(std::ostream& output);
bool RunE5Round26PairedWorkerSearch(std::ostream& output);
bool RunExplorationTests(std::ostream& output);
bool RunE5NaturalTimeAcceptance(std::ostream& output);
bool RunWasmTests(std::ostream& output);
bool RunLbfEntryTests(std::ostream& output);
bool RunWindowsConsoleCtrlCProbe(int argc, char** argv, std::ostream& output);

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view{argv[1]} == "--rng")
    {
        return RunRngTests(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--lbf-budget-runtime")
    {
        return RunLbfBudgetSplitRuntimeTest(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--lbf-pinned-search-trace")
    {
        return RunLbfPinnedSearchTraceProbe(std::cout) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--lbf-pinned-search-trace-continuous")
    {
        return RunLbfPinnedContinuousSearchTraceProbe(std::cout) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--lbf-pinned-search-trace-discrete")
    {
        return RunLbfPinnedDiscreteSearchTraceProbe(std::cout) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--lbf-pinned-normal-components")
    {
        return RunLbfPinnedNormalComponentProbe(std::cout) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--sampling")
    {
        return RunSamplingTests(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--bpp-quality-zone")
    {
        return RunBppQualityZonePinnedReferenceTest(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--spp-lbf")
    {
        return RunSppLbfTests(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--bpp-lbf")
    {
        return RunBppLbfTests(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--lbf-entry")
    {
        return RunLbfEntryTests(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--e5-natural-time")
    {
        return RunE5NaturalTimeAcceptance(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--worker-loss-selector")
    {
        return RunWorkerLossSelectorTests(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--observer-boundaries")
    {
        return RunObserverBoundaryTests(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--optimize-empty-large-error")
    {
        return RunOptimizeEmptyLargeErrorTest(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--compression-minimum-width")
    {
        return RunCompressionMinimumWidthBoundaryTest(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--s07-compression-zero-capacity-handoff")
    {
        return RunS07CompressionZeroCapacityHandoff(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--s07-compression-extreme-rotation-error")
    {
        return RunS07CompressionExtremeRotationError(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--s07-compression-discrete-rotation-error")
    {
        return RunS07CompressionDiscreteRotationError(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--s07-compression-strip-shift-error")
    {
        return RunS07CompressionStripShiftTransformError(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--e5-fixed-nonfinite-worker-rounds")
    {
        return RunE5FixedNonFiniteWorkerRounds(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--e5-same-seed-worker-trajectory")
    {
        return RunE5SameSeedWorkerTrajectory(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--e5-same-seed-worker-trajectory-seed4")
    {
        return RunE5Seed4SameSeedWorkerTrajectory(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--e5-seed4-top-level-optimize-compress")
    {
        return RunE5Seed4TopLevelOptimizeCompress(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--e5-seed4-top-level-two-worker-compression")
    {
        return RunE5Seed4TopLevelTwoWorkerCompression(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--e5-seed10-top-level-two-worker-compression")
    {
        return RunE5Seed10TopLevelTwoWorkerCompression(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--e5-placed-item-weight-overflow-infinite-bound")
    {
        return RunE5PlacedItemWeightOverflowWithInfiniteBound(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--e5-round24-paired-worker-selection")
    {
        return RunE5Round24PairedWorkerSelection(std::cerr) ? 0 : 1;
    }
    if (argc == 2
        && std::string_view{argv[1]} == "--e5-round26-paired-worker-search")
    {
        return RunE5Round26PairedWorkerSearch(std::cerr) ? 0 : 1;
    }
    if (argc > 1)
    {
        return RunWindowsConsoleCtrlCProbe(argc, argv, std::cerr) ? 0 : 1;
    }

    const char* version = sparrow::Version();
    if (version == nullptr || version[0] == '\0')
    {
        std::cerr << "SparrowCore version is empty\n";
        return 1;
    }
    const char* jaguarVersion = jaguar::Version();
    if (jaguarVersion == nullptr || jaguarVersion[0] == '\0')
    {
        std::cerr << "JaguarCore version is empty\n";
        return 1;
    }

    if (!RunRngTests(std::cerr))
    {
        return 1;
    }
    if (!RunSamplingTests(std::cerr))
    {
        return 1;
    }
    if (!RunLbfLossTests(std::cerr))
    {
        return 1;
    }
    if (!RunSppLbfTests(std::cerr))
    {
        return 1;
    }
    if (!RunBppLbfTests(std::cerr))
    {
        return 1;
    }
    if (!RunLbfEntryTests(std::cerr))
    {
        return 1;
    }
    if (!RunQuantifyTests(std::cerr))
    {
        return 1;
    }
    if (!RunSeparatorTests(std::cerr))
    {
        return 1;
    }
    if (!RunExplorationTests(std::cerr))
    {
        return 1;
    }
    return RunWasmTests(std::cerr) ? 0 : 1;
}
