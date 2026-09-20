#include "JaguarCore/entities/layout.h"
#include "JaguarCore/assertions.h"
#include "JaguarCore/geometry/simple_polygon.h"
#include "JaguarCore/geometry/transformation.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <algorithm>
#include <cstddef>
#include <string>

namespace
{
struct XorShift64Star
{
    std::uint64_t state;

    std::uint64_t next() noexcept
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        state *= 2685821657736338717ULL;
        return state;
    }
};

struct ActivePlacement
{
    jaguar::PlacedItemHandle handle{};
    std::size_t serial = 0;
    std::size_t column = 0;
    std::size_t row = 0;
    std::size_t rotation = 0;
};

void mix(std::uint64_t& checksum, std::uint64_t value) noexcept
{
    checksum ^= value;
    checksum *= 1099511628211ULL;
}

jaguar::Item makeTrajectoryItem()
{
    const auto shape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(shape.hasValue());
    jaguar::OriginalShape original{
        shape.value(), jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Inflate, {}};
    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::create(0, 8801, std::move(original), config);
    assert(item.hasValue());
    return item.value();
}

jaguar::Container makeTrajectoryContainer()
{
    const auto shape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {52.0, 0.0}, {52.0, 26.0}, {0.0, 26.0}});
    assert(shape.hasValue());
    jaguar::OriginalShape original{
        shape.value(), jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Deflate, {}};
    const auto container = jaguar::Container::create(
        8800, std::move(original), {}, jaguar::CDEConfig{});
    assert(container.hasValue());
    return container.value();
}
}

void runTrajectoryDifferentialTests()
{
    XorShift64Star rng{0x4A47524312345678ULL};
    jaguar::Layout layout(makeTrajectoryContainer());
    const jaguar::Item item = makeTrajectoryItem();
    std::vector<ActivePlacement> active;
    std::uint64_t checksum = 1469598103934665603ULL;
    std::size_t placements = 0;
    std::size_t removals = 0;
    std::size_t restores = 0;

    for (std::size_t step = 0; step < 256; ++step)
    {
        const std::uint64_t decision = rng.next();
        if (active.empty() || decision % 3ULL == 0ULL)
        {
            const std::size_t serial = placements;
            const double x = 1.0 + static_cast<double>((serial % 16) * 2);
            const double y = 1.0 + static_cast<double>(((serial / 16) % 8) * 2);
            const std::size_t rotationIndex = rng.next() % 4ULL;
            const auto placed = layout.placeItem(
                item,
                jaguar::Transform2d::fromRotationTranslation(
                    static_cast<double>(rotationIndex) * 0.5 * 3.14159265358979323846,
                    {x, y}));
            assert(placed.hasValue());
            active.push_back({placed.value(), serial, serial % 16, (serial / 16) % 8,
                              rotationIndex});
            ++placements;
            mix(checksum, 0); mix(checksum, step); mix(checksum, serial);
            mix(checksum, static_cast<std::uint64_t>(serial % 16));
            mix(checksum, static_cast<std::uint64_t>((serial / 16) % 8));
            mix(checksum, rotationIndex); mix(checksum, active.size());
        }
        else if (decision % 3ULL == 1ULL)
        {
            const std::size_t selected =
                static_cast<std::size_t>(rng.next() % active.size());
            const std::size_t removedSerial = active[selected].serial;
            const auto removed = layout.removeItem(active[selected].handle);
            assert(removed.hasValue());
            (void)removed;
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(selected));
            ++removals;
            mix(checksum, 1); mix(checksum, step); mix(checksum, selected);
            mix(checksum, removedSerial); mix(checksum, active.size());
        }
        else
        {
            const auto snapshot = layout.save();
            assert(layout.restore(snapshot).hasValue());
            ++restores;
            mix(checksum, 2); mix(checksum, step); mix(checksum, active.size());
        }
        assert(layout.placedItemCount() == active.size());
        assert(jaguar::validateLayout(layout).hasValue());
    }

    mix(checksum, 255); mix(checksum, 256); mix(checksum, active.size());
    for (const auto& placement : active)
    {
        const auto snapshot = layout.save();
        const auto found = std::find_if(
            snapshot.placements.begin(), snapshot.placements.end(),
            [placement](const auto& candidate) {
                return candidate.handle == placement.handle;
            });
        assert(found != snapshot.placements.end());
        (void)found;
        mix(checksum, placement.serial);
        mix(checksum, placement.column);
        mix(checksum, placement.row);
        mix(checksum, placement.rotation);
    }

    assert(placements == 91);
    assert(removals == 80);
    assert(restores == 85);
    assert(active.size() == 11);
    assert(checksum == 0x4bfd29cfe028430bULL);
    assert(rng.state == 0xe607235f6e91befaULL);
}
