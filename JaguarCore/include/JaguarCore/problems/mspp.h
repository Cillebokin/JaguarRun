#pragma once

#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Export.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace jaguar
{
struct MultiStrip
{
    double maxWidth = 0.0;
    double fixedHeight = 0.0;
    double width = 0.0;
    CDEConfig cdeConfig{};
    ShapeModifyConfig shapeModifyConfig{};

    static JAGUARCORE_API Result<MultiStrip> Create(
        double maxWidth,
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig,
        double width);
    JAGUARCORE_API Result<void> SetWidth(double newWidth);
    JAGUARCORE_API Result<Container> CreateContainer(ContainerId id = 0) const;
};

class MSPInstance
{
public:
    static JAGUARCORE_API Result<MSPInstance> Create(
        std::vector<Item> items,
        std::vector<std::size_t> demands,
        double maxWidth,
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig);

    JAGUARCORE_API std::size_t ItemCount() const noexcept;
    JAGUARCORE_API const Item& GetItem(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t ItemDemand(ItemIndex index) const noexcept;
    JAGUARCORE_API const MultiStrip& BaseStrip() const noexcept;

private:
    MSPInstance(std::vector<std::shared_ptr<const Item>> items,
                std::vector<std::size_t> demands,
                MultiStrip baseStrip);

    std::vector<std::shared_ptr<const Item>> m_Items;
    std::vector<std::size_t> m_Demands;
    MultiStrip m_BaseStrip;
};

struct MSPPlacement
{
    std::size_t layoutIndex = 0;
    ItemIndex itemIndex = 0;
    Transform2d transform = Transform2d::Identity();
};

struct MSPPlacedHandle
{
    std::size_t layoutIndex = 0;
    PlacedItemHandle placedItemHandle{};
};

struct MSPLayoutSnapshot
{
    std::size_t layoutIndex = 0;
    MultiStrip strip{};
    LayoutSnapshot layoutSnapshot;
};

struct MSPSolution
{
    std::vector<MSPLayoutSnapshot> layouts;

    JAGUARCORE_API double Density() const;
    JAGUARCORE_API double TotalStripWidth() const noexcept;
};

class MSPProblem
{
public:
    static JAGUARCORE_API Result<MSPProblem> Create(MSPInstance instance);

    MSPProblem(const MSPProblem&) = delete;
    MSPProblem& operator=(const MSPProblem&) = delete;
    MSPProblem(MSPProblem&&) noexcept = default;
    MSPProblem& operator=(MSPProblem&&) noexcept = default;

    JAGUARCORE_API Result<std::size_t> AddLayout(MultiStrip strip);
    JAGUARCORE_API Result<void> RemoveLayout(std::size_t layoutIndex);
    JAGUARCORE_API Result<MSPPlacedHandle> PlaceItem(
        const MSPPlacement& placement);
    JAGUARCORE_API Result<MSPPlacement> RemoveItem(
        const MSPPlacedHandle& handle);
    JAGUARCORE_API Result<void> ChangeStripWidth(std::size_t layoutIndex,
                                                 double width);
    JAGUARCORE_API Result<void> FitStrip(std::size_t layoutIndex);
    JAGUARCORE_API MSPSolution Save() const;
    JAGUARCORE_API Result<void> Restore(const MSPSolution& solution);

    JAGUARCORE_API std::size_t RemainingDemand(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t PlacedItemCount() const noexcept;
    JAGUARCORE_API std::size_t LayoutCount() const noexcept;
    JAGUARCORE_API double StripWidth(std::size_t layoutIndex) const noexcept;
    JAGUARCORE_API double TotalStripWidth() const noexcept;
    JAGUARCORE_API double Density() const;
    JAGUARCORE_API const MSPInstance& Instance() const noexcept;

private:
    struct LayoutSlot
    {
        std::optional<Layout> layout;
        MultiStrip strip{};
    };

    MSPProblem(MSPInstance instance,
               std::vector<std::size_t> remainingDemand);
    LayoutSlot* GetLayout(std::size_t layoutIndex) noexcept;
    const LayoutSlot* GetLayout(std::size_t layoutIndex) const noexcept;

    MSPInstance m_Instance;
    std::vector<LayoutSlot> m_Layouts;
    std::vector<std::size_t> m_RemainingDemand;
};
}
