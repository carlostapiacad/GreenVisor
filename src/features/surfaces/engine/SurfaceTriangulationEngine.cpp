#include "features/surfaces/engine/SurfaceTriangulationEngine.h"

#include <CDT.h>

#include <vtkCellArray.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace
{
struct Bounds2D
{
    double minX = std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();

    void include(const std::array<double, 3> &p)
    {
        minX = std::min(minX, p[0]);
        maxX = std::max(maxX, p[0]);
        minY = std::min(minY, p[1]);
        maxY = std::max(maxY, p[1]);
    }

    double diagonal() const
    {
        if (minX > maxX || minY > maxY) {
            return 0.0;
        }
        const double dx = maxX - minX;
        const double dy = maxY - minY;
        return std::sqrt(dx * dx + dy * dy);
    }
};

struct GridKey
{
    std::int64_t x = 0;
    std::int64_t y = 0;

    bool operator==(const GridKey &other) const
    {
        return x == other.x && y == other.y;
    }
};

struct GridKeyHash
{
    std::size_t operator()(const GridKey &key) const
    {
        const auto hx = std::hash<std::int64_t>{}(key.x);
        const auto hy = std::hash<std::int64_t>{}(key.y);
        return hx ^ (hy + 0x9e3779b97f4a7c15ULL + (hx << 6) + (hx >> 2));
    }
};

double DistanceSquared2D(const std::array<double, 3> &a, const std::array<double, 3> &b)
{
    const double dx = a[0] - b[0];
    const double dy = a[1] - b[1];
    return dx * dx + dy * dy;
}

double DistancePointToSegment2D(
    const std::array<double, 3> &p,
    const std::array<double, 3> &a,
    const std::array<double, 3> &b)
{
    const double vx = b[0] - a[0];
    const double vy = b[1] - a[1];
    const double len2 = vx * vx + vy * vy;
    if (len2 <= std::numeric_limits<double>::epsilon()) {
        return std::sqrt(DistanceSquared2D(p, a));
    }

    const double t = std::clamp(((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / len2, 0.0, 1.0);
    const double x = a[0] + t * vx;
    const double y = a[1] + t * vy;
    const double dx = p[0] - x;
    const double dy = p[1] - y;
    return std::sqrt(dx * dx + dy * dy);
}

void MarkRdpPoints(
    const std::vector<std::array<double, 3>> &points,
    int first,
    int last,
    double tolerance,
    std::vector<bool> &keep)
{
    if (last <= first + 1) {
        return;
    }

    double maxDistance = -1.0;
    int maxIndex = -1;
    for (int i = first + 1; i < last; ++i) {
        const double distance = DistancePointToSegment2D(points[i], points[first], points[last]);
        if (distance > maxDistance) {
            maxDistance = distance;
            maxIndex = i;
        }
    }

    if (maxIndex >= 0 && maxDistance > tolerance) {
        keep[static_cast<std::size_t>(maxIndex)] = true;
        MarkRdpPoints(points, first, maxIndex, tolerance, keep);
        MarkRdpPoints(points, maxIndex, last, tolerance, keep);
    }
}

std::vector<std::array<double, 3>> SimplifyPolyline(
    const std::vector<std::array<double, 3>> &input,
    double tolerance)
{
    if (input.size() <= 2 || tolerance <= 0.0) {
        return input;
    }

    const bool closed = DistanceSquared2D(input.front(), input.back()) <= tolerance * tolerance;
    std::vector<std::array<double, 3>> points = input;
    if (closed && points.size() > 2) {
        points.pop_back();
    }
    if (points.size() <= 2) {
        return input;
    }

    std::vector<bool> keep(points.size(), false);
    keep.front() = true;
    keep.back() = true;
    MarkRdpPoints(points, 0, static_cast<int>(points.size() - 1), tolerance, keep);

    std::vector<std::array<double, 3>> output;
    output.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (keep[i]) {
            output.push_back(points[i]);
        }
    }
    if (closed && output.size() > 2) {
        output.push_back(output.front());
    }
    return output;
}

double ZFromNearestInput(
    const CDT::V2d<double> &point,
    const std::vector<CDT::V2d<double>> &vertices,
    const std::vector<double> &zValues)
{
    double bestDistance = std::numeric_limits<double>::max();
    double bestZ = 0.0;
    const std::size_t count = std::min(vertices.size(), zValues.size());
    for (std::size_t i = 0; i < count; ++i) {
        const double dx = point.x - vertices[i].x;
        const double dy = point.y - vertices[i].y;
        const double distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestZ = zValues[i];
        }
    }
    return bestZ;
}
}

