#include "io/DxfLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

namespace
{
struct RawEntity
{
    std::string type;
    std::vector<std::pair<int, std::string>> codes;
};

struct Point
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct PointKey
{
    double x;
    double y;
    double z;
};

struct PointKeyHash
{
    std::size_t operator()(const PointKey &key) const
    {
        std::size_t h1 = std::hash<double>{}(key.x);
        std::size_t h2 = std::hash<double>{}(key.y);
        std::size_t h3 = std::hash<double>{}(key.z);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

struct PointKeyEq
{
    bool operator()(const PointKey &a, const PointKey &b) const
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
};

class DxfGeometryBuilder
{
public:
    DxfGeometryBuilder()
        : points_(vtkSmartPointer<vtkPoints>::New()),
          lines_(vtkSmartPointer<vtkCellArray>::New()),
          polys_(vtkSmartPointer<vtkCellArray>::New()),
          verts_(vtkSmartPointer<vtkCellArray>::New())
    {
    }

    void AddVertex(const Point &pt)
    {
        vtkIdType id = PointId(pt);
        verts_->InsertNextCell(1);
        verts_->InsertCellPoint(id);
    }

    void AddPolyline(const std::vector<Point> &pts, bool closed)
    {
        if (pts.size() < 2) {
            return;
        }
        std::vector<vtkIdType> ids;
        ids.reserve(pts.size() + (closed ? 1 : 0));
        for (const auto &pt : pts) {
            ids.push_back(PointId(pt));
        }
        if (closed && ids.size() > 2) {
            ids.push_back(ids.front());
        }
        lines_->InsertNextCell(static_cast<vtkIdType>(ids.size()));
        for (vtkIdType id : ids) {
            lines_->InsertCellPoint(id);
        }
    }

    void AddFace(const std::vector<Point> &pts)
    {
        if (pts.size() < 3) {
            return;
        }
        std::vector<vtkIdType> ids;
        ids.reserve(pts.size());
        for (const auto &pt : pts) {
            ids.push_back(PointId(pt));
        }
        polys_->InsertNextCell(static_cast<vtkIdType>(ids.size()));
        for (vtkIdType id : ids) {
            polys_->InsertCellPoint(id);
        }
    }

    vtkSmartPointer<vtkPolyData> Build() const
    {
        vtkSmartPointer<vtkPolyData> poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points_);
        if (lines_->GetNumberOfCells() > 0) {
            poly->SetLines(lines_);
        }
        if (polys_->GetNumberOfCells() > 0) {
            poly->SetPolys(polys_);
        }
        if (verts_->GetNumberOfCells() > 0) {
            poly->SetVerts(verts_);
        }
        return poly;
    }

private:
    vtkIdType PointId(const Point &pt)
    {
        PointKey key{pt.x, pt.y, pt.z};
        auto it = point_index_.find(key);
        if (it != point_index_.end()) {
            return it->second;
        }
        vtkIdType id = points_->InsertNextPoint(pt.x, pt.y, pt.z);
        point_index_.emplace(key, id);
        return id;
    }

    vtkSmartPointer<vtkPoints> points_;
    vtkSmartPointer<vtkCellArray> lines_;
    vtkSmartPointer<vtkCellArray> polys_;
    vtkSmartPointer<vtkCellArray> verts_;
    std::unordered_map<PointKey, vtkIdType, PointKeyHash, PointKeyEq> point_index_;
};

std::string Trim(const std::string &text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

std::string ToUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

bool ReadGroup(std::istream &stream, int &code, std::string &value)
{
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (!line.empty()) {
            break;
        }
    }
    if (line.empty() && stream.eof()) {
        return false;
    }
    char *endptr = nullptr;
    long parsed = std::strtol(line.c_str(), &endptr, 10);
    if (endptr == line.c_str()) {
        return false;
    }
    code = static_cast<int>(parsed);
    if (!std::getline(stream, value)) {
        return false;
    }
    value = Trim(value);
    return true;
}

double ToDouble(const std::string &value, double fallback = 0.0)
{
    char *endptr = nullptr;
    double parsed = std::strtod(value.c_str(), &endptr);
    if (endptr == value.c_str()) {
        return fallback;
    }
    return parsed;
}

int ToInt(const std::string &value, int fallback = 0)
{
    char *endptr = nullptr;
    long parsed = std::strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str()) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

std::string GetString(const RawEntity &entity, int code)
{
    for (const auto &pair : entity.codes) {
        if (pair.first == code) {
            return pair.second;
        }
    }
    return "";
}

int GetInt(const RawEntity &entity, int code, int fallback = 0)
{
    for (const auto &pair : entity.codes) {
        if (pair.first == code) {
            return ToInt(pair.second, fallback);
        }
    }
    return fallback;
}

double GetDouble(const RawEntity &entity, int code, double fallback = 0.0)
{
    for (const auto &pair : entity.codes) {
        if (pair.first == code) {
            return ToDouble(pair.second, fallback);
        }
    }
    return fallback;
}

std::vector<double> GetDoubles(const RawEntity &entity, int code)
{
    std::vector<double> values;
    for (const auto &pair : entity.codes) {
        if (pair.first == code) {
            values.push_back(ToDouble(pair.second, 0.0));
        }
    }
    return values;
}

std::vector<RawEntity> ParseEntities(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }

