#pragma once

#include "JaguarCore/entities/layout.h"
#include "JaguarCore/export.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace jaguar
{
struct Strip
{
    double fixedHeight = 0.0;
    double width = 0.0;
    CDEConfig cdeConfig{};
    ShapeModifyConfig shapeModifyConfig{};

    static JAGUARCORE_API Result<Strip> create(
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig,
        double width);
    JAGUARCORE_API Result<void> setWidth(double newWidth);
    JAGUARCORE_API Result<Container> createContainer(ContainerId id = 0) const;
};

class SPInstance
{
public:
    static JAGUARCORE_API Result<SPInstance> create(
        std::vector<Item> items,
        std::vector<std::size_t> demands,
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig);

    JAGUARCORE_API std::size_t itemCount() const noexcept;
    JAGUARCORE_API const Item& item(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t itemDemand(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t totalDemand() const noexcept;
    JAGUARCORE_API double totalItemArea() const noexcept;
    JAGUARCORE_API const Strip& baseStrip() const noexcept;

private:
    SPInstance(std::vector<std::shared_ptr<const Item>> items,
               std::vector<std::size_t> demands,
               Strip baseStrip);

    std::vector<std::shared_ptr<const Item>> m_Items;
    std::vector<std::size_t> m_Demands;
    Strip m_BaseStrip;
};

struct SPPlacement
{
    ItemIndex itemIndex = 0;
    Transform2d transform = Transform2d::identity();
};

struct SPSolution
{
    Strip strip;
    LayoutSnapshot layoutSnapshot;

    JAGUARCORE_API double density() const;
    JAGUARCORE_API double stripWidth() const noexcept;
};

class SPProblem
{
public:
    static JAGUARCORE_API Result<SPProblem> create(SPInstance instance);

    SPProblem(const SPProblem&) = delete;
    SPProblem& operator=(const SPProblem&) = delete;
    SPProblem(SPProblem&&) noexcept = default;
    SPProblem& operator=(SPProblem&&) noexcept = default;

    JAGUARCORE_API Result<PlacedItemHandle> placeItem(
        const SPPlacement& placement);
    JAGUARCORE_API Result<SPPlacement> removeItem(PlacedItemHandle handle);
    JAGUARCORE_API Result<void> changeStripWidth(double newWidth);
    JAGUARCORE_API Result<void> fitStrip();
    JAGUARCORE_API SPSolution save() const;
    JAGUARCORE_API Result<void> restore(const SPSolution& solution);

    JAGUARCORE_API std::size_t remainingDemand(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t placedItemCount() const noexcept;
    JAGUARCORE_API double density() const;
    JAGUARCORE_API double stripWidth() const noexcept;
    JAGUARCORE_API const SPInstance& instance() const noexcept;

private:
    SPProblem(SPInstance instance, Strip strip, Layout layout);

    SPInstance m_Instance;
    Strip m_Strip;
    Layout m_Layout;
    std::vector<std::size_t> m_RemainingDemand;
};
}
