#pragma once

#include "JaguarCore/collision/hazard.h"
#include "JaguarCore/entities/item.h"
#include "JaguarCore/export.h"

#include <memory>

namespace jaguar
{
using PlacedItemHandle = HazardHandle;

class PlacedItem
{
public:
    static JAGUARCORE_API Result<PlacedItem> create(const Item& item,
                                                   Transform2d transform);

    JAGUARCORE_API const Item& item() const noexcept;
    JAGUARCORE_API ItemIndex itemIndex() const noexcept;
    JAGUARCORE_API const Transform2d& transform() const noexcept;
    JAGUARCORE_API const SimplePolygon& collisionShape() const noexcept;
    JAGUARCORE_API double area() const noexcept;

private:
    PlacedItem(std::shared_ptr<const Item> item,
               Transform2d transform,
               SimplePolygon collisionShape);

    std::shared_ptr<const Item> m_Item;
    Transform2d m_Transform = Transform2d::identity();
    SimplePolygon m_CollisionShape;
};
}