    std::vector<RawEntity> entities;
    RawEntity current;
    bool in_entities = false;
    bool expect_section_name = false;

    int code = 0;
    std::string value;
    while (ReadGroup(file, code, value)) {
        if (code == 0) {
            std::string upper = ToUpper(value);
            if (upper == "SECTION") {
                expect_section_name = true;
                continue;
            }
            if (upper == "ENDSEC") {
                in_entities = false;
                continue;
            }
            if (upper == "EOF") {
                if (in_entities && !current.type.empty()) {
                    entities.push_back(current);
                }
                break;
            }
            if (in_entities) {
                if (!current.type.empty()) {
                    entities.push_back(current);
                }
                current = RawEntity{};
                current.type = upper;
            }
            continue;
        }

        if (expect_section_name && code == 2) {
            in_entities = (ToUpper(value) == "ENTITIES");
            expect_section_name = false;
            continue;
        }

        if (!in_entities || current.type.empty()) {
            continue;
        }

        current.codes.emplace_back(code, value);
    }

    return entities;
}

Point MakePoint(double x, double y, double z)
{
    Point pt;
    pt.x = x;
    pt.y = y;
    pt.z = z;
    return pt;
}
} // namespace

std::unordered_set<std::string> ListDxfLayers(const std::string &path)
{
    const auto entities = ParseEntities(path);
    std::unordered_set<std::string> layers;
    for (const auto &entity : entities) {
        if (entity.type.empty()) {
            continue;
        }
        if (entity.type == "SEQEND" || entity.type == "VERTEX") {
            continue;
        }
        std::string layer = GetString(entity, 8);
        if (layer.empty()) {
            layer = "0";
        }
        layers.insert(layer);
    }
    if (layers.empty()) {
        layers.insert("0");
    }
    return layers;
}

