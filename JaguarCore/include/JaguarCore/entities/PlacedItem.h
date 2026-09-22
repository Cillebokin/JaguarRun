#pragma once

#include "JaguarCore/Collision/Hazard.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Export.h"

#include <memory>

namespace jaguar
{
using PlacedItemHandle = HazardHandle;

class PlacedItem
{
public:
    static JAGUARCORE_API Result<PlacedItem> Create(const Item& item,
                                                   Transform2d transform);

    JAGUARCORE_API const Item& GetItem() const noexcept;
    JAGUARCORE_API ItemIndex GetItemIndex() const noexcept;
    JAGUARCORE_API const Transform2d& Transform() const noexcept;
    JAGUARCORE_API const SimplePolygon& CollisionShape() const noexcept;
    JAGUARCORE_API double Area() const noexcept;

private:
    PlacedItem(std::shared_ptr<const Item> item,
               Transform2d transform,
               SimplePolygon collisionShape);

    std::shared_ptr<const Item> m_Item;
    Transform2d m_Transform = Transform2d::Identity();
    SimplePolygon m_CollisionShape;
};
}
