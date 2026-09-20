#include "JaguarCore/io/external_repr.h"

#include <utility>

namespace jaguar
{
ExternalShape ExternalShape::rectangle(double xMin,
                                       double yMin,
                                       double width,
                                       double height)
{
    return ExternalShape{ExternalRectangle{xMin, yMin, width, height}};
}

ExternalShape ExternalShape::simplePolygon(std::vector<Point2d> points)
{
    return ExternalShape{ExternalSimplePolygon{std::move(points)}};
}

ExternalShape ExternalShape::polygon(std::vector<Point2d> outer,
                                     std::vector<std::vector<Point2d>> holes)
{
    return ExternalShape{ExternalPolygon{std::move(outer), std::move(holes)}};
}

ExternalShape ExternalShape::multiPolygon(std::vector<ExternalPolygon> polygons)
{
    return ExternalShape{ExternalMultiPolygon{std::move(polygons)}};
}

RotationPolicy RotationPolicy::none()
{
    return RotationPolicy{RotationPolicyKind::None, {}, 0.0};
}

RotationPolicy RotationPolicy::discrete(std::vector<double> anglesDegrees)
{
    return RotationPolicy{RotationPolicyKind::Discrete,
                           std::move(anglesDegrees), 0.0};
}

RotationPolicy RotationPolicy::stepped(double stepDegrees)
{
    return RotationPolicy{RotationPolicyKind::Stepped, {}, stepDegrees};
}

RotationPolicy RotationPolicy::continuous()
{
    return RotationPolicy{RotationPolicyKind::Continuous, {}, 0.0};
}
}