std::unordered_map<std::string, vtkSmartPointer<vtkPolyData>> LoadDxfLayeredPolyData(
    const std::string &path,
    const std::unordered_set<std::string> &include_layers)
{
    const auto entities = ParseEntities(path);
    std::unordered_map<std::string, std::unique_ptr<DxfGeometryBuilder>> builders;

    auto get_builder = [&](const std::string &layer) -> DxfGeometryBuilder * {
        auto it = builders.find(layer);
        if (it != builders.end()) {
            return it->second.get();
        }
        builders[layer] = std::make_unique<DxfGeometryBuilder>();
        return builders[layer].get();
    };

    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto &entity = entities[i];
        const std::string &type = entity.type;
        std::string layer = GetString(entity, 8);
        if (layer.empty()) {
            layer = "0";
        }
        const bool keep_layer = include_layers.empty() || include_layers.count(layer) > 0;

        if (type == "POINT") {
            if (!keep_layer) {
                continue;
            }
            Point pt = MakePoint(GetDouble(entity, 10), GetDouble(entity, 20), GetDouble(entity, 30));
            get_builder(layer)->AddVertex(pt);
        } else if (type == "LINE") {
            if (!keep_layer) {
                continue;
            }
            Point p0 = MakePoint(GetDouble(entity, 10), GetDouble(entity, 20), GetDouble(entity, 30));
            Point p1 = MakePoint(GetDouble(entity, 11), GetDouble(entity, 21), GetDouble(entity, 31));
            get_builder(layer)->AddPolyline({p0, p1}, false);
        } else if (type == "LWPOLYLINE") {
            if (!keep_layer) {
                continue;
            }
            const int flags = GetInt(entity, 70, 0);
            const bool closed = (flags & 1) != 0;
            const double elevation = GetDouble(entity, 38, 0.0);
            const auto xs = GetDoubles(entity, 10);
            const auto ys = GetDoubles(entity, 20);
            const auto zs = GetDoubles(entity, 30);

            std::vector<Point> pts;
            const std::size_t count = std::min(xs.size(), ys.size());
            pts.reserve(count);
            for (std::size_t idx = 0; idx < count; ++idx) {
                double z = elevation;
                if (idx < zs.size()) {
                    z = zs[idx];
                }
                pts.push_back(MakePoint(xs[idx], ys[idx], z));
            }
            get_builder(layer)->AddPolyline(pts, closed);
        } else if (type == "POLYLINE") {
            const int flags = GetInt(entity, 70, 0);
            const bool closed = (flags & 1) != 0;
            const int m_count = GetInt(entity, 71, 0);
            const int n_count = GetInt(entity, 72, 0);
            const bool is_mesh = (m_count > 0) || (n_count > 0);

            std::vector<Point> pts;
            for (++i; i < entities.size(); ++i) {
                const auto &child = entities[i];
                if (child.type == "SEQEND") {
                    break;
                }
                if (child.type != "VERTEX") {
                    continue;
                }
                Point pt = MakePoint(GetDouble(child, 10), GetDouble(child, 20), GetDouble(child, 30));
                pts.push_back(pt);
            }

            if (keep_layer && !is_mesh) {
                get_builder(layer)->AddPolyline(pts, closed);
            }
        } else if (type == "3DFACE" || type == "FACE3D") {
            if (!keep_layer) {
                continue;
            }
            Point p0 = MakePoint(GetDouble(entity, 10), GetDouble(entity, 20), GetDouble(entity, 30));
            Point p1 = MakePoint(GetDouble(entity, 11), GetDouble(entity, 21), GetDouble(entity, 31));
            Point p2 = MakePoint(GetDouble(entity, 12), GetDouble(entity, 22), GetDouble(entity, 32));
            Point p3 = MakePoint(GetDouble(entity, 13), GetDouble(entity, 23), GetDouble(entity, 33));

            std::vector<Point> pts;
            pts.reserve(4);
            for (const auto &p : {p0, p1, p2, p3}) {
                if (pts.empty() || !(p.x == pts.back().x && p.y == pts.back().y && p.z == pts.back().z)) {
                    pts.push_back(p);
                }
            }
            get_builder(layer)->AddFace(pts);
        } else if (type == "SOLID") {
            if (!keep_layer) {
                continue;
            }
            Point p0 = MakePoint(GetDouble(entity, 10), GetDouble(entity, 20), GetDouble(entity, 30));
            Point p1 = MakePoint(GetDouble(entity, 11), GetDouble(entity, 21), GetDouble(entity, 31));
            Point p2 = MakePoint(GetDouble(entity, 12), GetDouble(entity, 22), GetDouble(entity, 32));
            Point p3 = MakePoint(GetDouble(entity, 13), GetDouble(entity, 23), GetDouble(entity, 33));

            std::vector<Point> pts{p0, p1, p2};
            if (!(p3.x == p2.x && p3.y == p2.y && p3.z == p2.z)) {
                pts.push_back(p3);
            }
            get_builder(layer)->AddFace(pts);
        }
    }

    std::unordered_map<std::string, vtkSmartPointer<vtkPolyData>> output;
    for (auto &entry : builders) {
        vtkSmartPointer<vtkPolyData> poly = entry.second->Build();
        if (poly && poly->GetNumberOfPoints() > 0) {
            output.emplace(entry.first, poly);
        }
    }
    return output;
}

