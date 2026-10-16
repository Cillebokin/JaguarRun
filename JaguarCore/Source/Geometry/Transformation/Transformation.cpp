#include "JaguarCore/Geometry/Transformation.h"

#include <cmath>

namespace jaguar
{
namespace
{
float rustF32(double value) noexcept
{
    return static_cast<float>(value);
}
}

JAGUARCORE_API float NormalizeRotationRustF32(float rotationRad) noexcept
{
    constexpr float pi = 3.14159265358979323846F;
    const float period = 2.0F * pi;
    const float normalized = std::fmod(rotationRad, period);
    return normalized < 0.0F ? normalized + period : normalized;
}

JAGUARCORE_API Point2d Transform2d::Apply(const Point2d& point) const noexcept
{
    const double sinValue = std::sin(rotationRad);
    const double cosValue = std::cos(rotationRad);
    return Point2d{cosValue * point.x - sinValue * point.y + translation.x,
                   sinValue * point.x + cosValue * point.y + translation.y};
}

JAGUARCORE_API Point2d Transform2d::ApplyRustF32(
    const Point2d& point) const noexcept
{
    const float angle = static_cast<float>(rotationRad);
    const float x = static_cast<float>(point.x);
    const float y = static_cast<float>(point.y);
    const float tx = static_cast<float>(translation.x);
    const float ty = static_cast<float>(translation.y);
    const float sinValue = std::sin(angle);
    const float cosValue = std::cos(angle);
    return Point2d{
        static_cast<double>(cosValue * x - sinValue * y + tx),
        static_cast<double>(sinValue * x + cosValue * y + ty)};
}

JAGUARCORE_API Transform2d Transform2d::Inverse() const noexcept
{
    const double inverseRotation = -rotationRad;
    const double sinValue = std::sin(inverseRotation);
    const double cosValue = std::cos(inverseRotation);
    const Point2d inverseTranslation{
        -(cosValue * translation.x - sinValue * translation.y),
        -(sinValue * translation.x + cosValue * translation.y)};
    return Transform2d{inverseRotation, inverseTranslation};
}

JAGUARCORE_API Transform2d Transform2d::InverseRustF32() const noexcept
{
    const float angle = rustF32(rotationRad);
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    const float tx = rustF32(translation.x);
    const float ty = rustF32(translation.y);
    const float determinant = cosine * cosine + sine * sine;
    const float inverseCosine = cosine / determinant;
    const float inverseSine = -sine / determinant;
    const float inverseX = ((-sine) * ty - tx * cosine) / determinant;
    const float inverseY = (tx * sine - cosine * ty) / determinant;
    return Transform2d{
        static_cast<double>(std::atan2(inverseSine, inverseCosine)),
        {static_cast<double>(inverseX), static_cast<double>(inverseY)}};
}

JAGUARCORE_API Transform2d Transform2d::FollowedBy(const Transform2d& next) const noexcept
{
    return Transform2d{rotationRad + next.rotationRad, next.Apply(translation)};
}

JAGUARCORE_API Transform2d Transform2d::FollowedByRustF32(
    const Transform2d& next) const noexcept
{
    const float angle = rustF32(rotationRad);
    const float nextAngle = rustF32(next.rotationRad);
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    const float nextSine = std::sin(nextAngle);
    const float nextCosine = std::cos(nextAngle);
    const float matrix00 = nextCosine * cosine + (-nextSine) * sine;
    const float matrix10 = nextSine * cosine + nextCosine * sine;
    const float tx = rustF32(translation.x);
    const float ty = rustF32(translation.y);
    const float nextTx = rustF32(next.translation.x);
    const float nextTy = rustF32(next.translation.y);
    const float translatedX = nextCosine * tx + (-nextSine) * ty + nextTx;
    const float translatedY = nextSine * tx + nextCosine * ty + nextTy;
    return Transform2d{
        static_cast<double>(std::atan2(matrix10, matrix00)),
        {static_cast<double>(translatedX), static_cast<double>(translatedY)}};
}
}
