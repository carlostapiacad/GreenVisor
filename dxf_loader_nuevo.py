
from __future__ import annotations

from typing import Iterable, List, Tuple
import ezdxf
import numpy as np
import pyvista as pv


def _lwpoly_points(entity) -> List[Tuple[float, float, float]]:
    out: List[Tuple[float, float, float]] = []
    z = float(getattr(entity.dxf, "elevation", 0.0) or 0.0)
    getter = getattr(entity, "get_points", None)
    if callable(getter):
        try:
            for x, y, *_ in getter("xy"):
                out.append((float(x), float(y), z))
            return out
        except Exception:
            try:
                for x, y, *_ in getter():
                    out.append((float(x), float(y), z))
                return out
            except Exception:
                pass
    try:
        for v in entity:
            x, y = float(v[0]), float(v[1])
            out.append((x, y, z))
    except Exception:
        pass
    return out


def _polyline_points(entity) -> List[Tuple[float, float, float]]:
    out: List[Tuple[float, float, float]] = []
    verts_attr = getattr(entity, "vertices", None)
    if callable(verts_attr):
        iterator = entity.vertices()
    elif isinstance(verts_attr, list):
        iterator = verts_attr
    else:
        iterator = []
    for v in iterator:
        if hasattr(v, "dxf") and hasattr(v.dxf, "location"):
            loc = v.dxf.location
            x, y = float(loc.x), float(loc.y)
            z = float(getattr(loc, "z", 0.0) or 0.0)
            out.append((x, y, z))
    return out


def _collect_polydata_from_msp(msp, include_layers: set[str] | None) -> pv.PolyData:
    points: List[Tuple[float, float, float]] = []
    index_of: dict[Tuple[float, float, float], int] = {}

    def pid(pt) -> int:
        if hasattr(pt, "x"):
            x, y, z = float(pt.x), float(pt.y), float(getattr(pt, "z", 0.0) or 0.0)
        else:
            x, y = float(pt[0]), float(pt[1])
            z = float(getattr(pt, "z", pt[2] if len(pt) > 2 else 0.0))
        key = (x, y, z)
        idx = index_of.get(key)
        if idx is not None:
            return idx
        idx = len(points)
        index_of[key] = idx
        points.append(key)
        return idx

    vtk_lines: List[int] = []
    vtk_faces: List[int] = []
    vtk_verts: List[int] = []

    def add_polyline(ids: List[int]):
        if len(ids) >= 2:
            vtk_lines.extend([len(ids), *ids])

    def add_face(ids: List[int]):
        if len(ids) >= 3:
            vtk_faces.extend([len(ids), *ids])

    def add_vertex(pt):
        idx = pid(pt)
        vtk_verts.extend([1, idx])

    for entity in msp:
        try:
            layer_name = entity.dxf.layer if hasattr(entity, "dxf") else None
        except Exception:
            layer_name = None
        if include_layers is not None and layer_name not in include_layers:
            continue

        etype = entity.dxftype().upper() if hasattr(entity, "dxftype") else ""
        try:
            if etype == "POINT":
                add_vertex(entity.dxf.location)
            elif etype == "LINE":
                ids = [pid(entity.dxf.start), pid(entity.dxf.end)]
                add_polyline(ids)
            elif etype == "LWPOLYLINE":
                pts = _lwpoly_points(entity)
                ids = [pid(p) for p in pts]
                if getattr(entity, "closed", False) and len(ids) > 2:
                    ids = ids + [ids[0]]
                add_polyline(ids)
            elif etype == "POLYLINE":
                pts = _polyline_points(entity)
                ids = [pid(p) for p in pts]
                is_mesh = int(getattr(entity.dxf, "m_count", 0) or 0) > 0 or int(getattr(entity.dxf, "n_count", 0) or 0) > 0
                if not ids:
                    continue
                if not is_mesh and getattr(entity, "closed", False) and len(ids) > 2:
                    ids = ids + [ids[0]]
                add_polyline(ids)
            elif etype in ("3DFACE", "FACE3D"):
                verts = [entity.dxf.vtx0, entity.dxf.vtx1, entity.dxf.vtx2, entity.dxf.vtx3]
                dense: List[Tuple[float, float, float]] = []
                for p in verts:
                    candidate = (float(p.x), float(p.y), float(p.z))
                    if not dense or candidate != dense[-1]:
                        dense.append(candidate)
                ids = [pid(pt) for pt in dense[:4]]
                add_face(ids)
            elif etype == "SOLID":
                p0, p1, p2, p3 = entity.dxf.vtx0, entity.dxf.vtx1, entity.dxf.vtx2, entity.dxf.vtx3
                pts = [
                    (float(p0.x), float(p0.y), float(p0.z)),
                    (float(p1.x), float(p1.y), float(p1.z)),
                    (float(p2.x), float(p2.y), float(p2.z)),
                ]
                if (p3.x, p3.y, p3.z) != (p2.x, p2.y, p2.z):
                    pts.append((float(p3.x), float(p3.y), float(p3.z)))
                ids = [pid(pt) for pt in pts]
                add_face(ids)
        except Exception:
            continue

    poly = pv.PolyData()
    if points:
        poly.points = np.asarray(points, dtype=float)
    if vtk_lines:
        poly.lines = np.asarray(vtk_lines, dtype=np.int64)
    if vtk_faces:
        poly.faces = np.asarray(vtk_faces, dtype=np.int64)
    if vtk_verts:
        poly.verts = np.asarray(vtk_verts, dtype=np.int64)
    return poly


def load_dxf_filtered_as_polydata_nuevo(path: str, include_layers: Iterable[str]) -> pv.PolyData:
    include = set(include_layers or [])
    doc = ezdxf.readfile(path)
    msp = doc.modelspace()
    selected = None if len(include) == 0 else include
    return _collect_polydata_from_msp(msp, selected)


def load_dxf_as_polydata_nuevo(path: str) -> pv.PolyData:
    return load_dxf_filtered_as_polydata_nuevo(path, include_layers=[])
