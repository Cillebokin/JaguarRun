#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Geometry/Transformation.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

namespace
{
std::map<std::string, std::string> loadReference()
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\Tests\\Fixtures\\RustReference\\trajectory_b11.txt"),
             std::string("JaguarCore\\Tests\\Fixtures\\RustReference\\trajectory_b11.txt"),
             std::string("..\\JaguarRun\\JaguarCore\\Tests\\Fixtures\\RustReference\\trajectory_b11.txt")})
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            continue;
        const std::string content{std::istreambuf_iterator<char>(input),
                                  std::istreambuf_iterator<char>()};
        std::map<std::string, std::string> result;
        std::istringstream lines(content);
        std::string line;
        while (std::getline(lines, line))
        {
            if (line.empty() || line.front() == '#')
                continue;
            const std::size_t separator = line.find('=');
            assert(separator != std::string::npos);
            result.emplace(line.substr(0, separator), line.substr(separator + 1));
        }
        return result;
    }
    return {};
}

std::uint64_t expectedInteger(const std::map<std::string, std::string>& reference,
                              const char* key,
                              int base = 10)
{
    const auto found = reference.find(key);
    assert(found != reference.end());
    return std::stoull(found->second, nullptr, base);
}

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
    const auto shape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(shape.HasValue());
    jaguar::OriginalShape original{
        shape.Value(), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate, {}};
    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::Create(0, 8801, std::move(original), config);
    assert(item.HasValue());
    return item.Value();
}

jaguar::Container makeTrajectoryContainer()
{
    const auto shape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {52.0, 0.0}, {52.0, 26.0}, {0.0, 26.0}});
    assert(shape.HasValue());
    jaguar::OriginalShape original{
        shape.Value(), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate, {}};
    const auto container = jaguar::Container::Create(
        8800, std::move(original), {}, jaguar::CDEConfig{});
    assert(container.HasValue());
    return container.Value();
}
}

void runTrajectoryDifferentialTests()
{
    const auto reference = loadReference();
    assert(reference.size() == 6);

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
            const auto placed = layout.PlaceItem(
                item,
                jaguar::Transform2d::FromRotationTranslation(
                    static_cast<double>(rotationIndex) * 0.5 * 3.14159265358979323846,
                    {x, y}));
            assert(placed.HasValue());
            active.push_back({placed.Value(), serial, serial % 16, (serial / 16) % 8,
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
            const auto removed = layout.RemoveItem(active[selected].handle);
            assert(removed.HasValue());
            (void)removed;
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(selected));
            ++removals;
            mix(checksum, 1); mix(checksum, step); mix(checksum, selected);
            mix(checksum, removedSerial); mix(checksum, active.size());
        }
        else
        {
            const auto snapshot = layout.Save();
            assert(layout.Restore(snapshot).HasValue());
            ++restores;
            mix(checksum, 2); mix(checksum, step); mix(checksum, active.size());
        }
        assert(layout.PlacedItemCount() == active.size());
        assert(jaguar::ValidateLayout(layout).HasValue());
    }

    mix(checksum, 255); mix(checksum, 256); mix(checksum, active.size());
    for (const auto& placement : active)
    {
        const auto snapshot = layout.Save();
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

    assert(placements == expectedInteger(reference, "placements"));
    assert(removals == expectedInteger(reference, "removals"));
    assert(restores == expectedInteger(reference, "restores"));
    assert(active.size() == expectedInteger(reference, "active"));
    assert(checksum == expectedInteger(reference, "checksum", 16));
    assert(rng.state == expectedInteger(reference, "rng_state", 16));
}
