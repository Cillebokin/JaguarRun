#pragma once

#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Geometry/Point.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace jaguar
{
struct ExternalRectangle
{
    double xMin = 0.0;
    double yMin = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct ExternalSimplePolygon
{
    std::vector<Point2d> points;
};

struct ExternalPolygon
{
    std::vector<Point2d> outer;
    std::vector<std::vector<Point2d>> holes;
};

struct ExternalMultiPolygon
{
    std::vector<ExternalPolygon> polygons;
};

struct ExternalShape
{
    using Value = std::variant<ExternalRectangle,
                               ExternalSimplePolygon,
                               ExternalPolygon,
                               ExternalMultiPolygon>;

    Value value{};

    static JAGUARCORE_API ExternalShape Rectangle(double xMin,
                                                  double yMin,
                                                  double width,
                                                  double height);
    static JAGUARCORE_API ExternalShape SimplePolygon(std::vector<Point2d> points);
    static JAGUARCORE_API ExternalShape Polygon(
        std::vector<Point2d> outer,
        std::vector<std::vector<Point2d>> holes = {});
    static JAGUARCORE_API ExternalShape MultiPolygon(
        std::vector<ExternalPolygon> polygons);
};

enum class RotationPolicyKind
{
    None,
    Discrete,
    Stepped,
    Continuous,
};

struct RotationPolicy
{
    RotationPolicyKind kind = RotationPolicyKind::None;
    std::vector<double> anglesDegrees;
    double stepDegrees = 0.0;

    static JAGUARCORE_API RotationPolicy None();
    static JAGUARCORE_API RotationPolicy Discrete(std::vector<double> anglesDegrees);
    static JAGUARCORE_API RotationPolicy Stepped(double stepDegrees);
    static JAGUARCORE_API RotationPolicy Continuous();
};

struct ExternalItem
{
    ItemId id = 0;
    RotationPolicy rotation{};
    ExternalShape shape{};
    std::optional<std::size_t> minQuality;
};

struct ExternalQualityZone
{
    std::size_t quality = 0;
    ExternalShape shape{};
};

struct ExternalContainer
{
    ContainerId id = 0;
    ExternalShape shape{};
    std::vector<ExternalQualityZone> zones;
};

struct ExternalTransformation
{
    double rotationDegrees = 0.0;
    Point2d translation{};
};

struct ExternalPlacedItem
{
    ItemId itemId = 0;
    ExternalTransformation transformation{};
};

struct ExternalLayout
{
    ContainerId containerId = 0;
    std::vector<ExternalPlacedItem> placedItems;
    double density = 0.0;
};

struct ExternalDemandItem
{
    ExternalItem item;
    std::uint64_t demand = 0;
};

struct ExternalSPInstance
{
    std::string name;
    double minItemSeparation = 0.0;
    std::vector<ExternalDemandItem> items;
    double stripHeight = 0.0;
};

struct ExternalSPSolution
{
    double stripWidth = 0.0;
    ExternalLayout layout{};
    double density = 0.0;
    std::uint64_t runTimeSec = 0;
};

struct ExternalBin
{
    ExternalContainer container;
    std::uint64_t stock = 0;
    std::uint64_t cost = 0;
};

struct ExternalBPInstance
{
    std::string name;
    double minItemSeparation = 0.0;
    std::vector<ExternalDemandItem> items;
    std::vector<ExternalBin> bins;
};

struct ExternalBPSolution
{
    std::uint64_t cost = 0;
    std::vector<ExternalLayout> layouts;
    double density = 0.0;
    std::uint64_t runTimeSec = 0;
};

struct ExternalMSPStrip
{
    double height = 0.0;
    double maxWidth = 0.0;
};

struct ExternalMSPInstance
{
    std::string name;
    double minItemSeparation = 0.0;
    std::vector<ExternalDemandItem> items;
    ExternalMSPStrip strips{};
};

struct ExternalMSPSolution
{
    std::vector<ExternalLayout> layouts;
    std::vector<ExternalContainer> containers;
    double density = 0.0;
    std::uint64_t runTimeSec = 0;
};
}
