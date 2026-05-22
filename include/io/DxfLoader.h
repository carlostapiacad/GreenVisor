#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <vtkSmartPointer.h>

class vtkPolyData;

using DxfPoint3 = std::array<double, 3>;
using DxfPolyline = std::vector<DxfPoint3>;

std::unordered_set<std::string> ListDxfLayers(const std::string &path);
vtkSmartPointer<vtkPolyData> LoadDxfAsPolyData(const std::string &path);
vtkSmartPointer<vtkPolyData> LoadDxfFilteredAsPolyData(
    const std::string &path,
    const std::unordered_set<std::string> &include_layers);
std::unordered_map<std::string, vtkSmartPointer<vtkPolyData>> LoadDxfLayeredPolyData(
    const std::string &path,
    const std::unordered_set<std::string> &include_layers);
std::vector<DxfPolyline> LoadDxfPolylines(const std::string &path);
std::unordered_map<std::string, std::vector<DxfPolyline>> LoadDxfLayeredPolylines(
    const std::string &path,
    const std::unordered_set<std::string> &include_layers = {});
