#pragma once

#include "JaguarCore/export.h"
#include "JaguarCore/geometry/circle.h"
#include "JaguarCore/geometry/edge.h"
#include "JaguarCore/geometry/simple_polygon.h"
#include "JaguarCore/geometry/transformation.h"
#include "JaguarCore/result.h"

#include <array>
#include <cstddef>
#include <vector>

namespace jaguar
{
struct SurrogateConfig
{
    std::array<std::pair<std::size_t, double>, 3> poleLimits{{{0, 0.0}, {0, 0.0}, {0, 0.0}}};
    double failFastPoleAreaRatio = 0.0;
    std::size_t failFastPiers = 0;

    friend bool operator==(const SurrogateConfig& lhs,
                           const SurrogateConfig& rhs) noexcept
    {
        return lhs.poleLimits == rhs.poleLimits &&
               lhs.failFastPoleAreaRatio == rhs.failFastPoleAreaRatio &&
               lhs.failFastPiers == rhs.failFastPiers;
    }

    friend bool operator!=(const SurrogateConfig& lhs,
                           const SurrogateConfig& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

class Surrogate
{
public:
    static JAGUARCORE_API Result<Surrogate> create(const SimplePolygon& shape,
                                                   const SurrogateConfig& config);

    JAGUARCORE_API const std::vector<Circle2d>& poles() const noexcept;
    JAGUARCORE_API const std::vector<Edge2d>& piers() const noexcept;
    JAGUARCORE_API std::size_t failFastPoleCount() const noexcept;
    JAGUARCORE_API double convexHullArea() const noexcept;
    JAGUARCORE_API const SurrogateConfig& config() const noexcept;
    JAGUARCORE_API Surrogate transformed(const Transform2d& transform) const;

private:
    Surrogate(std::vector<Circle2d> poles,
              std::vector<Edge2d> piers,
              std::size_t failFastPoleCount,
              double convexHullArea,
              SurrogateConfig config);

    std::vector<Circle2d> m_Poles;
    std::vector<Edge2d> m_Piers;
    std::size_t m_FailFastPoleCount = 0;
    double m_ConvexHullArea = 0.0;
    SurrogateConfig m_Config{};
};
}
