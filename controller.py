from __future__ import annotations

from typing import Callable, Optional, Tuple

from app.core.query.measurement_query_controller import MeasurementQueryController


class CameraRotationController:
    """Interactive helper to re-focus the camera around a snapped 3D point."""

    def __init__(
        self,
        *,
        scene,
        on_status: Callable[[str], None] | None = None,
        on_exit: Callable[[], None] | None = None,
        on_center: Callable[[Tuple[float, float, float]], None] | None = None,
    ) -> None:
        self.scene = scene
        self.plotter = scene.plotter
        self.on_status = on_status or (lambda _msg: None)
        self.on_exit = on_exit or (lambda: None)
        self.on_center = on_center or (lambda _pt: None)

        # Use MeasurementQueryController internals to reuse snapping logic and overlays.
        self._snap = MeasurementQueryController(
            scene=scene,
            on_info=lambda _info: None,
            on_status=lambda _msg: None,
            on_exit=lambda: None,
        )

        self._active = False
        self._iren = None
        self._obs_move = None
        self._obs_click = None
        self._obs_key = None

    def is_active(self) -> bool:
        return self._active

    def activate(self) -> None:
        if self._active:
            return

        iren = getattr(self.plotter, "iren", None)
        if iren is None:
            self.on_status("Centrar vista: interactor no disponible.")
            return

        # Prepare snapping helpers fresh for current scene state.
        self._snap._pt_locator_by_layer.clear()
        self._snap._clear_ring()
        try:
            self.scene.hide_snap_highlights(all=True)
        except Exception:
            pass
        self._snap._build_pointpicker_list()

        self._iren = iren
        self._obs_move = iren.add_observer("MouseMoveEvent", self._on_mouse_move)
        self._obs_click = iren.add_observer("LeftButtonPressEvent", self._on_left_click)
        self._obs_key = iren.add_observer("KeyPressEvent", self._on_key_press)
        self._active = True

        self.on_status("Centrar vista: apunta al punto y clic para fijarlo. ESC para cancelar.")

    def deactivate(self, *, status: Optional[str] = None) -> None:
        if not self._active:
            if status:
                self.on_status(status)
            return

        try:
            for oid in (self._obs_move, self._obs_click, self._obs_key):
                if oid is not None and self._iren is not None:
                    self._iren.remove_observer(oid)
        except Exception:
            pass
        self._obs_move = self._obs_click = self._obs_key = None
        self._active = False

        self._snap._clear_ring()
        self._cleanup_picklist()
        self._snap._pt_locator_by_layer.clear()

        if status:
            self.on_status(status)
        try:
            self.on_exit()
        except Exception:
            pass

    def _cleanup_picklist(self) -> None:
        try:
            if getattr(self._snap, "_picker_has_list", False):
                picker = self._snap._point_picker
                picker.InitializePickList()
                picker.PickFromListOff()
        except Exception:
            pass
        self._snap._picker_has_list = False

    # --- VTK observers ---
    def _on_key_press(self, caller, evt):  # pragma: no cover - GUI callback
        if not self._active:
            return
        try:
            key = caller.GetKeySym().lower()
        except Exception:
            key = ""
        if key == "escape":
            self.deactivate(status="Centrar vista: cancelado.")

    def _on_mouse_move(self, caller, evt):  # pragma: no cover - GUI callback
        if not self._active:
            return
        try:
            x, y = (caller.GetEventPosition() if hasattr(caller, "GetEventPosition")
                    else self._iren.GetEventPosition())
        except Exception:
            return

        lid, pt = self._snap._hover_find_point(int(x), int(y))
        if pt is not None:
            self._snap._show_ring(lid, pt)
        else:
            self._snap._clear_ring()

    def _on_left_click(self, caller, evt):  # pragma: no cover - GUI callback
        if not self._active:
            return
        snap_pt = getattr(self._snap, "_ring_pt", None)
        if snap_pt is None:
            try:
                x, y = (caller.GetEventPosition() if hasattr(caller, "GetEventPosition")
                        else self._iren.GetEventPosition())
            except Exception:
                x = y = None
            if x is not None and y is not None:
                _, snap_pt = self._snap._hover_find_point(int(x), int(y))

        if snap_pt is None:
            self.on_status("Centrar vista: no se encontro un punto cercano.")
            return

        snap_tuple = tuple(float(v) for v in snap_pt)
        if self._apply_center(snap_tuple):
            self.on_center(snap_tuple)
            self.deactivate(status="Centrar vista: foco actualizado.")
        else:
            self.deactivate(status="Centrar vista: no fue posible ajustar la camara.")

    def _apply_center(self, point: Tuple[float, float, float]) -> bool:
        try:
            cam = self.plotter.camera
        except Exception:
            cam = None
        if cam is None:
            return False
        try:
            pos = cam.GetPosition()
            foc = cam.GetFocalPoint()
            vup = cam.GetViewUp()
        except Exception:
            return False

        if not pos or not foc:
            return False

        offset = (
            float(pos[0]) - float(foc[0]),
            float(pos[1]) - float(foc[1]),
            float(pos[2]) - float(foc[2]),
        )
        new_pos = (
            point[0] + offset[0],
            point[1] + offset[1],
            point[2] + offset[2],
        )

        try:
            cam.SetFocalPoint(*point)
            cam.SetPosition(*new_pos)
            cam.SetViewUp(float(vup[0]), float(vup[1]), float(vup[2]))
        except Exception:
            return False

        iren = getattr(self.plotter, "iren", None)
        if iren is not None and hasattr(iren, "SetCenterOfRotation"):
            try:
                iren.SetCenterOfRotation(*point)
            except Exception:
                pass

        try:
            self.plotter.reset_camera_clipping_range()
        except Exception:
            pass
        try:
            self.plotter.render()
        except Exception:
            pass
        return True
