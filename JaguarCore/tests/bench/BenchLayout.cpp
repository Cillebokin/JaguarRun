#include "JaguarCore/Assertions.h"
#include "JaguarCore/Entities/Layout.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

jaguar::SimplePolygon Rectangle(double width, double height)
{
    const auto shape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    assert(shape.HasValue());
    return shape.Value();
}

jaguar::Item makeItem()
{
    jaguar::OriginalShape original{
        Rectangle(2.0, 2.0), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate, {}};
    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::Create(0, 9901, std::move(original), config);
    assert(item.HasValue());
    return item.Value();
}

jaguar::Container makeContainer()
{
    jaguar::OriginalShape original{
        Rectangle(40.0, 40.0), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate, {}};
    const auto container = jaguar::Container::Create(
        9900, std::move(original), {}, jaguar::CDEConfig{});
    assert(container.HasValue());
    return container.Value();
}

std::vector<jaguar::Transform2d> transforms()
{
    std::vector<jaguar::Transform2d> result;
    for (std::size_t row = 0; row < 6; ++row)
        for (std::size_t column = 0; column < 6; ++column)
            result.push_back(jaguar::Transform2d::FromRotationTranslation(
                static_cast<double>((row + column) % 4) * 0.5 * std::acos(-1.0),
                {2.0 + static_cast<double>(column) * 6.0,
                 2.0 + static_cast<double>(row) * 6.0}));
    return result;
}

double elapsedMilliseconds(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
}

int runLayoutBench()
{
    const jaguar::Item item = makeItem();
    const jaguar::Container container = makeContainer();
    const auto placementTransforms = transforms();
    volatile double checksum = 0.0;

    std::vector<jaguar::SimplePolygon> cached;
    cached.reserve(placementTransforms.size());
    for (const auto& transform : placementTransforms)
    {
        const auto transformed = item.CollisionShape().Transformed(transform);
        if (!transformed.HasValue())
            return 1;
        cached.push_back(transformed.Value());
    }

    auto start = Clock::now();
    for (std::size_t repeat = 0; repeat < 2000; ++repeat)
        for (const auto& shape : cached)
            checksum += shape.Area();
    const double cachedMilliseconds = elapsedMilliseconds(start);

    start = Clock::now();
    for (std::size_t repeat = 0; repeat < 2000; ++repeat)
        for (const auto& transform : placementTransforms)
        {
            const auto transformed = item.CollisionShape().Transformed(transform);
            if (!transformed.HasValue())
                return 1;
            checksum += transformed.Value().Area();
        }
    (void)placementTransforms;
    const double rebuildMilliseconds = elapsedMilliseconds(start);

    start = Clock::now();
    std::size_t placedCount = 0;
    for (std::size_t repeat = 0; repeat < 50; ++repeat)
    {
        jaguar::Layout layout(container);
        std::vector<jaguar::PlacedItemHandle> handles;
        for (const auto& transform : placementTransforms)
        {
            const auto handle = layout.PlaceItem(item, transform);
            if (!handle.HasValue())
                return 1;
            handles.push_back(handle.Value());
        }
        placedCount += layout.PlacedItemCount();
        if (!jaguar::ValidateLayout(layout).HasValue())
            return 1;
        for (const auto handle : handles)
        {
            if (!layout.RemoveItem(handle).HasValue())
                return 1;
        }
    }
    const double placeRemoveMilliseconds = elapsedMilliseconds(start);

    jaguar::Layout snapshotLayout(container);
    for (const auto& transform : placementTransforms)
    {
        const auto placed = snapshotLayout.PlaceItem(item, transform);
        if (!placed.HasValue())
            return 1;
    }
    const auto snapshot = snapshotLayout.Save();
    start = Clock::now();
    for (std::size_t i = 0; i < 500; ++i)
    {
        if (!snapshotLayout.Restore(snapshot).HasValue())
            return 1;
    }
    const double snapshotMilliseconds = elapsedMilliseconds(start);
    assert(snapshotLayout.PlacedItemCount() == placementTransforms.size());

    std::cout << std::fixed << std::setprecision(3)
              << "layout,cached_transform_ms," << cachedMilliseconds << '\n'
              << "layout,rebuild_transform_ms," << rebuildMilliseconds << '\n'
              << "layout,place_remove_ms," << placeRemoveMilliseconds << '\n'
              << "layout,snapshot_restore_ms," << snapshotMilliseconds << '\n'
              << "layout,placed_count," << placedCount << '\n'
              << "layout,checksum," << checksum << '\n';
    return std::isfinite(static_cast<double>(checksum)) ? 0 : 1;
}
