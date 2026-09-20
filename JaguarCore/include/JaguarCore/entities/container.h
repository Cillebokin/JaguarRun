#pragma once

#include "JaguarCore/collision/collision_engine.h"
#include "JaguarCore/export.h"
#include "JaguarCore/geometry/original_shape.h"
#include "JaguarCore/result.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace jaguar
{
using ContainerId = std::uint64_t;

struct QualityZone
{
    std::size_t quality = 0;
    std::vector<SimplePolygon> shapes;
};

class Container
{
public:
    static JAGUARCORE_API Result<Container> create(
        ContainerId id,
        OriginalShape originalOuter,
        std::vector<QualityZone> qualityZones,
        CDEConfig cdeConfig);

    Container() = delete;
    Container(const Container&) noexcept = default;
    Container(Container&&) noexcept = default;
    Container& operator=(const Container&) noexcept = default;
    Container& operator=(Container&&) noexcept = default;
    ~Container() = default;

    JAGUARCORE_API ContainerId id() const noexcept;
    JAGUARCORE_API double area() const noexcept;
    JAGUARCORE_API const OriginalShape& originalShape() const noexcept;
    JAGUARCORE_API const SimplePolygon& collisionShape() const noexcept;
    JAGUARCORE_API const Rect2d& bounds() const noexcept;
    JAGUARCORE_API bool sameBase(const Container& other) const noexcept;
    JAGUARCORE_API std::unique_ptr<CollisionEngine> createCollisionEngine() const;

private:
    struct Data;

    explicit Container(std::shared_ptr<const Data> data);

    std::shared_ptr<const Data> m_Data;
};
}
