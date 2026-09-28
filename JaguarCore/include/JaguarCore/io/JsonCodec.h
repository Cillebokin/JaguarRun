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

// JSON 编解码选项
struct JsonCodecOptions
{
    // 是否格式化输出 JSON
    bool pretty = false;
};

// 将内部布局转换为外部布局
JAGUARCORE_API Result<ExternalLayout> ExternalLayoutFromLayout(
    const Layout& layout,
    std::optional<ContainerId> containerId = std::nullopt);

// 将 SPP 解转换为外部表示
JAGUARCORE_API Result<ExternalSPSolution> ExternalSPSolutionFromSolution(
    const SPInstance& instance,
    const SPSolution& solution,
    std::uint64_t runTimeSec = 0);
// 将 BPP 解转换为外部表示
JAGUARCORE_API Result<ExternalBPSolution> ExternalBPSolutionFromSolution(
    const BPInstance& instance,
    const BPSolution& solution,
    std::uint64_t runTimeSec = 0);
// 将 MSP 解转换为外部表示
JAGUARCORE_API Result<ExternalMSPSolution> ExternalMSPSolutionFromSolution(
    const MSPInstance& instance,
    const MSPSolution& solution,
    std::uint64_t runTimeSec = 0);

// 解析外部物品 JSON
JAGUARCORE_API Result<ExternalItem> ParseExternalItemJson(
    const std::string& json);
// 序列化外部物品 JSON
JAGUARCORE_API Result<std::string> SerializeExternalItemJson(
    const ExternalItem& item,
    JsonCodecOptions options = {});

// 解析外部容器 JSON
JAGUARCORE_API Result<ExternalContainer> ParseExternalContainerJson(
    const std::string& json);
// 序列化外部容器 JSON
JAGUARCORE_API Result<std::string> SerializeExternalContainerJson(
    const ExternalContainer& container,
    JsonCodecOptions options = {});

// 解析外部布局 JSON
JAGUARCORE_API Result<ExternalLayout> ParseExternalLayoutJson(
    const std::string& json);
// 序列化外部布局 JSON
JAGUARCORE_API Result<std::string> SerializeExternalLayoutJson(
    const ExternalLayout& layout,
    JsonCodecOptions options = {});

// 解析外部 SPP 实例 JSON
JAGUARCORE_API Result<ExternalSPInstance> ParseExternalSPInstanceJson(
    const std::string& json);
// 序列化外部 SPP 实例 JSON
JAGUARCORE_API Result<std::string> SerializeExternalSPInstanceJson(
    const ExternalSPInstance& instance,
    JsonCodecOptions options = {});

// 解析外部 SPP 解 JSON
JAGUARCORE_API Result<ExternalSPSolution> ParseExternalSPSolutionJson(
    const std::string& json);
// 序列化外部 SPP 解 JSON
JAGUARCORE_API Result<std::string> SerializeExternalSPSolutionJson(
    const ExternalSPSolution& solution,
    JsonCodecOptions options = {});

// 解析外部 BPP 实例 JSON
JAGUARCORE_API Result<ExternalBPInstance> ParseExternalBPInstanceJson(
    const std::string& json);
// 序列化外部 BPP 实例 JSON
JAGUARCORE_API Result<std::string> SerializeExternalBPInstanceJson(
    const ExternalBPInstance& instance,
    JsonCodecOptions options = {});

// 解析外部 BPP 解 JSON
JAGUARCORE_API Result<ExternalBPSolution> ParseExternalBPSolutionJson(
    const std::string& json);
// 序列化外部 BPP 解 JSON
JAGUARCORE_API Result<std::string> SerializeExternalBPSolutionJson(
    const ExternalBPSolution& solution,
    JsonCodecOptions options = {});

// 解析外部 MSP 实例 JSON
JAGUARCORE_API Result<ExternalMSPInstance> ParseExternalMSPInstanceJson(
    const std::string& json);
// 序列化外部 MSP 实例 JSON
JAGUARCORE_API Result<std::string> SerializeExternalMSPInstanceJson(
    const ExternalMSPInstance& instance,
    JsonCodecOptions options = {});

// 解析外部 MSP 解 JSON
JAGUARCORE_API Result<ExternalMSPSolution> ParseExternalMSPSolutionJson(
    const std::string& json);
// 序列化外部 MSP 解 JSON
JAGUARCORE_API Result<std::string> SerializeExternalMSPSolutionJson(
    const ExternalMSPSolution& solution,
    JsonCodecOptions options = {});
}
