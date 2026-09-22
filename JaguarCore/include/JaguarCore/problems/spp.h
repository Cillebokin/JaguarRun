#pragma once

#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Export.h"

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

    static JAGUARCORE_API Result<Strip> Create(
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig,
        double width);
    JAGUARCORE_API Result<void> SetWidth(double newWidth);
    JAGUARCORE_API Result<Container> CreateContainer(ContainerId id = 0) const;
};

class SPInstance
{
public:
    static JAGUARCORE_API Result<SPInstance> Create(
        std::vector<Item> items,
        std::vector<std::size_t> demands,
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig);

    JAGUARCORE_API std::size_t ItemCount() const noexcept;
    JAGUARCORE_API const Item& GetItem(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t ItemDemand(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t TotalDemand() const noexcept;
    JAGUARCORE_API double TotalItemArea() const noexcept;
    JAGUARCORE_API const Strip& BaseStrip() const noexcept;

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
    Transform2d transform = Transform2d::Identity();
};

struct SPSolution
{
    Strip strip;
    LayoutSnapshot layoutSnapshot;

    JAGUARCORE_API double Density() const;
    JAGUARCORE_API double StripWidth() const noexcept;
};

class SPProblem
{
public:
    static JAGUARCORE_API Result<SPProblem> Create(SPInstance instance);

    SPProblem(const SPProblem&) = delete;
    SPProblem& operator=(const SPProblem&) = delete;
    SPProblem(SPProblem&&) noexcept = default;
    SPProblem& operator=(SPProblem&&) noexcept = default;

    JAGUARCORE_API Result<PlacedItemHandle> PlaceItem(
        const SPPlacement& placement);
    JAGUARCORE_API Result<SPPlacement> RemoveItem(PlacedItemHandle handle);
    JAGUARCORE_API Result<void> ChangeStripWidth(double newWidth);
    JAGUARCORE_API Result<void> FitStrip();
    JAGUARCORE_API SPSolution Save() const;
    JAGUARCORE_API Result<void> Restore(const SPSolution& solution);

    JAGUARCORE_API std::size_t RemainingDemand(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t PlacedItemCount() const noexcept;
    JAGUARCORE_API double Density() const;
    JAGUARCORE_API double StripWidth() const noexcept;
    JAGUARCORE_API const SPInstance& Instance() const noexcept;

private:
    SPProblem(SPInstance instance, Strip strip, Layout layout);

    SPInstance m_Instance;
    Strip m_Strip;
    Layout m_Layout;
    std::vector<std::size_t> m_RemainingDemand;
};
}
