#pragma once

#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Export.h"

#include <array>
#include <cmath>

namespace jaguar
{
// Normalize an angle with jagua-rs's f32 remainder and correction rules.
JAGUARCORE_API float NormalizeRotationRustF32(float rotationRad) noexcept;

// 二维旋转和平移变换
struct Transform2d
{
    double rotationRad = 0.0; // 旋转角度，单位为弧度
    Point2d translation{};    // 平移量

    Transform2d() noexcept = default;
    Transform2d(double rotation, Point2d offset) noexcept
        : rotationRad(rotation), translation(offset)
    {
    }

    // 创建单位变换
    inline static Transform2d Identity() noexcept { return {}; }

    // 根据旋转角度和平移量创建变换
    inline static Transform2d FromRotationTranslation(
        double rotation, Point2d offset) noexcept
    {
        return Transform2d{rotation, offset};
    }

    // 判断变换参数是否为有限值
    inline bool IsFinite() const noexcept
    {
        return std::isfinite(rotationRad) && translation.IsFinite();
    }

    // 对点应用变换
    JAGUARCORE_API Point2d Apply(const Point2d& point) const noexcept;
    // 以 f32 精度执行与 jagua-rs Transformation 等价的点变换
    JAGUARCORE_API Point2d ApplyRustF32(const Point2d& point) const noexcept;

    // 获取逆变换
    JAGUARCORE_API Transform2d Inverse() const noexcept;
    // 以 f32 精度执行与 jagua-rs Transformation::inverse 等价的逆变换
    JAGUARCORE_API Transform2d InverseRustF32() const noexcept;

    // 按顺序组合两个变换
    JAGUARCORE_API Transform2d FollowedBy(const Transform2d& next) const noexcept;
    // 以 f32 精度执行与 jagua-rs Transformation::transform 等价的组合
    JAGUARCORE_API Transform2d FollowedByRustF32(
        const Transform2d& next) const noexcept;

private:
    std::array<float, 4> RustF32RotationMatrix() const noexcept;
    void StoreRustF32RotationMatrix(
        const std::array<float, 4>& matrix) noexcept;

    std::array<float, 4> m_RustF32RotationMatrix{1.0F, 0.0F, 0.0F, 1.0F};
    float m_RustF32SourceRotation = 0.0F;
    bool m_HasRustF32RotationMatrix = false;
};
}
