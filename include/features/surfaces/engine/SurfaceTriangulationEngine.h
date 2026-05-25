#pragma once

#include <array>
#include <vector>

#include <vtkSmartPointer.h>

class vtkPolyData;

namespace surfaces
{
struct SurfacePolyline
{
    std::vector<std::array<double, 3>> points;
};

struct TriangulationOptions
{
    bool optimize = false;
    bool usePolylineConstraints = false;
    double rdpTolerance = 0.0;
};

struct TriangulationResult
{
    vtkSmartPointer<vtkPolyData> mesh;
    int inputPolylineCount = 0;
    int inputPointCount = 0;
    int simplifiedPointCount = 0;
    int constraintEdgeCount = 0;
    int triangleCount = 0;
};

TriangulationResult TriangulatePolylines(
    const std::vector<SurfacePolyline> &polylines,
    const TriangulationOptions &options);
}
