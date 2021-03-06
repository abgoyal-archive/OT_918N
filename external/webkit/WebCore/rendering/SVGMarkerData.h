

#ifndef SVGMarkerData_h
#define SVGMarkerData_h

#if ENABLE(SVG)
#include "FloatConversion.h"
#include "Path.h"
#include <wtf/MathExtras.h>

namespace WebCore {

class SVGResourceMarker;

class SVGMarkerData {
public:
    enum Type {
        Unknown = 0,
        Start,
        Mid,
        End
    };

    SVGMarkerData(const Type& type = Unknown, SVGResourceMarker* marker = 0)
        : m_type(type)
        , m_marker(marker)
    {
    }

    FloatPoint origin() const { return m_origin; }
    SVGResourceMarker* marker() const { return m_marker; }

    float currentAngle() const
    {
        FloatSize inslopeChange = m_inslopePoints[1] - m_inslopePoints[0];
        FloatSize outslopeChange = m_outslopePoints[1] - m_outslopePoints[0];

        double inslope = rad2deg(atan2(inslopeChange.height(), inslopeChange.width()));
        double outslope = rad2deg(atan2(outslopeChange.height(), outslopeChange.width()));

        double angle = 0;
        switch (m_type) {
        case Start:
            angle = outslope;
            break;
        case Mid:
            angle = (inslope + outslope) / 2;
            break;
        case End:
            angle = inslope;
            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }

        return narrowPrecisionToFloat(angle);
    }

    void updateTypeAndMarker(const Type& type, SVGResourceMarker* marker)
    {
        m_type = type;
        m_marker = marker;
    }

    void updateOutslope(const FloatPoint& point)
    {
        m_outslopePoints[0] = m_origin;
        m_outslopePoints[1] = point;
    }

    void updateMarkerDataForPathElement(const PathElement* element)
    {
        FloatPoint* points = element->points;

        switch (element->type) {
        case PathElementAddQuadCurveToPoint:
            // FIXME: https://bugs.webkit.org/show_bug.cgi?id=33115 (PathElementAddQuadCurveToPoint not handled for <marker>)
            m_origin = points[1];
            break;
        case PathElementAddCurveToPoint:
            m_inslopePoints[0] = points[1];
            m_inslopePoints[1] = points[2];
            m_origin = points[2];
            break;
        case PathElementMoveToPoint:
            m_subpathStart = points[0];
        case PathElementAddLineToPoint:
            updateInslope(points[0]);
            m_origin = points[0];
            break;
        case PathElementCloseSubpath:
            updateInslope(points[0]);
            m_origin = m_subpathStart;
            m_subpathStart = FloatPoint();
        }
    }

private:
    void updateInslope(const FloatPoint& point)
    {
        m_inslopePoints[0] = m_origin;
        m_inslopePoints[1] = point;
    }

    Type m_type;
    SVGResourceMarker* m_marker;
    FloatPoint m_origin;
    FloatPoint m_subpathStart;
    FloatPoint m_inslopePoints[2];
    FloatPoint m_outslopePoints[2];
};

}

#endif // ENABLE(SVG)
#endif // SVGMarkerData_h
