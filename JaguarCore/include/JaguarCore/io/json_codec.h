#pragma once

#include "JaguarCore/export.h"
#include "JaguarCore/io/external_repr.h"
#include "JaguarCore/result.h"

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

JAGUARCORE_API Result<ExternalLayout> externalLayoutFromLayout(
    const Layout& layout,
    std::optional<ContainerId> containerId = std::nullopt);

JAGUARCORE_API Result<ExternalSPSolution> externalSPSolutionFromSolution(
    const SPInstance& instance,
    const SPSolution& solution,
    std::uint64_t runTimeSec = 0);
JAGUARCORE_API Result<ExternalBPSolution> externalBPSolutionFromSolution(
    const BPInstance& instance,
    const BPSolution& solution,
    std::uint64_t runTimeSec = 0);
JAGUARCORE_API Result<ExternalMSPSolution> externalMSPSolutionFromSolution(
    const MSPInstance& instance,
    const MSPSolution& solution,
    std::uint64_t runTimeSec = 0);

JAGUARCORE_API Result<ExternalItem> parseExternalItemJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> serializeExternalItemJson(
    const ExternalItem& item,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalContainer> parseExternalContainerJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> serializeExternalContainerJson(
    const ExternalContainer& container,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalLayout> parseExternalLayoutJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> serializeExternalLayoutJson(
    const ExternalLayout& layout,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalSPInstance> parseExternalSPInstanceJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> serializeExternalSPInstanceJson(
    const ExternalSPInstance& instance,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalSPSolution> parseExternalSPSolutionJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> serializeExternalSPSolutionJson(
    const ExternalSPSolution& solution,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalBPInstance> parseExternalBPInstanceJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> serializeExternalBPInstanceJson(
    const ExternalBPInstance& instance,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalBPSolution> parseExternalBPSolutionJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> serializeExternalBPSolutionJson(
    const ExternalBPSolution& solution,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalMSPInstance> parseExternalMSPInstanceJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> serializeExternalMSPInstanceJson(
    const ExternalMSPInstance& instance,
    JsonCodecOptions options = {});

JAGUARCORE_API Result<ExternalMSPSolution> parseExternalMSPSolutionJson(
    const std::string& json);
JAGUARCORE_API Result<std::string> serializeExternalMSPSolutionJson(
    const ExternalMSPSolution& solution,
    JsonCodecOptions options = {});
}