vtkSmartPointer<vtkPolyData> LoadDxfFilteredAsPolyData(
    const std::string &path,
    const std::unordered_set<std::string> &include_layers)
{
    const auto entities = ParseEntities(path);
    DxfGeometryBuilder builder;

    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto &entity = entities[i];
        const std::string &type = entity.type;
        const std::string layer = GetString(entity, 8);
        const bool keep_layer = include_layers.empty() || include_layers.count(layer) > 0;

        if (type == "POINT") {
            if (!keep_layer) {
                continue;
            }
            Point pt = MakePoint(GetDouble(entity, 10), GetDouble(entity, 20), GetDouble(entity, 30));
            builder.AddVertex(pt);
        } else if (type == "LINE") {
            if (!keep_layer) {
                continue;
            }
            Point p0 = MakePoint(GetDouble(entity, 10), GetDouble(entity, 20), GetDouble(entity, 30));
            Point p1 = MakePoint(GetDouble(entity, 11), GetDouble(entity, 21), GetDouble(entity, 31));
            builder.AddPolyline({p0, p1}, false);
        } else if (type == "LWPOLYLINE") {
            if (!keep_layer) {
                continue;
            }
            const int flags = GetInt(entity, 70, 0);
            const bool closed = (flags & 1) != 0;
            const double elevation = GetDouble(entity, 38, 0.0);
            const auto xs = GetDoubles(entity, 10);
            const auto ys = GetDoubles(entity, 20);
            const auto zs = GetDoubles(entity, 30);

            std::vector<Point> pts;
            const std::size_t count = std::min(xs.size(), ys.size());
            pts.reserve(count);
            for (std::size_t idx = 0; idx < count; ++idx) {
                double z = elevation;
                if (idx < zs.size()) {
                    z = zs[idx];
                }
                pts.push_back(MakePoint(xs[idx], ys[idx], z));
            }
            builder.AddPolyline(pts, closed);
        } else if (type == "POLYLINE") {
            const int flags = GetInt(entity, 70, 0);
            const bool closed = (flags & 1) != 0;
            const int m_count = GetInt(entity, 71, 0);
            const int n_count = GetInt(entity, 72, 0);
            const bool is_mesh = (m_count > 0) || (n_count > 0);

            std::vector<Point> pts;
            for (++i; i < entities.size(); ++i) {
                const auto &child = entities[i];
                if (child.type == "SEQEND") {
                    break;
                }
                if (child.type != "VERTEX") {
                    continue;
                }
                Point pt = MakePoint(GetDouble(child, 10), GetDouble(child, 20), GetDouble(child, 30));
                pts.push_back(pt);
            }

            if (keep_layer && !is_mesh) {
                builder.AddPolyline(pts, closed);
            }
        } else if (type == "3DFACE" || type == "FACE3D") {
            if (!keep_layer) {
                continue;
            }
            Point p0 = MakePoint(GetDouble(entity, 10), GetDouble(entity, 20), GetDouble(entity, 30));
            Point p1 = MakePoint(GetDouble(entity, 11), GetDouble(entity, 21), GetDouble(entity, 31));
            Point p2 = MakePoint(GetDouble(entity, 12), GetDouble(entity, 22), GetDouble(entity, 32));
            Point p3 = MakePoint(GetDouble(entity, 13), GetDouble(entity, 23), GetDouble(entity, 33));

            std::vector<Point> pts;
            pts.reserve(4);
            for (const auto &p : {p0, p1, p2, p3}) {
                if (pts.empty() || !(p.x == pts.back().x && p.y == pts.back().y && p.z == pts.back().z)) {
                    pts.push_back(p);
                }
            }
            builder.AddFace(pts);
        } else if (type == "SOLID") {
            if (!keep_layer) {
                continue;
            }
            Point p0 = MakePoint(GetDouble(entity, 10), GetDouble(entity, 20), GetDouble(entity, 30));
            Point p1 = MakePoint(GetDouble(entity, 11), GetDouble(entity, 21), GetDouble(entity, 31));
            Point p2 = MakePoint(GetDouble(entity, 12), GetDouble(entity, 22), GetDouble(entity, 32));
            Point p3 = MakePoint(GetDouble(entity, 13), GetDouble(entity, 23), GetDouble(entity, 33));

            std::vector<Point> pts{p0, p1, p2};
            if (!(p3.x == p2.x && p3.y == p2.y && p3.z == p2.z)) {
                pts.push_back(p3);
            }
            builder.AddFace(pts);
        }
    }

    return builder.Build();
}

