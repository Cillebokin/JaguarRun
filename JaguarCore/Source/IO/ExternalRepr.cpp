#include "JaguarCore/IO/ExternalRepr.h"

#include <utility>

namespace jaguar
{
ExternalShape ExternalShape::Rectangle(double xMin,
                                       double yMin,
                                       double width,
                                       double height)
{
    return ExternalShape{ExternalRectangle{xMin, yMin, width, height}};
}

ExternalShape ExternalShape::SimplePolygon(std::vector<Point2d> points)
{
    return ExternalShape{ExternalSimplePolygon{std::move(points)}};
}

ExternalShape ExternalShape::Polygon(std::vector<Point2d> outer,
                                     std::vector<std::vector<Point2d>> holes)
{
    return ExternalShape{ExternalPolygon{std::move(outer), std::move(holes)}};
}

ExternalShape ExternalShape::MultiPolygon(std::vector<ExternalPolygon> polygons)
{
    return ExternalShape{ExternalMultiPolygon{std::move(polygons)}};
}

RotationPolicy RotationPolicy::None()
{
    return RotationPolicy{RotationPolicyKind::None, {}, 0.0};
}

RotationPolicy RotationPolicy::Discrete(std::vector<double> anglesDegrees)
{
    return RotationPolicy{RotationPolicyKind::Discrete,
                           std::move(anglesDegrees), 0.0};
}

RotationPolicy RotationPolicy::Stepped(double stepDegrees)
{
    return RotationPolicy{RotationPolicyKind::Stepped, {}, stepDegrees};
}

RotationPolicy RotationPolicy::Continuous()
{
    return RotationPolicy{RotationPolicyKind::Continuous, {}, 0.0};
}
}