namespace surfaces
{
TriangulationResult TriangulatePolylines(
    const std::vector<SurfacePolyline> &polylines,
    const TriangulationOptions &options)
{
    TriangulationResult result;
    result.inputPolylineCount = static_cast<int>(polylines.size());

    Bounds2D bounds;
    for (const SurfacePolyline &polyline : polylines) {
        result.inputPointCount += static_cast<int>(polyline.points.size());
        for (const auto &point : polyline.points) {
            bounds.include(point);
        }
    }

    const double diagonal = bounds.diagonal();
    if (diagonal <= std::numeric_limits<double>::epsilon()) {
        throw std::runtime_error("The selected lines do not have enough XY extent to triangulate.");
    }

    const double rdpTolerance = options.rdpTolerance > 0.0
        ? options.rdpTolerance
        : diagonal * 0.001;
    const double dedupeStep = std::max(diagonal * 1e-9, 1e-8);

    std::vector<CDT::V2d<double>> vertices;
    std::vector<double> zValues;
    std::vector<CDT::Edge> edges;
    std::set<std::pair<CDT::VertInd, CDT::VertInd>> edgeSet;
    std::unordered_map<GridKey, CDT::VertInd, GridKeyHash> vertexIndex;

    auto vertexForPoint = [&](const std::array<double, 3> &point) {
        const GridKey key{
            static_cast<std::int64_t>(std::llround(point[0] / dedupeStep)),
            static_cast<std::int64_t>(std::llround(point[1] / dedupeStep))
        };
        auto it = vertexIndex.find(key);
        if (it != vertexIndex.end()) {
            return it->second;
        }

        const CDT::VertInd index = static_cast<CDT::VertInd>(vertices.size());
        vertices.emplace_back(point[0], point[1]);
        zValues.push_back(point[2]);
        vertexIndex.insert({key, index});
        return index;
    };

    for (const SurfacePolyline &polyline : polylines) {
        std::vector<std::array<double, 3>> points = options.optimize
            ? SimplifyPolyline(polyline.points, rdpTolerance)
            : polyline.points;
        result.simplifiedPointCount += static_cast<int>(points.size());

        if (points.size() < 2) {
            continue;
        }

        CDT::VertInd previous = vertexForPoint(points.front());
        for (std::size_t i = 1; i < points.size(); ++i) {
            const CDT::VertInd current = vertexForPoint(points[i]);
            if (current == previous) {
                continue;
            }
            if (options.usePolylineConstraints) {
                const auto normalized = std::minmax(previous, current);
                if (edgeSet.insert(normalized).second) {
                    edges.emplace_back(previous, current);
                }
            }
            previous = current;
        }
    }

    if (vertices.size() < 3) {
        throw std::runtime_error("Select at least three non-collinear line points.");
    }

    result.constraintEdgeCount = static_cast<int>(edges.size());

    CDT::Triangulation<double> cdt(CDT::VertexInsertionOrder::Auto);
    cdt.insertVertices(vertices);
    if (options.usePolylineConstraints && !edges.empty()) {
        cdt.insertEdges(edges);
    }
    cdt.eraseSuperTriangle();

    vtkNew<vtkPoints> vtkPointsData;
    vtkPointsData->SetNumberOfPoints(static_cast<vtkIdType>(cdt.vertices.size()));
    for (std::size_t i = 0; i < cdt.vertices.size(); ++i) {
        const double z = i < zValues.size()
            ? zValues[i]
            : ZFromNearestInput(cdt.vertices[i], vertices, zValues);
        vtkPointsData->SetPoint(
            static_cast<vtkIdType>(i),
            cdt.vertices[i].x,
            cdt.vertices[i].y,
            z);
    }

    vtkNew<vtkCellArray> triangles;
    for (const CDT::Triangle &triangle : cdt.triangles) {
        const CDT::VertInd a = triangle.vertices[0];
        const CDT::VertInd b = triangle.vertices[1];
        const CDT::VertInd c = triangle.vertices[2];
        if (a >= cdt.vertices.size() || b >= cdt.vertices.size() || c >= cdt.vertices.size()) {
            continue;
        }
        vtkIdType ids[3] = {
            static_cast<vtkIdType>(a),
            static_cast<vtkIdType>(b),
            static_cast<vtkIdType>(c)
        };
        triangles->InsertNextCell(3, ids);
    }

    result.mesh = vtkSmartPointer<vtkPolyData>::New();
    result.mesh->SetPoints(vtkPointsData);
    result.mesh->SetPolys(triangles);
    result.mesh->BuildCells();
    result.mesh->BuildLinks();
    result.triangleCount = static_cast<int>(result.mesh->GetNumberOfPolys());

    if (result.triangleCount == 0) {
        throw std::runtime_error("The selected lines did not produce any triangles.");
    }

    return result;
}
}