vtkSmartPointer<vtkPolyData> LoadDxfAsPolyData(const std::string &path)
{
    return LoadDxfFilteredAsPolyData(path, {});
}

std::unordered_map<std::string, std::vector<DxfPolyline>> LoadDxfLayeredPolylines(
    const std::string &path,
    const std::unordered_set<std::string> &include_layers)
{
    const auto entities = ParseEntities(path);
    std::unordered_map<std::string, std::vector<DxfPolyline>> layered;

    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto &entity = entities[i];
        const std::string &type = entity.type;
        std::string layer = GetString(entity, 8);
        if (layer.empty()) {
            layer = "0";
        }
        const bool keep_layer = include_layers.empty() || include_layers.count(layer) > 0;
        if (!keep_layer) {
            if (type == "POLYLINE") {
                for (++i; i < entities.size(); ++i) {
                    if (entities[i].type == "SEQEND") {
                        break;
                    }
                }
            }
            continue;
        }

        if (type == "LWPOLYLINE") {
            const auto xs = GetDoubles(entity, 10);
            const auto ys = GetDoubles(entity, 20);
            const auto zs = GetDoubles(entity, 30);
            const double elevation = GetDouble(entity, 38, 0.0);
            const std::size_t count = std::min(xs.size(), ys.size());
            if (count < 2) {
                continue;
            }

            DxfPolyline poly;
            poly.reserve(count);
            for (std::size_t idx = 0; idx < count; ++idx) {
                double z = elevation;
                if (idx < zs.size()) {
                    z = zs[idx];
                }
                poly.push_back({xs[idx], ys[idx], z});
            }
            layered[layer].push_back(std::move(poly));
        } else if (type == "POLYLINE") {
            const int m_count = GetInt(entity, 71, 0);
            const int n_count = GetInt(entity, 72, 0);
            const bool is_mesh = (m_count > 0) || (n_count > 0);
            if (is_mesh) {
                for (++i; i < entities.size(); ++i) {
                    if (entities[i].type == "SEQEND") {
                        break;
                    }
                }
                continue;
            }

            DxfPolyline poly;
            for (++i; i < entities.size(); ++i) {
                const auto &child = entities[i];
                if (child.type == "SEQEND") {
                    break;
                }
                if (child.type != "VERTEX") {
                    continue;
                }
                poly.push_back({
                    GetDouble(child, 10),
                    GetDouble(child, 20),
                    GetDouble(child, 30)});
            }
            if (poly.size() >= 2) {
                layered[layer].push_back(std::move(poly));
            }
        }
    }

    return layered;
}

std::vector<DxfPolyline> LoadDxfPolylines(const std::string &path)
{
    std::vector<DxfPolyline> polylines;
    const auto layered = LoadDxfLayeredPolylines(path, {});
    for (const auto &entry : layered) {
        polylines.insert(polylines.end(), entry.second.begin(), entry.second.end());
    }
    return polylines;
}
