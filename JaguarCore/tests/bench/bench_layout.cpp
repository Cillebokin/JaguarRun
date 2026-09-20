#include "JaguarCore/assertions.h"
#include "JaguarCore/entities/layout.h"

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

jaguar::SimplePolygon rectangle(double width, double height)
{
    const auto shape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    assert(shape.hasValue());
    return shape.value();
}

jaguar::Item makeItem()
{
    jaguar::OriginalShape original{
        rectangle(2.0, 2.0), jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Inflate, {}};
    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::create(0, 9901, std::move(original), config);
    assert(item.hasValue());
    return item.value();
}

jaguar::Container makeContainer()
{
    jaguar::OriginalShape original{
        rectangle(40.0, 40.0), jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Deflate, {}};
    const auto container = jaguar::Container::create(
        9900, std::move(original), {}, jaguar::CDEConfig{});
    assert(container.hasValue());
    return container.value();
}

std::vector<jaguar::Transform2d> transforms()
{
    std::vector<jaguar::Transform2d> result;
    for (std::size_t row = 0; row < 6; ++row)
        for (std::size_t column = 0; column < 6; ++column)
            result.push_back(jaguar::Transform2d::fromRotationTranslation(
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
        cached.push_back(item.collisionShape().transformed(transform));

    auto start = Clock::now();
    for (std::size_t repeat = 0; repeat < 2000; ++repeat)
        for (const auto& shape : cached)
            checksum += shape.area();
    const double cachedMilliseconds = elapsedMilliseconds(start);

    start = Clock::now();
    for (std::size_t repeat = 0; repeat < 2000; ++repeat)
        for (const auto& transform : placementTransforms)
            checksum += item.collisionShape().transformed(transform).area();
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
            const auto handle = layout.placeItem(item, transform);
            if (!handle.hasValue())
                return 1;
            handles.push_back(handle.value());
        }
        placedCount += layout.placedItemCount();
        if (!jaguar::validateLayout(layout).hasValue())
            return 1;
        for (const auto handle : handles)
        {
            if (!layout.removeItem(handle).hasValue())
                return 1;
        }
    }
    const double placeRemoveMilliseconds = elapsedMilliseconds(start);

    jaguar::Layout snapshotLayout(container);
    for (const auto& transform : placementTransforms)
    {
        const auto placed = snapshotLayout.placeItem(item, transform);
        if (!placed.hasValue())
            return 1;
    }
    const auto snapshot = snapshotLayout.save();
    start = Clock::now();
    for (std::size_t i = 0; i < 500; ++i)
    {
        if (!snapshotLayout.restore(snapshot).hasValue())
            return 1;
    }
    const double snapshotMilliseconds = elapsedMilliseconds(start);
    assert(snapshotLayout.placedItemCount() == placementTransforms.size());

    std::cout << std::fixed << std::setprecision(3)
              << "layout,cached_transform_ms," << cachedMilliseconds << '\n'
              << "layout,rebuild_transform_ms," << rebuildMilliseconds << '\n'
              << "layout,place_remove_ms," << placeRemoveMilliseconds << '\n'
              << "layout,snapshot_restore_ms," << snapshotMilliseconds << '\n'
              << "layout,placed_count," << placedCount << '\n'
              << "layout,checksum," << checksum << '\n';
    return std::isfinite(static_cast<double>(checksum)) ? 0 : 1;
}
