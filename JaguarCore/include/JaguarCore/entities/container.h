#pragma once

#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Export.h"
#include "JaguarCore/Geometry/OriginalShape.h"
#include "JaguarCore/Result.h"

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
    static JAGUARCORE_API Result<Container> Create(
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

    JAGUARCORE_API ContainerId Id() const noexcept;
    JAGUARCORE_API double Area() const noexcept;
    JAGUARCORE_API const OriginalShape& GetOriginalShape() const noexcept;
    JAGUARCORE_API const SimplePolygon& CollisionShape() const noexcept;
    JAGUARCORE_API const Rect2d& Bounds() const noexcept;
    JAGUARCORE_API bool SameBase(const Container& other) const noexcept;
    JAGUARCORE_API CollisionEnginePtr CreateCollisionEngine() const;

private:
    struct Data;

    explicit Container(std::shared_ptr<const Data> data);

    std::shared_ptr<const Data> m_Data;
};
}
