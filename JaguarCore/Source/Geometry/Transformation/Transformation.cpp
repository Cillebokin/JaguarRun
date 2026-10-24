#include "JaguarCore/Geometry/Transformation.h"

#include <cmath>
#include <cstring>

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

std::array<float, 4> Transform2d::RustF32RotationMatrix() const noexcept
{
    const float angle = rustF32(rotationRad);
    const bool sameSource =
        std::memcmp(&angle, &m_RustF32SourceRotation, sizeof(float)) == 0;
    if (m_HasRustF32RotationMatrix && sameSource)
        return m_RustF32RotationMatrix;

    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    return {cosine, -sine, sine, cosine};
}

void Transform2d::StoreRustF32RotationMatrix(
    const std::array<float, 4>& matrix) noexcept
{
    m_RustF32RotationMatrix = matrix;
    m_RustF32SourceRotation = rustF32(rotationRad);
    m_HasRustF32RotationMatrix = true;
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
    const std::array<float, 4> matrix = RustF32RotationMatrix();
    const float x = static_cast<float>(point.x);
    const float y = static_cast<float>(point.y);
    const float tx = static_cast<float>(translation.x);
    const float ty = static_cast<float>(translation.y);
    return Point2d{
        static_cast<double>(matrix[0] * x + matrix[1] * y + tx * 1.0F),
        static_cast<double>(matrix[2] * x + matrix[3] * y + ty * 1.0F)};
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
    const std::array<float, 4> matrix = RustF32RotationMatrix();
    const float m00 = matrix[0];
    const float m01 = matrix[1];
    const float m10 = matrix[2];
    const float m11 = matrix[3];
    const float tx = rustF32(translation.x);
    const float ty = rustF32(translation.y);
    const float determinant =
        m00 * m11 * 1.0F + m01 * ty * 0.0F + tx * m10 * 0.0F -
        tx * m11 * 0.0F - m01 * m10 * 1.0F - m00 * 0.0F * ty;
    const float inverseM00 = (m11 * 1.0F - ty * 0.0F) / determinant;
    const float inverseM01 = (tx * 0.0F - m01 * 1.0F) / determinant;
    const float inverseX = (m01 * ty - tx * m11) / determinant;
    const float inverseM10 = (ty * 0.0F - m10 * 1.0F) / determinant;
    const float inverseM11 = (m00 * 1.0F - tx * 0.0F) / determinant;
    const float inverseY = (tx * m10 - m00 * ty) / determinant;
    Transform2d result{
        static_cast<double>(std::atan2(inverseM10, inverseM00)),
        {static_cast<double>(inverseX), static_cast<double>(inverseY)}};
    result.StoreRustF32RotationMatrix(
        {inverseM00, inverseM01, inverseM10, inverseM11});
    return result;
}

JAGUARCORE_API Transform2d Transform2d::FollowedBy(const Transform2d& next) const noexcept
{
    return Transform2d{rotationRad + next.rotationRad, next.Apply(translation)};
}

JAGUARCORE_API Transform2d Transform2d::FollowedByRustF32(
    const Transform2d& next) const noexcept
{
    const std::array<float, 4> matrix = RustF32RotationMatrix();
    const std::array<float, 4> nextMatrix = next.RustF32RotationMatrix();
    const float matrix00 =
        nextMatrix[0] * matrix[0] + nextMatrix[1] * matrix[2] +
        rustF32(next.translation.x) * 0.0F;
    const float matrix01 =
        nextMatrix[0] * matrix[1] + nextMatrix[1] * matrix[3] +
        rustF32(next.translation.x) * 0.0F;
    const float matrix10 =
        nextMatrix[2] * matrix[0] + nextMatrix[3] * matrix[2] +
        rustF32(next.translation.y) * 0.0F;
    const float matrix11 =
        nextMatrix[2] * matrix[1] + nextMatrix[3] * matrix[3] +
        rustF32(next.translation.y) * 0.0F;
    const float tx = rustF32(translation.x);
    const float ty = rustF32(translation.y);
    const float nextTx = rustF32(next.translation.x);
    const float nextTy = rustF32(next.translation.y);
    const float translatedX =
        nextMatrix[0] * tx + nextMatrix[1] * ty + nextTx * 1.0F;
    const float translatedY =
        nextMatrix[2] * tx + nextMatrix[3] * ty + nextTy * 1.0F;
    Transform2d result{
        static_cast<double>(std::atan2(matrix10, matrix00)),
        {static_cast<double>(translatedX), static_cast<double>(translatedY)}};
    result.StoreRustF32RotationMatrix(
        {matrix00, matrix01, matrix10, matrix11});
    return result;
}
}
