#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/IO/ExternalRepr.h"
#include "JaguarCore/Result.h"

#include <cstdint>
#include <optional>
#include <string>

namespace jaguar
{
class Layout;
class SPInstance;
struct SPSolution;
class BPInstance;
struct BPSolution;
class MSPInstance;
struct MSPSolution;

struct JsonCodecOptions
{
    bool pretty = false;
};

JAGUARCORE_API Result<ExternalLayout> ExternalLayoutFromLayout(
    const Layout& layout,
    std::optional<ContainerId> containerId = std::nullopt);

JAGUARCORE_API Result<ExternalSPSolution> ExternalSPSolutionFromSolution(
    const SPInstance& instance,
    const SPSolution& solution,
    std::uint64_t runTimeSec = 0);
JAGUARCORE_API Result<ExternalBPSolution> ExternalBPSolutionFromSolution(
    const BPInstance& instance,
    const BPSolution& solution,
    std::uint64_t runTimeSec = 0);
JAGUARCORE_API Result<ExternalMSPSolution> ExternalMSPSolutionFromSolution(
    const MSPInstance& instance,
    const MSPSolution& solution,
    std::uint64_t runTimeSec = 0);

JAGUARCORE_API Result<ExternalItem> ParseExternalItemJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> SerializeExternalItemJson(
    const ExternalItem& item,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalContainer> ParseExternalContainerJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> SerializeExternalContainerJson(
    const ExternalContainer& container,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalLayout> ParseExternalLayoutJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> SerializeExternalLayoutJson(
    const ExternalLayout& layout,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalSPInstance> ParseExternalSPInstanceJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> SerializeExternalSPInstanceJson(
    const ExternalSPInstance& instance,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalSPSolution> ParseExternalSPSolutionJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> SerializeExternalSPSolutionJson(
    const ExternalSPSolution& solution,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalBPInstance> ParseExternalBPInstanceJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> SerializeExternalBPInstanceJson(
    const ExternalBPInstance& instance,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalBPSolution> ParseExternalBPSolutionJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> SerializeExternalBPSolutionJson(
    const ExternalBPSolution& solution,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalMSPInstance> ParseExternalMSPInstanceJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> SerializeExternalMSPInstanceJson(
    const ExternalMSPInstance& instance,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalMSPSolution> ParseExternalMSPSolutionJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> SerializeExternalMSPSolutionJson(
    const ExternalMSPSolution& solution,
    JsonCodecOptions options = {});
}
