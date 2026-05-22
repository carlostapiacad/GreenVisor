
# app/ui/main_window.py
from PyQt6.QtWidgets import (
    QMainWindow, QFileDialog, QDockWidget, QWidget, QVBoxLayout, QHBoxLayout,
    QGroupBox, QToolButton, QSizePolicy, QButtonGroup, QMessageBox, QColorDialog, QMenu,
    QInputDialog, QTabWidget, QLabel, QProgressDialog, QApplication
)
from PyQt6.QtGui import QAction, QIcon
from PyQt6.QtCore import Qt, QSize, QPoint, QTimer
from dataclasses import asdict
import os
import time
import vtk
import numpy as np
import pyvista as pv
from pyvistaqt import QtInteractor"""  """

# Escena y loaders
from app.core.scene.manager import SceneManager
from app.core.loaders.obj_loader import load_obj_mesh
from app.core.loaders.dxf_loader import list_dxf_layers
from app.core.loaders.dxf_loader_nuevo import load_dxf_filtered_as_polydata_nuevo
from app.core.loaders.blockmodel_loader import load_block_model_csv

# UI
from app.ui import icons as Icons
from app.ui.dialogs.dxf_layer_select import DXFLayerSelectDialog
from app.ui.dialogs.export_obj import ExportOBJDialog
from app.ui.dialogs.export_screen import ExportScreenDialog
from app.ui.docks.inspector_panel import InspectorPanel
from app.ui.docks.inspector_poly_panel import PolylineInspectorPanel
from app.ui.docks.measurement_panel import MeasurementPanel
from app.ui.docks.layer_tree_panel import LayerTreePanel
from app.ui.dialogs.layer_props_dialog import LayerPropsDialog
from app.ui.dialogs.geometry_style_dialog import GeometryStyleDialog
from app.ui.dialogs.input_ore_drive_dialog import InputOreDriveDialog

# Export utilidades
from app.core.export.obj_writer import export_layers_to_obj
from app.core.export.screenshot import save_screenshot
from app.core.export.dxf_exporter import export_polydata_to_dxf

# Herramientas de consulta
from app.core.query.query_controller import QueryController
from app.core.query.geometry_query_controller import GeometryQueryController
from app.core.query.polyline_query_controller import PolylineQueryController
from app.core.query.measurement_query_controller import MeasurementQueryController
from app.core.query.section_controller import SectionController
from app.core.camera_rotation import CameraRotationController
from app.core.Drawing import PolylineDrawingTool, CircleDrawingTool, RectangleDrawingTool, PolygonDrawingTool
from app.ui.widgets import SnapSelector

# Sistema de proyectos
from app.core.project.project_manager import ProjectManager
from app.ui.dialogs.select_polyline_dialog import SelectPolylineDialog

# Sistema de rutas
from app.core.routes import RouteManager
from app.core.entech.ore_drive_planner import (
    iter_polyline_cells,
    plan_ore_drives_for_outline,
)


# Configuración del interactor personalizado
import vtk


# Ya no necesitamos una clase personalizada, usaremos la API oficial de PyVista


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Visor")
        self.resize(1280, 800)
        
        # --- Configurar icono de la aplicación ---
        self._set_app_icon()

        # --- Visor 3D ---
        self.plotter = QtInteractor(self)
        self.setCentralWidget(self.plotter.interactor)
        self._kill_hw_selector()
        
        # --- Configurar Interactor Personalizado ANTES de cualquier otra configuración ---
        self._setup_custom_interactor()
        
        try:
            # Optimizaciones de rendimiento para muchas líneas
            self.plotter.enable_anti_aliasing()
            # Deshabilitar algunas características costosas para mejor rendimiento
            try:
                renderer = self.plotter.renderer
                if renderer:
                    # Optimizar para mejor rendimiento con muchas líneas
                    renderer.SetAutomaticLightCreation(False)
                    renderer.SetTwoSidedLighting(False)
                    renderer.SetBackfaceCulling(True)
            except Exception:
                pass
        except Exception:
            pass
        self.plotter.set_background('#1a1a1a', top="#5b5b5b")

        axes = self.plotter.add_axes(line_width=2, shaft_length=0.95, tip_length=0.15)
        for prop in (
            axes.GetXAxisCaptionActor2D().GetCaptionTextProperty(),
            axes.GetYAxisCaptionActor2D().GetCaptionTextProperty(),
            axes.GetZAxisCaptionActor2D().GetCaptionTextProperty(),
        ):
            prop.SetColor(1.0, 1.0, 1.0)
        self.plotter.render()

        # --- Escena ---
        self.scene = SceneManager(self.plotter)
        
        # --- Configurar proyección ortográfica por defecto ---
        try:
            self.scene.set_orthographic_projection(True)
        except Exception:
            pass

        # --- Sistema de Proyecto ---
        self.project = ProjectManager(scene=self.scene)

        # --- Sistema de Rutas ---
        self.route_manager = RouteManager(
            scene_manager=self.scene,
            project_manager=self.project,
            plotter=self.plotter,
            status_callback=self._query_on_status
        )


        # --- Menú Proyecto ---
        self._build_project_menu()

        # --- Ribbon superior ---
        ribbon = self._build_ribbon_tabs()
        topDock = QDockWidget("", self)
        topDock.setTitleBarWidget(QWidget())
        topDock.setWidget(ribbon)
        topDock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.addDockWidget(Qt.DockWidgetArea.TopDockWidgetArea, topDock)


        # --- Dock: Proyecto ---
        self.projectDock = QDockWidget("Proyecto", self)
        self.projectDock.setAllowedAreas(Qt.LeftDockWidgetArea | Qt.RightDockWidgetArea)
        self.layerTree = LayerTreePanel(self)
        self.layerTree.openLayerRequested.connect(self._on_tree_open_layer)
        self.layerTree.closeLayerRequested.connect(self._on_tree_close_layer)
        self.layerTree.visibilityToggled.connect(self._on_tree_visibility)
        self.layerTree.contextMenuRequested.connect(self._on_tree_context_menu)
        self.layerTree.moveLayerRequested.connect(self._on_tree_move_layer)
        self.layerTree.openGroupRequested.connect(self._on_tree_open_group)
        self.layerTree.closeGroupRequested.connect(self._on_tree_close_group)
        self.layerTree.createFolderRequested.connect(self._on_tree_create_folder)
        self.layerTree.deleteFolderRequested.connect(self._on_tree_delete_folder)
        self.layerTree.renameFolderRequested.connect(self._on_tree_rename_folder)
        self.projectDock.setWidget(self.layerTree)
        self.addDockWidget(Qt.LeftDockWidgetArea, self.projectDock)

        # --- Dock: Inspector ---
        self.inspectDock = QDockWidget("Inspector", self)
        self.inspectDock.setAllowedAreas(Qt.RightDockWidgetArea | Qt.LeftDockWidgetArea)
        self.inspector = InspectorPanel(self)
        self.polyInspector = PolylineInspectorPanel(self)
        self.measInspector = MeasurementPanel(self)
        self.inspectDock.setWidget(self.inspector)
        self.addDockWidget(Qt.RightDockWidgetArea, self.inspectDock)
        self.inspectDock.hide()

        # --- Selector de Snaps ---
        self.snap_selector = SnapSelector(self)
        self.snap_selector.snap_changed.connect(self._on_snap_changed)
        self.snap_selector.hide()  # Inicialmente oculto
        
        # Agregar selector de snaps a la barra de estado (lado derecho)
        self.statusBar().addPermanentWidget(self.snap_selector)

        # --- Controladores de consulta ---
        self.query = QueryController(
            plotter=self.plotter,
            scene=self.scene,
            on_info=self._query_on_info,
            on_status=self._query_on_status,
            on_exit=self._query_on_exit,
        )
        self.geometry_ctrl = GeometryQueryController(
            scene=self.scene,
            on_info=self._query_on_info,
            on_status=self._query_on_status,
            on_exit=lambda: self._set_query_tile(None),
        )
        self.polyline_ctrl = PolylineQueryController(
            scene=self.scene,
            on_info=self._query_on_info,
            on_status=self._query_on_status,
            on_exit=lambda: self._set_query_tile(None),
        )
        self.measure_ctrl = MeasurementQueryController(
            scene=self.scene,
            on_info=self._query_on_info,
            on_status=self._query_on_status,
            on_exit=lambda: self._set_query_tile(None),
        )
        self.camera_center_ctrl = CameraRotationController(
            scene=self.scene,
            on_status=self._query_on_status,
            on_exit=lambda: None,
            on_center=self._on_camera_centered,
        )
        self.polyline_tool = PolylineDrawingTool(
            scene=self.scene,
            on_status=self._query_on_status,
            on_commit=self._on_draw_polyline_commit,
            on_finished=self._on_draw_poly_tool_finished,
        )
        self.circle_tool = CircleDrawingTool(
            scene=self.scene,
            on_status=self._query_on_status,
            on_commit=self._on_draw_circle_commit,
            on_finished=self._on_draw_circle_finished,
        )
        self.rectangle_tool = RectangleDrawingTool(
            scene=self.scene,
            on_status=self._query_on_status,
            on_commit=self._on_draw_rectangle_commit,
            on_finished=self._on_draw_rectangle_finished,
        )
        self.polygon_tool = PolygonDrawingTool(
            scene=self.scene,
            on_status=self._query_on_status,
            on_commit=self._on_draw_polygon_commit,
            on_finished=self._on_draw_polygon_finished,
        )
        self._last_camera_center = None
        self._editing_layer_id: str | None = None
        self._editing_scene_sid: str | None = None
        self._entech_selection = None

        # Herramienta de Sección (Hito 9)
        self.section_ctrl = SectionController(
            scene=self.scene,
            on_status=self._query_on_status,
            on_plane_ready=lambda plane: self._on_section_plane_ready(plane),
            on_exit=lambda: self._on_section_exit(),
        )

        self.query_group = QButtonGroup(self)
        self.query_group.setExclusive(True)
        self.query_group.addButton(self.btn_q_block, 1)
        self.query_group.addButton(self.btn_q_geom,  2)
        self.query_group.addButton(self.btn_q_poly,  3)
        self.query_group.addButton(self.btn_q_meas,  4)

        # apaga cualquier helper de selección de PyVista antes de activar el Query
        self.btn_q_block.toggled.connect(
            lambda checked: (self._kill_hw_selector(), self.query.activate_block()) if checked else self.query.deactivate()
        )
        self.btn_q_geom.toggled.connect(self._on_geom_toggled)
        self.btn_q_poly.toggled.connect(self._on_poly_toggled)
        self.btn_q_meas.toggled.connect(self._on_meas_toggled)
        # ajustar refocus 2D cuando se activa/desactiva consulta de bloque
        self.btn_q_block.toggled.connect(self._on_block_toggled_aux)


        # Estado inicial del árbol: reflejar estado real del proyecto
        self._refresh_project_tree()

    # ================== Construcción de UI ==================

    def _build_project_menu(self):
        mb = self.menuBar()
        menu = mb.addMenu("Proyecto")

        act_new = QAction("Nuevo…", self)
        act_new.triggered.connect(self._action_new_project)
        menu.addAction(act_new)

        act_open = QAction("Abrir…", self)
        act_open.triggered.connect(self._action_open_project)
        menu.addAction(act_open)

        act_save = QAction("Guardar manifiesto", self)
        act_save.triggered.connect(lambda: self.project.save_project())
        menu.addAction(act_save)

    def _build_ribbon(self) -> QWidget:
        ribbon = QWidget(self)
        ribbon_h = QHBoxLayout(ribbon)
        ribbon_h.setContentsMargins(8, 6, 8, 6)
        ribbon_h.setSpacing(16)

        # Importar
        grp_import = QGroupBox("Importar", self)
        gimp_w = QWidget(grp_import); gimp_h = QHBoxLayout(gimp_w)
        gimp_h.setContentsMargins(8, 8, 8, 8); gimp_h.setSpacing(12)
        btn_dxf = self._mk_tool_button("DXF", Icons.dxf(), self.load_dxf_into_project)
        btn_obj = self._mk_tool_button("OBJ", Icons.obj(), self.load_obj_into_project)
        btn_bm  = self._mk_tool_button("BM CSV", Icons.blockmodel(), self.load_bm_into_project)
        btn_dh  = self._mk_tool_button("Taladros", Icons.drillholes(), self.load_dh_into_project)
        gimp_h.addWidget(btn_dxf); gimp_h.addWidget(btn_obj); gimp_h.addWidget(btn_bm); gimp_h.addWidget(btn_dh)
        grp_import.setLayout(QVBoxLayout()); grp_import.layout().addWidget(gimp_w)

        # Vistas
        grp_views = QGroupBox("Vistas", self)
        gvw_w = QWidget(grp_views); gvw_h = QHBoxLayout(gvw_w)
        gvw_h.setContentsMargins(8, 8, 8, 8); gvw_h.setSpacing(12)
        btn_top   = self._mk_tool_button("Planta", Icons.planta(), self.view_planta)
        btn_north = self._mk_tool_button("Norte",  Icons.norte(),  self.view_norte)
        btn_east  = self._mk_tool_button("Este",   Icons.este(),   self.view_este)
        self.btn_view_center = self._mk_tool_button("Centrar", Icons.view_center(), self.center_on_point)
        for b in (btn_top, btn_north, btn_east, self.btn_view_center): gvw_h.addWidget(b)
        grp_views.setLayout(QVBoxLayout()); grp_views.layout().addWidget(gvw_w)


        # Consultar
        grp_query = QGroupBox("Consultar", self)
        gq_w = QWidget(grp_query); gq_h = QHBoxLayout(gq_w)
        gq_h.setContentsMargins(8, 8, 8, 8); gq_h.setSpacing(12)
        self.btn_q_block = self._mk_toggle_tool_button("Bloque", Icons.query_block())
        self.btn_q_geom  = self._mk_toggle_tool_button("Superficies", Icons.query_geom())
        self.btn_q_poly  = self._mk_toggle_tool_button("Polilíneas", QIcon())
        self.btn_q_meas  = self._mk_toggle_tool_button("Medir", Icons.query_measure())
        for b in (self.btn_q_block, self.btn_q_geom, self.btn_q_poly, self.btn_q_meas):
            b.setStyleSheet("QToolButton:checked{background:rgba(255,255,255,0.08);border:1px solid #777;border-radius:6px;}")
            gq_h.addWidget(b)
        grp_query.setLayout(QVBoxLayout()); grp_query.layout().addWidget(gq_w)
        try:
            # Asegurar icono para 'Polilíneas'
            if hasattr(self, 'btn_q_poly'):
                self.btn_q_poly.setIcon(Icons.query_poly())
        except Exception:
            pass

        # Sección (Hito 9)
        grp_section = QGroupBox("Sección", self)
        gsec_w = QWidget(grp_section); gsec_h = QHBoxLayout(gsec_w)
        gsec_h.setContentsMargins(8, 8, 8, 8); gsec_h.setSpacing(12)
        self.btn_sec_2pt = self._mk_toggle_tool_button("2 puntos", Icons.section_2pt())
        self.btn_sec_3pt = self._mk_toggle_tool_button("3 puntos", Icons.section_3pt())
        self.btn_sec_view2d = self._mk_toggle_tool_button("2D", Icons.view2d())
        self.btn_sec_view3d = self._mk_toggle_tool_button("3D", Icons.view3d())
        for b in (self.btn_sec_2pt, self.btn_sec_3pt):
            b.setStyleSheet("QToolButton:checked{background:rgba(255,255,255,0.08);border:1px solid #777;border-radius:6px;}")
            gsec_h.addWidget(b)
        gsec_h.addWidget(self.btn_sec_view2d); gsec_h.addWidget(self.btn_sec_view3d)
        # Grupo de modo de vista (2D/3D)
        self.view_mode_group = QButtonGroup(self)
        self.view_mode_group.setExclusive(True)
        self.view_mode_group.addButton(self.btn_sec_view2d, 2)
        self.view_mode_group.addButton(self.btn_sec_view3d, 3)
        # wiring de toggles: ejecutar acciones solo al activar
        self.btn_sec_view2d.toggled.connect(lambda checked: (checked and self.view_section_2d()))
        self.btn_sec_view3d.toggled.connect(lambda checked: (checked and self.view_3d_reset()))
        # estado inicial: 3D
        try: self.btn_sec_view3d.setChecked(True)
        except Exception: pass
        # wiring
        self.btn_sec_2pt.toggled.connect(self._on_sec_2pt_toggled)
        self.btn_sec_3pt.toggled.connect(self._on_sec_3pt_toggled)
        grp_section.setLayout(QVBoxLayout()); grp_section.layout().addWidget(gsec_w)

        # Exportar
        grp_export = QGroupBox("Exportar", self)
        gexp_w = QWidget(grp_export); gexp_h = QHBoxLayout(gexp_w)
        gexp_h.setContentsMargins(8, 8, 8, 8); gexp_h.setSpacing(12)
        btn_exp_obj = self._mk_tool_button("OBJ", Icons.obj(), self.export_obj_dialog)
        btn_exp_png = self._mk_tool_button("Pantalla", Icons.screenshot(), self.export_screen_dialog)
        btn_exp_dxf = self._mk_tool_button("Exportar DXF", Icons.dxf(), self._export_layer_to_dxf)
        for b in (btn_exp_obj, btn_exp_png, btn_exp_dxf): gexp_h.addWidget(b)
        grp_export.setLayout(QVBoxLayout()); grp_export.layout().addWidget(gexp_w)

        for g in (grp_import, grp_views, grp_query, grp_section, grp_export):
            ribbon_h.addWidget(g)
        ribbon_h.addStretch(1)
        return ribbon

    def _build_ribbon_tabs(self) -> QWidget:
        """Crea un QTabWidget con dos pestañas: Principal y Modelo de Bloques.
        - Principal: reutiliza el ribbon existente (_build_ribbon).
        - Modelo de Bloques: botones placeholder para futuras herramientas.
        """
        tabs = QTabWidget(self)
        tabs.setDocumentMode(True)
        try:
            tabs.setStyleSheet(
                """
                QTabWidget::pane { border: 0; background: #e9edf5; }
                QTabBar::tab { background: #d7dbe6; color: #1f2937; padding: 6px 12px; margin: 2px; border-top-left-radius: 6px; border-top-right-radius: 6px; }
                QTabBar::tab:selected { background: #ffffff; }
                QGroupBox { background: #f6f7fb; border: 1px solid #cfd6e4; border-radius: 8px; margin-top: 18px; }
                QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0px 6px; color: #4b5563; }
                QToolButton { padding: 6px 10px; }
                """
            )
        except Exception:
            pass

        # Tab Principal (contenido actual)
        tab_main = QWidget(self)
        lm = QVBoxLayout(tab_main)
        lm.setContentsMargins(0, 0, 0, 0)
        lm.setSpacing(0)
        lm.addWidget(self._build_ribbon())
        tabs.addTab(tab_main, "Principal")

        # Tab Dibujar
        tab_draw = QWidget(self)
        draw_h = QHBoxLayout(tab_draw)
        draw_h.setContentsMargins(8, 6, 8, 6)
        draw_h.setSpacing(16)

        grp_draw_tools = QGroupBox("Dibujo", self)
        gdt_w = QWidget(grp_draw_tools); gdt_h = QHBoxLayout(gdt_w)
        gdt_h.setContentsMargins(12, 12, 12, 12); gdt_h.setSpacing(16)
        self.btn_draw_polyline = self._mk_toggle_tool_button("Polylinea", Icons.draw_polyline())
        self.btn_draw_polyline.toggled.connect(self._on_draw_polyline_toggled)
        gdt_h.addWidget(self.btn_draw_polyline)
        
        self.btn_draw_polygon = self._mk_toggle_tool_button("Poligono", Icons.draw_polygon())
        self.btn_draw_polygon.toggled.connect(self._on_draw_polygon_toggled)
        gdt_h.addWidget(self.btn_draw_polygon)
        
        self.btn_draw_circle = self._mk_toggle_tool_button("Circulo", Icons.draw_circle())
        self.btn_draw_circle.toggled.connect(self._on_draw_circle_toggled)
        gdt_h.addWidget(self.btn_draw_circle)
        
        self.btn_draw_rectangle = self._mk_toggle_tool_button("Rectangulo", Icons.draw_rectangle())
        self.btn_draw_rectangle.toggled.connect(self._on_draw_rectangle_toggled)
        gdt_h.addWidget(self.btn_draw_rectangle)
        
        gdt_h.addStretch(1)
        grp_draw_tools.setLayout(QVBoxLayout()); grp_draw_tools.layout().addWidget(gdt_w)

        draw_h.addWidget(grp_draw_tools)
        
        # Grupo de herramientas de edición
        grp_edit_tools = QGroupBox("Editar", self)
        get_w = QWidget(grp_edit_tools); get_h = QHBoxLayout(get_w)
        get_h.setContentsMargins(12, 12, 12, 12); get_h.setSpacing(16)
        
        self.btn_edit_select = self._mk_toggle_tool_button("Seleccionar", Icons.edit_select())
        self.btn_edit_select.toggled.connect(self._on_edit_select_toggled)
        get_h.addWidget(self.btn_edit_select)
        
        self.btn_edit_delete = self._mk_toggle_tool_button("Eliminar", Icons.edit_delete())
        self.btn_edit_delete.toggled.connect(self._on_edit_delete_toggled)
        get_h.addWidget(self.btn_edit_delete)
        
        self.btn_edit_copy = self._mk_toggle_tool_button("Copiar", Icons.edit_copy())
        self.btn_edit_copy.toggled.connect(self._on_edit_copy_toggled)
        get_h.addWidget(self.btn_edit_copy)
        
        self.btn_edit_move = self._mk_toggle_tool_button("Mover", Icons.edit_move())
        self.btn_edit_move.toggled.connect(self._on_edit_move_toggled)
        get_h.addWidget(self.btn_edit_move)
        
        get_h.addStretch(1)
        grp_edit_tools.setLayout(QVBoxLayout()); grp_edit_tools.layout().addWidget(get_w)
        
        draw_h.addWidget(grp_edit_tools)
        draw_h.addStretch(1)

        tabs.addTab(tab_draw, "Dibujar")

        # Tab Modelo de Bloques (agrupado según petición)
        tab_bm = QWidget(self)
        bm_h = QHBoxLayout(tab_bm)
        bm_h.setContentsMargins(8, 6, 8, 6)
        bm_h.setSpacing(16)

        # Gestión (Campos + Topografia)
        grp_manage = QGroupBox("Gestion", self)
        gm_w = QWidget(grp_manage); gm_h = QHBoxLayout(gm_w)
        gm_h.setContentsMargins(8, 8, 8, 8); gm_h.setSpacing(12)
        btn_manage_fields = self._mk_tool_button("Campos", Icons.bm_field_add(), self._bm_manage_fields)
        btn_clip_topo = self._mk_tool_button("Topografia", Icons.bm_clip_topo(), self._bm_clip_topography)
        gm_h.addWidget(btn_manage_fields); gm_h.addWidget(btn_clip_topo)
        grp_manage.setLayout(QVBoxLayout()); grp_manage.layout().addWidget(gm_w)

        # Reportar (Estadisticas + Solidos)
        grp_report = QGroupBox("Reportar", self)
        gr_w = QWidget(grp_report); gr_h = QHBoxLayout(gr_w)
        gr_h.setContentsMargins(8, 8, 8, 8); gr_h.setSpacing(12)
        btn_stats = self._mk_tool_button("Estadisticas", Icons.bm_stats(), self._bm_show_stats)
        btn_report = self._mk_tool_button("Solidos", Icons.bm_report_solids(), self._bm_report_tonnage)
        gr_h.addWidget(btn_stats); gr_h.addWidget(btn_report)
        grp_report.setLayout(QVBoxLayout()); grp_report.layout().addWidget(gr_w)

        # Validacion
        grp_validate = QGroupBox("Validacion", self)
        gv_w = QWidget(grp_validate); gv_h = QHBoxLayout(gv_w)
        gv_h.setContentsMargins(8, 8, 8, 8); gv_h.setSpacing(12)
        btn_validate = self._mk_tool_button("Revisar", Icons.bm_validate(), self._bm_validate)
        gv_h.addWidget(btn_validate)
        grp_validate.setLayout(QVBoxLayout()); grp_validate.layout().addWidget(gv_w)

        for g in (grp_manage, grp_report, grp_validate):
            bm_h.addWidget(g)
        bm_h.addStretch(1)

        tabs.addTab(tab_bm, "Modelo de Bloques")

        # Tab Editar
        tab_edit = QWidget(self)
        ed_h = QHBoxLayout(tab_edit)
        ed_h.setContentsMargins(8, 6, 8, 6)
        ed_h.setSpacing(16)

        grp_surf = QGroupBox("Superficies", self)
        gs_w = QWidget(grp_surf); gs_h = QHBoxLayout(gs_w)
        gs_h.setContentsMargins(8, 8, 8, 8); gs_h.setSpacing(12)
        btn_scale = self._mk_tool_button("Escalar", Icons.edit_scale(), self._edit_scale_surface)
        gs_h.addWidget(btn_scale)
        grp_surf.setLayout(QVBoxLayout()); grp_surf.layout().addWidget(gs_w)

        ed_h.addWidget(grp_surf)
        ed_h.addStretch(1)

        tabs.addTab(tab_edit, "Editar")

        # Tab Equipos
        tab_eq = QWidget(self)
        eq_h = QHBoxLayout(tab_eq)
        eq_h.setContentsMargins(8, 6, 8, 6)
        eq_h.setSpacing(16)

        grp_routes = QGroupBox("Rutas", self)
        gr_w = QWidget(grp_routes); gr_h = QHBoxLayout(gr_w)
        gr_h.setContentsMargins(8, 8, 8, 8); gr_h.setSpacing(12)
        btn_eq_add = self._mk_tool_button("Agregar", Icons.query_poly(), self._equip_manage_routes)
        btn_eq_clear = self._mk_tool_button("Limpiar", Icons.clear(), self._equip_clear)
        btn_eq_play = self._mk_tool_button("Play", Icons.play(), self._equip_play)
        btn_eq_pause = self._mk_tool_button("Pause", Icons.pause(), self._equip_pause)
        btn_eq_stop = self._mk_tool_button("Stop", Icons.stop(), self._equip_stop)
        for b in (btn_eq_add, btn_eq_clear, btn_eq_play, btn_eq_pause, btn_eq_stop): gr_h.addWidget(b)
        grp_routes.setLayout(QVBoxLayout()); grp_routes.layout().addWidget(gr_w)

        eq_h.addWidget(grp_routes)
        eq_h.addStretch(1)

        tabs.addTab(tab_eq, "Equipos")

        # Tab Entech
        tab_entech = QWidget(self)
        entech_h = QHBoxLayout(tab_entech)
        entech_h.setContentsMargins(8, 6, 8, 6)
        entech_h.setSpacing(16)

        grp_ore_drive = QGroupBox("Ore Drive", self)
        god_w = QWidget(grp_ore_drive); god_h = QHBoxLayout(god_w)
        god_h.setContentsMargins(8, 8, 8, 8); god_h.setSpacing(12)
        btn_determine_points = self._mk_tool_button(
            "Determine Points",
            Icons.determine_points(),
            self._entech_determine_points,
        )
        god_h.addWidget(btn_determine_points)
        grp_ore_drive.setLayout(QVBoxLayout()); grp_ore_drive.layout().addWidget(god_w)

        entech_h.addWidget(grp_ore_drive)
        entech_h.addStretch(1)

        tabs.addTab(tab_entech, "Entech")

        # guardar referencia
        self.ribbon_tabs = tabs
        return tabs

    # -------- Placeholders: Modelo de Bloques --------
    def _bm_placeholder(self, title: str):
        try:
            QMessageBox.information(self, "Modelo de Bloques", f"{title}: en construccion")
        except Exception:
            pass

    def _bm_manage_fields(self):
        """Abrir ventana de gestión de campos"""
        from app.ui.dialogs.bm_editor_window import BMEditorWindow
        window = BMEditorWindow(self.project, self.scene, self)
        window.show()

    def _bm_clip_topography(self):
        self._bm_placeholder("Recortar con topografia")

    def _bm_show_stats(self):
        """Abrir ventana de estadísticas descriptivas"""
        from app.ui.dialogs.bm_stats_window import BMStatsWindow
        window = BMStatsWindow(self.project, self.scene, self)
        window.show()

    def _bm_report_tonnage(self):
        """Abrir diálogo de reporte de tonelaje dentro de sólido"""
        from app.core.query_bm.solid_selector_dialog import SolidSelectorDialog
        
        dialog = SolidSelectorDialog(self.project, self.scene, self)
        dialog.report_requested.connect(self._on_tonnage_report_requested)
        dialog.exec()
    
    def _on_tonnage_report_requested(self, solid_path: str, model_id: str, 
                                   density_field: str, grade_fields: list, breakdown_field: str):
        """Manejador para cuando se solicita un reporte de tonelaje"""
        from app.core.query_bm.solid_tonnage_report import SolidTonnageReport
        
        window = SolidTonnageReport(
            self.project, self.scene, solid_path, model_id, 
            density_field, grade_fields, breakdown_field, self
        )
        window.show()

    def _bm_validate(self):
        self._bm_placeholder("Validacion")

    # ================== Editar: Superficies ==================
    def _edit_scale_surface(self):
        """Selecciona una superficie y aplica un factor de escala uniforme."""
        try:
            from app.ui.dialogs.select_solid_dialog import SelectSolidDialog
            dlg = SelectSolidDialog(self, self.project)
            if dlg.exec() != dlg.DialogCode.Accepted or not dlg.selected_layer_id:
                return
            layer_id = dlg.selected_layer_id
            layer_name = layer_id
            try:
                for m in self.project.manifest.layers:
                    if m.id == layer_id:
                        layer_name = m.name; break
            except Exception:
                pass
        except Exception:
            QMessageBox.warning(self, "Escalar", "No se pudo abrir el selector de superficies.")
            return

        # Importar diálogo aquí para evitar dependencias circulares en el arranque
        try:
            from app.ui.dialogs.scale_surface_dialog import ScaleSurfaceDialog
        except Exception:
            QMessageBox.warning(self, "Escalar", "No se pudo cargar el diálogo de escala.")
            return
        sd = ScaleSurfaceDialog(layer_name, self)
        if sd.exec() != sd.DialogCode.Accepted:
            return
        factor = sd.factor()
        if abs(factor - 1.0) < 1e-9:
            return

        try:
            sid = None
            try:
                sid = self.project._active_layers.get(layer_id)
            except Exception:
                sid = None
            if not sid:
                sid = self.project.activate_layer(layer_id, show=True)
            info = self.scene.layers.get(sid)
            if not info:
                QMessageBox.warning(self, "Escalar", "No se pudo acceder a la superficie seleccionada.")
                return
            for key in ("dataset", "orig_dataset"):
                ds = info.get(key)
                try:
                    if ds is None or not hasattr(ds, 'points'):
                        continue
                    c = ds.center
                    ds.translate((-c[0], -c[1], -c[2]), inplace=True)
                    ds.scale(factor, inplace=True)
                    ds.translate((c[0], c[1], c[2]), inplace=True)
                except Exception:
                    try:
                        new_ds = ds.copy(deep=True)
                        c = new_ds.center
                        new_ds.translate((-c[0], -c[1], -c[2]), inplace=True)
                        new_ds.scale(factor, inplace=True)
                        new_ds.translate((c[0], c[1], c[2]), inplace=True)
                        info[key] = new_ds
                    except Exception:
                        pass
            try:
                self.scene.plotter.render()
                self.statusBar().showMessage(f"Superficie '{layer_name}' escalada x{factor:.3f}")
            except Exception:
                pass
        except Exception as e:
            QMessageBox.warning(self, "Escalar", f"No se pudo escalar la superficie:\n{e}")

    # ================== Equipos: Rutas ==================
    def _equip_manage_routes(self):
        """Abrir el gestor de rutas (lista + CRUD + cargar)."""
        try:
            from app.ui.dialogs.routes_manager_dialog import RoutesManagerDialog
            proj_root = getattr(self.project, 'root', None)
            dlg = RoutesManagerDialog(proj_root, self)
            dlg.exec()
        except Exception as e:
            QMessageBox.warning(self, "Rutas", f"No se pudo abrir el gestor de rutas:\n{e}")

    # Flujo anterior de agregar equipo directo queda disponible si se llama _equip_add()
    def _equip_add(self):
        """Selecciona una polilínea con el selector interactivo y un equipo OBJ, y prepara animación."""
        # Salir de modo Sección 2D si estaba activo (para no ocultar actores 3D)
        try:
            if getattr(self.scene, "_in_section_2d", False):
                self.view_3d_reset()
                self.scene.clear_section()
        except Exception:
            pass

        # Usar el RouteManager para agregar equipo
        self.route_manager.add_equipment_to_route()

    def _equip_play(self):
        """Inicia la animación de los equipos."""
        self.route_manager.play_animation()

    def _equip_pause(self):
        """Pausa la animación de los equipos."""
        self.route_manager.pause_animation()

    def _equip_stop(self):
        """Detiene la animación y reinicia todos los equipos al inicio."""
        self.route_manager.stop_animation()

    def _equip_clear(self):
        """Limpiar todos los equipos temporales de la escena"""
        self.route_manager.clear_equipment()


    # ================== Entech ==================
    def _entech_determine_points(self):
        """Abrir el dialogo de parametros y seleccionar una capa de outlines."""
        dlg = InputOreDriveDialog(self)
        if dlg.exec() != dlg.DialogCode.Accepted:
            self.statusBar().showMessage("Entech: configuracion cancelada.")
            return

        params = dlg.parameters()

        if not self._project_is_open():
            QMessageBox.warning(self, "Entech", "Necesitas un proyecto abierto para elegir las polilineas.")
            return

        layer_dlg = SelectPolylineDialog(self, self.project)
        if layer_dlg.exec() != layer_dlg.DialogCode.Accepted or not layer_dlg.selected_layer_id:
            self.statusBar().showMessage("Entech: no se selecciono ninguna capa de outlines.")
            return

        layer_id = layer_dlg.selected_layer_id
        layer_name = self._layer_name_from_manifest(layer_id)

        self._entech_selection = {
            "params": params,
            "layer_id": layer_id,
            "layer_name": layer_name,
            "result_scene_id": None,
        }

        self._entech_report_polyline_count(layer_id, layer_name, params)


    def _entech_report_polyline_count(self, layer_id: str, layer_name: str, params: dict):
        scene_id, dataset = self._entech_prepare_layer_dataset(layer_id)
        if dataset is None:
            msg = "Entech: no se pudo acceder a la geometria de la capa seleccionada."
            self.statusBar().showMessage(msg)
            try:
                QMessageBox.warning(self, "Entech", msg)
            except Exception:
                pass
            return

        count = self._entech_count_polylines_in_dataset(dataset)
        if count <= 0:
            msg = f"La capa '{layer_name}' no contiene polilineas."
            self.statusBar().showMessage(msg)
            try:
                QMessageBox.information(self, "Entech", msg)
            except Exception:
                pass
            return

        if self._entech_selection is not None:
            self._entech_selection["polyline_count"] = count
            self._entech_selection["scene_layer_id"] = scene_id

        msg = f"La capa '{layer_name}' contiene {count} polilineas."
        try:
            QMessageBox.information(self, "Entech", msg)
        except Exception:
            pass
        self.statusBar().showMessage(f"Entech: {msg}")

        self._entech_generate_centerlines(layer_name, params, dataset)

    def _entech_prepare_layer_dataset(self, layer_id: str):
        scene_id = None
        try:
            scene_id = getattr(self.project, "_active_layers", {}).get(layer_id)
        except Exception:
            scene_id = None
        if not scene_id:
            try:
                scene_id = self.project.activate_layer(layer_id, show=False)
            except Exception:
                return None, None

        layer_info = None
        try:
            layer_info = self.scene.layers.get(scene_id)
        except Exception:
            layer_info = None
        if not layer_info:
            return scene_id, None

        dataset = None
        for key in ("dataset", "orig_dataset", "polydata", "mesh"):
            ds = layer_info.get(key)
            if ds is not None:
                dataset = ds
                break
        if dataset is None:
            return scene_id, None

        try:
            data = pv.wrap(dataset)
            if isinstance(data, pv.MultiBlock):
                data = data.combine()
            if not isinstance(data, pv.PolyData):
                geom = data.extract_geometry()
                data = geom.cast_to_poly_data()
        except Exception:
            return scene_id, None
        return scene_id, data

    def _entech_count_polylines_in_dataset(self, dataset) -> int:
        total = 0
        for _ in iter_polyline_cells(dataset):
            total += 1
        return total

    def _entech_generate_centerlines(self, layer_name: str, params: dict, dataset):
        try:
            max_span = float(params.get("maximum_span") or 0.0)
            drive_width = float(params.get("width") or 0.0)
            drive_height = float(params.get("height") or 0.0)
            rib_pillar = float(params.get("rib_pillar") or 0.0)
        except Exception:
            max_span = 0.0
            drive_width = 0.0
            drive_height = 0.0
            rib_pillar = 0.0
        if max_span <= 0 or drive_width <= 0 or drive_height <= 0:
            msg = "Entech: valores de Maximum Span, Ancho y Alto deben ser mayores que cero."
            self.statusBar().showMessage(msg)
            try:
                QMessageBox.warning(self, "Entech", msg)
            except Exception:
                pass
            return
        if drive_width >= max_span:
            msg = "Entech: el ancho del drive debe ser menor que el Maximum Span."
            self.statusBar().showMessage(msg)
            try:
                QMessageBox.warning(self, "Entech", msg)
            except Exception:
                pass
            return

        polylines = list(iter_polyline_cells(dataset))
        total = len(polylines)
        if total == 0:
            msg = "Entech: la capa no tiene polilineas para procesar."
            self.statusBar().showMessage(msg)
            try:
                QMessageBox.information(self, "Entech", msg)
            except Exception:
                pass
            return

        progress = QProgressDialog(
            "Generando ore drives...",
            "Cancelar",
            0,
            total,
            self,
        )
        progress.setWindowTitle("Entech")
        progress.setWindowModality(Qt.WindowModality.ApplicationModal)
        progress.setMinimumDuration(0)
        progress.setValue(0)

        segments_geom = []
        points_geom = []
        warnings_accum = []
        skipped = 0
        processed = 0
        cancelled = False

        for idx, pts in enumerate(polylines, start=1):
            if progress.wasCanceled():
                cancelled = True
                break
            progress.setValue(idx)
            QApplication.processEvents()
            processed += 1
            rounded = np.unique(np.round(pts, 6), axis=0)
            if rounded.shape[0] != 4:
                skipped += 1
                continue
            placements, warns = plan_ore_drives_for_outline(
                pts,
                drive_width=drive_width,
                drive_height=drive_height,
                max_span=max_span,
                rib_pillar=rib_pillar,
            )
            if warns:
                warnings_accum.extend(warns)
            if not placements:
                skipped += 1
                continue
            for placement in placements:
                if placement.kind == "segment":
                    segments_geom.append(placement.points)
                else:
                    points_geom.append(placement.points[0])

        progress.setValue(total)
        progress.close()

        if cancelled:
            msg = "Entech: generacion cancelada por el usuario."
            self.statusBar().showMessage(msg)
            try:
                QMessageBox.information(self, "Entech", msg)
            except Exception:
                pass
            return

        if not segments_geom and not points_geom:
            msg = "Entech: no se generaron ubicaciones validas."
            self.statusBar().showMessage(msg)
            try:
                QMessageBox.warning(self, "Entech", msg)
            except Exception:
                pass
            return

        points = []
        lines = []
        verts = []
        current_idx = 0
        for seg in segments_geom:
            points.extend(seg.tolist())
            lines.extend([2, current_idx, current_idx + 1])
            current_idx += 2
        for pt in points_geom:
            points.append(pt.tolist())
            verts.extend([1, current_idx])
            current_idx += 1

        result_poly = pv.PolyData()
        result_poly.points = np.asarray(points, dtype=float)
        if lines:
            result_poly.lines = np.asarray(lines, dtype=np.int64)
        if verts:
            result_poly.verts = np.asarray(verts, dtype=np.int64)

        out_name = params.get("layer_name") or f"Ore Drive Centerlines - {layer_name}"
        project_layer_id = None
        result_layer_id = None

        if self._project_is_open():
            try:
                meta = self.project.add_generated_polydata(
                    result_poly,
                    out_name,
                    group_path=self._target_group_from_selection(),
                )
                project_layer_id = meta.id
                try:
                    result_layer_id = self.project.activate_layer(meta.id, show=True)
                except Exception:
                    result_layer_id = None
                if result_layer_id:
                    try:
                        self.scene.set_color(result_layer_id, (0.95, 0.45, 0.05))
                    except Exception:
                        pass
                self._refresh_project_tree()
            except Exception as exc:
                try:
                    QMessageBox.warning(self, "Entech", f"No se pudo registrar la capa generada:\n{exc}")
                except Exception:
                    pass

        if result_layer_id is None:
            result_layer_id = self.scene.add_polydata(result_poly, name=out_name)
            try:
                self.scene.set_color(result_layer_id, (0.95, 0.45, 0.05))
            except Exception:
                pass

        if self._entech_selection is not None:
            self._entech_selection["result_layer_id"] = project_layer_id or result_layer_id
            self._entech_selection["result_scene_id"] = result_layer_id

        placements_count = len(segments_geom) + len(points_geom)
        msg = (
            f"Se generaron {placements_count} ubicaciones (segmentos: {len(segments_geom)}, puntos: {len(points_geom)}) "
            f"a partir de {processed} outlines ({skipped} omitidos)."
        )
        self.statusBar().showMessage(f"Entech: {msg}")
        try:
            QMessageBox.information(self, "Entech", msg)
        except Exception:
            pass

        if warnings_accum:
            unique_warns = []
            for w in warnings_accum:
                if w not in unique_warns:
                    unique_warns.append(w)
            warn_text = "\n".join(unique_warns)
            try:
                QMessageBox.warning(self, "Entech", warn_text)
            except Exception:
                pass

    def _export_layer_to_dxf(self):
        if not hasattr(self.scene, "layers") or not self.scene.layers:
            QMessageBox.information(self, "Exportar DXF", "No hay capas cargadas en la escena para exportar.")
            return
        layer_items = []
        layer_names = []
        for sid, info in self.scene.layers.items():
            name = (info.get("name") if isinstance(info, dict) else None) or sid
            layer_items.append((name, sid))
            layer_names.append(name)
        if not layer_items:
            QMessageBox.information(self, "Exportar DXF", "No hay capas disponibles para exportar.")
            return
        item, ok = QInputDialog.getItem(
            self,
            "Seleccionar capa",
            "Exportar a DXF:",
            layer_names,
            0,
            False,
        )
        if not ok or not item:
            return
        index = layer_names.index(item)
        _, scene_id = layer_items[index]
        layer_info = self.scene.layers.get(scene_id)
        if not isinstance(layer_info, dict):
            QMessageBox.warning(self, "Exportar DXF", "No se pudo acceder a la capa seleccionada.")
            return
        dataset = None
        for key in ("dataset", "polydata", "dataset_vis", "orig_dataset"):
            ds = layer_info.get(key)
            if ds is not None:
                dataset = ds
                break
        if dataset is None:
            QMessageBox.warning(self, "Exportar DXF", "La capa seleccionada no tiene geometría disponible para exportar.")
            return
        path, _ = QFileDialog.getSaveFileName(self, "Exportar capa a DXF", item, "DXF (*.dxf);;Todos (*.*)")
        if not path:
            return
        try:
            data = pv.wrap(dataset) if not isinstance(dataset, pv.PolyData) else dataset
            if not isinstance(data, pv.PolyData):
                data = data.extract_geometry()
                data = data.cast_to_poly_data()
            export_polydata_to_dxf(data.copy(deep=True), path)
            QMessageBox.information(self, "Exportar DXF", f"DXF exportado: {path}")
        except Exception as exc:
            QMessageBox.warning(self, "Exportar DXF", f"No se pudo exportar a DXF:\n{exc}")

    # ================== Importar Taladros ==================
    def load_dh_into_project(self):
        from PyQt6.QtWidgets import QMessageBox
        from app.ui.dialogs.drillhole_import_dialog import DrillholeImportDialog
        from app.core.loaders.drillhole_loader import load_drillholes_csvs

        dlg = DrillholeImportDialog(self)
        if dlg.exec() != dlg.DialogCode.Accepted:
            return
        cfg = dlg.get_configs()

        # Sin proyecto: cargar directo a escena
        if not self._project_is_open():
            try:
                poly, info = load_drillholes_csvs(
                    cfg['collar']['path'], cfg['survey']['path'],
                    (cfg.get('lithology') or {}).get('path') or None,
                    (cfg.get('assay') or {}).get('path') or None,
                    step=1.0,
                    collar_map=cfg['collar']['map'],
                    survey_map=cfg['survey']['map'],
                    lith_map=(cfg.get('lithology') or {}).get('map'),
                    assay_map=(cfg.get('assay') or {}).get('map'),
                )
            except Exception as e:
                QMessageBox.warning(self, "Taladros", f"No se pudieron cargar los CSV:\n{e}")
                return

            try:
                name = f"Taladros: {info.get('n_holes', 0)} pozos"
                layer_id = self.scene.add_mesh(
                    poly, name=name,
                    style="wireframe", render_lines_as_tubes=True, line_width=2,
                    show_edges=False,
                )
                try:
                    self.scene.set_color(layer_id, (1.0, 0.533, 0.0))
                except Exception:
                    pass
                try:
                    self.scene.zoom_to_layer(layer_id)
                except Exception:
                    pass
                self.statusBar().showMessage(f"Taladros cargados: {info.get('n_holes', 0)} agujeros, {poly.n_cells} tramos")
            except Exception as e:
                QMessageBox.warning(self, "Taladros", f"No se pudo agregar la capa:\n{e}")
            return

        # Con proyecto: registrar asset y activar visible
        try:
            maps = {
                'collar': cfg['collar']['map'],
                'survey': cfg['survey']['map'],
                'lithology': (cfg.get('lithology') or {}).get('map'),
                'assay': (cfg.get('assay') or {}).get('map'),
            }
            meta = self.project.add_drillholes_asset(
                collar_csv=cfg['collar']['path'],
                survey_csv=cfg['survey']['path'],
                lith_csv=(cfg.get('lithology') or {}).get('path'),
                assay_csv=(cfg.get('assay') or {}).get('path'),
                name='Taladros',
                maps=maps,
                step=1.0,
                group_path=self._target_group_from_selection(),
            )
            sid = self.project.activate_layer(meta.id, show=True)
            try:
                self.scene.zoom_to_layer(sid)
            except Exception:
                pass
            self.statusBar().showMessage(f"Registrado y cargado: {meta.name}")
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Taladros", f"No se pudo registrar/cargar:\n{e}")

    # ================== Gestión de Proyecto ==================

    def _project_is_open(self) -> bool:
        return getattr(self.project, "root", None) is not None

    def _layer_name_from_manifest(self, layer_id: str) -> str:
        try:
            for layer in getattr(getattr(self.project, 'manifest', None), 'layers', []) or []:
                if getattr(layer, 'id', None) == layer_id:
                    return getattr(layer, 'name', layer_id)
        except Exception:
            pass
        return layer_id

    def _refresh_project_tree(self):
        try:
            layers = [asdict(x) for x in self.project.list_layers()]
            folders = self.project.list_folders()
        except Exception:
            layers, folders = [], []

        self._editing_layer_id = next((m['id'] for m in layers if m.get('state') == 'editing'), None) if layers else None
        self._editing_scene_sid = None
        if self._editing_layer_id:
            try:
                active_map = getattr(self.project, '_active_layers', {})
                self._editing_scene_sid = active_map.get(self._editing_layer_id)
            except Exception:
                self._editing_scene_sid = None
        self.layerTree.populate(layers, folders=folders)

    def _action_new_project(self):
        root = QFileDialog.getExistingDirectory(self, "Seleccionar carpeta vacía para el proyecto")
        if not root: return
        name = os.path.basename(root.rstrip("/\\")) or f"Proyecto_{int(time.time())}"
        try:
            self.project.new_project(root, project_name=name)
            self.statusBar().showMessage(f"Proyecto creado en: {root}")
        except Exception as e:
            QMessageBox.warning(self, "Proyecto", f"No se pudo crear el proyecto:\n{e}")
            return
        self._refresh_project_tree()

    def _action_open_project(self):
        root = QFileDialog.getExistingDirectory(self, "Abrir proyecto (carpeta con vmproj.json)")
        if not root: return
        try:
            self.project.open_project(root)
            self.statusBar().showMessage(f"Proyecto abierto: {root}")
        except Exception as e:
            QMessageBox.warning(self, "Proyecto", f"No se pudo abrir el proyecto:\n{e}")
            return
        self._refresh_project_tree()

    def _ensure_project_or_prompt(self) -> bool:
        if self._project_is_open():
            return True
        resp = QMessageBox.question(
            self, "Proyecto requerido",
            "Para registrar archivos como 'Cerrado' y abrirlos luego, necesitas un proyecto.\n\n"
            "¿Quieres inicializar uno ahora en una carpeta vacía?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if resp == QMessageBox.StandardButton.Yes:
            self._action_new_project()
            return self._project_is_open()
        return False

    # ================== Importar (al Proyecto) ==================

    def _target_group_from_selection(self) -> str:
        try:
            return self.layerTree.get_selected_group_path() or ""
        except Exception:
            return ""

    def load_obj_into_project(self):
        path, _ = QFileDialog.getOpenFileName(self, "Seleccionar OBJ", "", "OBJ (*.obj);;Todos (*.*)")
        if not path: return
        if not self._ensure_project_or_prompt():
            mesh = load_obj_mesh(path)
            layer_id = self.scene.add_mesh(mesh, name=f"OBJ: {os.path.basename(path)}")
            # No auto-zoom al cargar sin proyecto
            self.statusBar().showMessage("OBJ cargado (sin proyecto).")
            return
        try:
            meta = self.project.add_asset(
                path, layer_type="obj", name=os.path.basename(path),
                group_path=self._target_group_from_selection()
            )
            self.statusBar().showMessage(f"Registrado en proyecto (Cerrado): {meta.name}")
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "OBJ", f"No se pudo registrar el archivo en el proyecto:\n{e}")

    def load_dxf_into_project(self):
        path, _ = QFileDialog.getOpenFileName(self, "Seleccionar DXF", "", "DXF (*.dxf);;Todos (*.*)")
        if not path: return
        seleccionadas = []
        try:
            capas = list_dxf_layers(path)
            if capas:
                dlg = DXFLayerSelectDialog(self, capas)
                if dlg.exec() != dlg.DialogCode.Accepted:
                    return
                seleccionadas = dlg.selected_layers()
                if not seleccionadas:
                    QMessageBox.information(self, "DXF", "No se seleccionó ninguna capa.")
                    return
        except Exception as e:
            QMessageBox.warning(self, "DXF", f"No se pudo leer las capas del DXF:\n{e}")
            return

        if not self._ensure_project_or_prompt():
            try:
                poly = load_dxf_filtered_as_polydata_nuevo(path, include_layers=seleccionadas or [])
                name = f"DXF: {os.path.basename(path)}"
                layer_id = self.scene.add_polydata(poly, name=name)
                # No auto-zoom al cargar sin proyecto
                self.statusBar().showMessage("DXF cargado (sin proyecto).")
            except Exception as e:
                QMessageBox.warning(self, "DXF", f"No se pudo convertir el DXF:\n{e}")
            return

        try:
            meta = self.project.add_asset(
                path, layer_type="dxf",
                name=os.path.basename(path),
                loader_opts={"layers": seleccionadas},
                group_path=self._target_group_from_selection()
            )
            self.statusBar().showMessage(f"Registrado en proyecto (Cerrado): {meta.name}")
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "DXF", f"No se pudo registrar el archivo en el proyecto:\n{e}")

    def load_bm_into_project(self):
        path, _ = QFileDialog.getOpenFileName(self, "Seleccionar Block Model CSV", "", "CSV (*.csv);;Todos (*.*)")
        if not path: return
        if not self._ensure_project_or_prompt():
            try:
                dataset, info = load_block_model_csv(path, prefer_image=True)
                base = os.path.basename(path); name = f"BM CSV: {base}"
                if info.get("representation") == "image":
                    layer_id = self.scene.add_mesh(dataset, name=name)
                else:
                    layer_id = self.scene.add_mesh(
                        dataset, name=name,
                        style="points", render_points_as_spheres=True, point_size=4, show_edges=False,
                    )
                # No auto-zoom al cargar sin proyecto
                self.statusBar().showMessage("BM CSV cargado (sin proyecto).")
            except Exception as e:
                QMessageBox.warning(self, "Block Model CSV", f"No se pudo cargar el CSV:\n{e}")
            return

        try:
            meta = self.project.add_asset(
                path, layer_type="blockmodel", name=os.path.basename(path),
                group_path=self._target_group_from_selection()
            )
            self.statusBar().showMessage(f"Registrado en proyecto (Cerrado): {meta.name}")
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Block Model CSV", f"No se pudo registrar el archivo en el proyecto:\n{e}")

    # ================== Árbol: handlers ==================

    def _on_tree_open_layer(self, layer_id: str, show: bool):
        try:
            sid = self.project.activate_layer(layer_id, show=show)
            # No auto-zoom al abrir capas; usar 'Enfocar' cuando se requiera
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Abrir capa", f"No se pudo abrir la capa:\n{e}")

    def _on_tree_close_layer(self, layer_id: str, unload: bool):
        if self._editing_layer_id == layer_id:
            try:
                self.project.set_layer_editing(layer_id, False)
            except Exception as e:
                QMessageBox.warning(self, "Editar capa", f"No se pudo salir del modo edicion:\n{e}")
            self._editing_layer_id = None
        try:
            self.project.deactivate_layer(layer_id, unload=unload)
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Cerrar capa", f"No se pudo cerrar la capa:\n{e}")

    def _on_tree_visibility(self, layer_id: str, visible: bool):
        if not visible and self._editing_layer_id == layer_id:
            try:
                self.project.set_layer_editing(layer_id, False)
            except Exception as e:
                QMessageBox.warning(self, "Editar capa", f"No se pudo salir del modo edicion:\n{e}")
                return
            self._editing_layer_id = None
        try:
            self.project.set_layer_visibility(layer_id, visible)
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Visibilidad", f"No se pudo cambiar la visibilidad:\n{e}")

    def _on_tree_open_group(self, gpath: str):
        try:
            for m in self.project.list_layers():
                gp = (m.group_path or "").strip("/")
                if gp == gpath or gp.startswith(gpath + "/"):
                    self.project.activate_layer(m.id, show=True)
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Abrir carpeta", f"No se pudo abrir la carpeta:\n{e}")

    def _on_tree_close_group(self, gpath: str, unload: bool):
        try:
            for m in self.project.list_layers():
                gp = (m.group_path or "").strip("/")
                if gp == gpath or gp.startswith(gpath + "/"):
                    self.project.deactivate_layer(m.id, unload=unload)
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Cerrar carpeta", f"No se pudo cerrar la carpeta:\n{e}")

    def _on_tree_create_folder(self, parent_gpath: str):
        name, ok = QInputDialog.getText(self, "Nueva carpeta", "Nombre:")
        if not ok or not name.strip(): return
        try:
            self.project.create_folder(parent_gpath, name.strip())
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Nueva carpeta", f"No se pudo crear la carpeta:\n{e}")

    def _on_tree_delete_folder(self, gpath: str):
        try:
            if self.project.group_has_layers(gpath):
                QMessageBox.information(self, "Eliminar carpeta",
                                        "La carpeta no está vacía. Mueve o elimina las capas primero.")
                return
            resp = QMessageBox.question(self, "Eliminar carpeta",
                                        f"¿Eliminar la carpeta '{gpath}' del proyecto?",
                                        QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
            if resp != QMessageBox.StandardButton.Yes: return
            self.project.remove_folder(gpath)
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Eliminar carpeta", f"No se pudo eliminar la carpeta:\n{e}")

    def _on_tree_rename_folder(self, gpath: str):
        leaf = gpath.split("/")[-1] if gpath else ""
        new_name, ok = QInputDialog.getText(self, "Renombrar carpeta", "Nuevo nombre:", text=leaf)
        if not ok or not new_name.strip(): return
        try:
            self.project.rename_folder(gpath, new_name.strip())
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Renombrar carpeta", f"No se pudo renombrar la carpeta:\n{e}")

    def _on_tree_move_layer(self, layer_id: str, new_group_path: str):
        try:
            self.project.set_group_path(layer_id, new_group_path or "")
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Mover capa", f"No se pudo mover la capa:\n{e}")

    # --- Menú contextual de capa ---

    def _on_tree_context_menu(self, layer_id: str, layer_name: str, global_pos: QPoint):
        menu = self._build_tree_layer_menu(layer_id, layer_name)
        menu.exec(global_pos)

    def _build_tree_layer_menu(self, layer_id: str, layer_name: str) -> QMenu:
        menu = QMenu(self)
        sid = getattr(self.project, "_active_layers", {}).get(layer_id)
        state = "closed"
        try:
            meta = next(m for m in self.project.manifest.layers if m.id == layer_id)
            state = meta.state
        except Exception:
            meta = None

        # Acciones de estado/carga
        act_open = QAction("Abrir (mostrar)", self)
        act_open.triggered.connect(lambda: self._on_tree_open_layer(layer_id, True))
        act_open_hidden = QAction("Abrir sin mostrar", self)
        act_open_hidden.triggered.connect(lambda: self._on_tree_open_layer(layer_id, False))
        act_hide = QAction("Ocultar (mantener cargada)", self)
        act_hide.triggered.connect(lambda: self._on_tree_visibility(layer_id, False))
        act_close = QAction("Cerrar (descargar)", self)
        act_close.triggered.connect(lambda: self._on_tree_close_layer(layer_id, True))

        # Estilo / Enfocar
        act_focus = QAction("Enfocar", self)
        act_color = QAction("Color de capa…", self)

        # Mover a carpeta…
        act_move_to = QAction("Mover a carpeta…", self)
        def _do_move_to():
            try:
                folders = ["(raíz)"] + self.project.list_folders()
                sel, ok = QInputDialog.getItem(self, "Mover a carpeta", "Carpeta destino:", folders, 0, False)
                if not ok: return
                dest = "" if sel == "(raíz)" else sel
                self.project.set_group_path(layer_id, dest)
                self._refresh_project_tree()
            except Exception as e:
                QMessageBox.warning(self, "Mover a carpeta", f"No se pudo mover:\n{e}")
        act_move_to.triggered.connect(_do_move_to)

        # Nueva carpeta aquí…
        act_new_folder_here = QAction("Nueva carpeta aquí…", self)
        def _do_new_folder_here():
            try:
                meta = next(m for m in self.project.manifest.layers if m.id == layer_id)
                parent_gpath = (meta.group_path or "").strip("/")
                name, ok = QInputDialog.getText(self, "Nueva carpeta", "Nombre:")
                if not ok or not name.strip(): return
                self.project.create_folder(parent_gpath, name.strip())
                self._refresh_project_tree()
            except Exception as e:
                QMessageBox.warning(self, "Nueva carpeta", f"No se pudo crear la carpeta:\n{e}")
        act_new_folder_here.triggered.connect(_do_new_folder_here)

        # Renombrar capa…
        act_rename_layer = QAction("Renombrar…", self)
        def _do_rename_layer():
            new_name, ok = QInputDialog.getText(self, "Renombrar capa", "Nuevo nombre:", text=layer_name)
            if not ok or not new_name.strip(): return
            try:
                self.project.rename_layer(layer_id, new_name.strip())
                self._refresh_project_tree()
            except Exception as e:
                QMessageBox.warning(self, "Renombrar", f"No se pudo renombrar:\n{e}")
        act_rename_layer.triggered.connect(_do_rename_layer)

        # Eliminar del proyecto…
        act_delete = QAction("Eliminar del proyecto…", self)
        def _do_delete_layer():
            resp = QMessageBox.question(self, "Eliminar del proyecto",
                                        f"¿Eliminar '{layer_name}' del proyecto?",
                                        QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
            if resp != QMessageBox.StandardButton.Yes: return
            resp2 = QMessageBox.question(self, "Borrar archivo del disco",
                                         "¿También eliminar el archivo del disco?",
                                         QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
            delete_file = (resp2 == QMessageBox.StandardButton.Yes)
            try:
                self.project.remove_layer(layer_id, delete_file=delete_file)
                self._refresh_project_tree()
            except Exception as e:
                QMessageBox.warning(self, "Eliminar", f"No se pudo eliminar:\n{e}")
        act_delete.triggered.connect(_do_delete_layer)

        if sid is None:
            menu.addAction(act_open)
            menu.addAction(act_open_hidden)
            menu.addSeparator()
            menu.addAction(act_rename_layer)
            menu.addAction(act_move_to)
            menu.addAction(act_new_folder_here)
            menu.addSeparator()
            menu.addAction(act_delete)
        else:
            # Capa activa
            menu.addAction(act_focus)
            menu.addAction(act_color)
            edit_label = "Detener edicion" if state == "editing" else "Editar..."
            act_edit = QAction(edit_label, self)
            menu.addAction(act_edit)
            act_focus.triggered.connect(lambda: self.scene.zoom_to_layer(sid))
            act_color.triggered.connect(lambda: self._pick_project_layer_color(layer_id, sid))
            act_edit.triggered.connect(lambda _=False, lid=layer_id, enable=(state != "editing"): self._handle_layer_edit_action(lid, enable))

            # --- Propiedades (bins + estilo geometrico) ---
            act_props = QAction("Propiedades…", self)

            def _open_props():
                try:
                    meta = next(m for m in self.project.manifest.layers if m.id == layer_id)
                except StopIteration:
                    QMessageBox.warning(self, "Propiedades", "No se encontro la capa en el proyecto.")
                    return

                if meta.type == "blockmodel":
                    try:
                        layer_info = self.scene.layers[sid]
                        ds_now = layer_info.get("dataset")
                        ds_orig = layer_info.get("orig_dataset") or ds_now

                        ban = {"bm_scale", "x", "y", "z", "__vm_bins__", "__vm_keep__", "__vm_rgba__"}
                        attrs = []
                        try:
                            attrs += [k for k in getattr(ds_orig, "cell_data", {}).keys() if k not in ban]
                        except Exception:
                            pass
                        try:
                            attrs += [k for k in getattr(ds_orig, "point_data", {}).keys() if k not in ban]
                        except Exception:
                            pass
                        attrs = sorted(set(attrs))
                        if not attrs:
                            QMessageBox.information(self, "Propiedades", "No se encontraron atributos numericos en el modelo.")
                            return

                        current = self.scene.get_bins_config(sid) or {}
                        if not current:
                            style = meta.style if isinstance(meta.style, dict) else {}
                            cur2 = style.get("bins") or {}
                            if isinstance(cur2, dict):
                                current = cur2

                        dlg = LayerPropsDialog(
                            self,
                            attributes=attrs,
                            current=current,
                            on_create_isoshell=lambda a, mn, mx: self._create_isoshell_from_props(sid, layer_id, a, mn, mx),
                        )
                        if dlg.exec() != dlg.DialogCode.Accepted:
                            return

                        cfg = dlg.result_config()
                        if cfg.get("array") and cfg.get("rules"):
                            self.scene.apply_bins(sid, cfg["array"], cfg["rules"], cfg.get("colors"))
                            try:
                                style = meta.style if isinstance(meta.style, dict) else {}
                                style = dict(style)
                                style["bins"] = cfg
                                self.project.set_layer_style(layer_id, style)
                            except Exception:
                                pass
                    except Exception as e:
                        QMessageBox.warning(self, "Propiedades", f"No se pudo aplicar:\n{e}")
                    return

                layer_info = self.scene.layers.get(sid)
                if not layer_info:
                    QMessageBox.warning(self, "Propiedades", "La capa no esta activa en la escena.")
                    return
                actor = layer_info.get("actor")
                if actor is None or not hasattr(actor, "GetProperty"):
                    QMessageBox.warning(self, "Propiedades", "No se pudo obtener el estilo actual de la capa.")
                    return
                try:
                    prop = actor.GetProperty()
                except Exception:
                    QMessageBox.warning(self, "Propiedades", "No se pudo acceder a las propiedades de la capa.")
                    return

                flags = layer_info.get("geom_flags") or {}
                meta_geom = meta.style.get("geom") if isinstance(meta.style, dict) else {}
                base_style = self._snapshot_geom_style(actor, prop, meta_geom, flags)

                dlg = GeometryStyleDialog(
                    self,
                    layer_name=layer_name,
                    flags=flags,
                    base_style=base_style,
                )
                if dlg.exec() != dlg.DialogCode.Accepted:
                    return
                new_style = dlg.result_style()
                if not new_style:
                    return

                self.scene.apply_geom_style(sid, new_style)

                style = dict(meta.style or {})
                style["geom"] = new_style
                if "opacity" in new_style:
                    try:
                        style["opacity"] = float(new_style["opacity"])
                    except Exception:
                        pass

                dominant_color = None
                faces_cfg = new_style.get("faces") or {}
                lines_cfg = new_style.get("lines") or {}
                points_cfg = new_style.get("points") or {}
                if flags.get("has_faces") and faces_cfg.get("visible", True):
                    dominant_color = faces_cfg.get("color")
                if (not dominant_color or not faces_cfg.get("visible", True)) and lines_cfg.get("visible", False):
                    dominant_color = lines_cfg.get("color")
                if not dominant_color and points_cfg.get("visible", False):
                    dominant_color = points_cfg.get("color")
                if dominant_color:
                    try:
                        style["color"] = [float(dominant_color[0]), float(dominant_color[1]), float(dominant_color[2])]
                    except Exception:
                        pass

                self.project.set_layer_style(layer_id, style)
                self._refresh_project_tree()

            act_props.triggered.connect(_open_props)
            menu.addAction(act_props)
            # --- fin Propiedades ---
            menu.addSeparator()
            menu.addAction(act_hide)
            menu.addAction(act_close)
            menu.addSeparator()
            menu.addAction(act_rename_layer)
            menu.addAction(act_move_to)
            menu.addAction(act_new_folder_here)
            menu.addSeparator()
            menu.addAction(act_delete)

        return menu

    def _snapshot_geom_style(self, actor, prop, meta_geom: dict | None, flags: dict | None) -> dict:
        flags = flags or {}
        meta_geom = meta_geom if isinstance(meta_geom, dict) else {}
        style: dict = {}

        has_points = bool(flags.get("has_points"))
        has_lines = bool(flags.get("has_lines"))
        has_faces = bool(flags.get("has_faces"))

        def _ensure_color(spec, fallback):
            candidate = spec if isinstance(spec, (list, tuple)) and len(spec) >= 3 else fallback
            if candidate is None:
                candidate = (0.8, 0.8, 0.8)
            return [float(candidate[0]), float(candidate[1]), float(candidate[2])]

        def _prop_color():
            if prop is None:
                return None
            try:
                return tuple(prop.GetColor())
            except Exception:
                return None

        def _edge_color():
            if prop is None:
                return None
            try:
                return tuple(prop.GetEdgeColor())
            except Exception:
                return None

        def _line_width():
            if prop is None:
                return None
            try:
                return float(prop.GetLineWidth())
            except Exception:
                return None

        def _point_size():
            if prop is None:
                return None
            try:
                return float(prop.GetPointSize())
            except Exception:
                return None

        def _points_as_spheres():
            if prop is None:
                return None
            try:
                return bool(prop.GetRenderPointsAsSpheres())
            except Exception:
                return None

        try:
            current_opacity = float(prop.GetOpacity())
        except Exception:
            current_opacity = 1.0
        style["opacity"] = current_opacity

        actor_visible = bool(getattr(actor, "GetVisibility", lambda: 1)())
        try:
            rep = int(prop.GetRepresentation())
        except Exception:
            rep = None
        try:
            edge_vis = bool(prop.GetEdgeVisibility())
        except Exception:
            edge_vis = False

        points_meta = dict(meta_geom.get("points") or {})
        lines_meta = dict(meta_geom.get("lines") or {})
        faces_meta = dict(meta_geom.get("faces") or {})

        points_visible_actual = actor_visible and has_points and not has_lines and not has_faces
        lines_visible_actual = actor_visible and (has_lines or has_faces)
        if has_faces:
            lines_visible_actual = actor_visible and (edge_vis or (rep == 1 and current_opacity <= 0.01))
        faces_visible_actual = actor_visible and has_faces and current_opacity > 0.01 and (rep is None or rep != 1)

        points_visible = bool(points_meta.get("visible", points_visible_actual))
        lines_visible = bool(lines_meta.get("visible", lines_visible_actual))
        faces_visible = bool(faces_meta.get("visible", faces_visible_actual))

        points_meta["visible"] = points_visible
        lines_meta["visible"] = lines_visible
        faces_meta["visible"] = faces_visible

        points_meta["color"] = _ensure_color(points_meta.get("color"), _prop_color())
        size_val = points_meta.get("size")
        if size_val is None:
            size_val = _point_size()
        try:
            points_meta["size"] = float(size_val) if size_val is not None else 6.0
        except Exception:
            points_meta["size"] = 6.0
        shape_val = (points_meta.get("shape") or "").lower()
        if not shape_val:
            rendered = _points_as_spheres()
            if rendered is None:
                shape_val = "auto"
            else:
                shape_val = "sphere" if rendered else "square"
        points_meta["shape"] = shape_val
        style["points"] = points_meta

        fallback_line = _edge_color() if has_faces else _prop_color()
        lines_meta["color"] = _ensure_color(lines_meta.get("color"), fallback_line)
        width_val = lines_meta.get("width")
        if width_val is None:
            width_val = _line_width()
        try:
            lines_meta["width"] = float(width_val) if width_val is not None else 1.5
        except Exception:
            lines_meta["width"] = 1.5
        style["lines"] = lines_meta

        faces_meta["color"] = _ensure_color(faces_meta.get("color"), _prop_color())
        style["faces"] = faces_meta

        return style

    def _pick_project_layer_color(self, layer_id: str, scene_layer_id: str):
        color = QColorDialog.getColor(parent=self)
        if not color.isValid(): return
        rgb = (color.redF(), color.greenF(), color.blueF())
        try:
            self.scene.set_color(scene_layer_id, rgb)
            style = {}
            try:
                meta = next(m for m in self.project.manifest.layers if m.id == layer_id)
                style = dict(meta.style or {})
            except Exception:
                style = {}

            style["color"] = [float(rgb[0]), float(rgb[1]), float(rgb[2])]

            geom_style = dict(style.get("geom") or {})
            flags = {}
            try:
                flags = self.scene.layers.get(scene_layer_id, {}).get("geom_flags") or {}
            except Exception:
                flags = {}

            updated_geom = False
            if flags.get("has_faces"):
                faces = dict(geom_style.get("faces") or {})
                faces["color"] = [float(rgb[0]), float(rgb[1]), float(rgb[2])]
                geom_style["faces"] = faces
                updated_geom = True
            elif flags.get("has_lines"):
                lines = dict(geom_style.get("lines") or {})
                lines["color"] = [float(rgb[0]), float(rgb[1]), float(rgb[2])]
                geom_style["lines"] = lines
                updated_geom = True
            elif flags.get("has_points"):
                points = dict(geom_style.get("points") or {})
                points["color"] = [float(rgb[0]), float(rgb[1]), float(rgb[2])]
                geom_style["points"] = points
                updated_geom = True

            if updated_geom:
                style["geom"] = geom_style
                try:
                    self.scene.apply_geom_style(scene_layer_id, geom_style)
                except Exception:
                    pass

            self.project.set_layer_style(layer_id, style)
            self._refresh_project_tree()
        except Exception as e:
            QMessageBox.warning(self, "Color", f"No se pudo aplicar color:\n{e}")


    def _handle_layer_edit_action(self, layer_id: str, enable: bool):
        self._set_layer_editing(layer_id, enable)

    def _set_layer_editing(self, layer_id: str, enable: bool):
        current = self._editing_layer_id
        try:
            if enable:
                if current and current != layer_id:
                    self.project.set_layer_editing(current, False)
                sid = self.project.set_layer_editing(layer_id, True)
                self._editing_layer_id = layer_id
                self._editing_scene_sid = sid
            else:
                sid = self.project.set_layer_editing(layer_id, False)
                if current == layer_id:
                    self._editing_layer_id = None
                    self._editing_scene_sid = None
        except Exception as e:
            QMessageBox.warning(self, "Editar capa", f"No se pudo actualizar el modo de edicion:\n{e}")
            return

        if enable and self._editing_scene_sid:
            if self.polyline_tool.is_active():
                self.polyline_tool.activate(self._editing_scene_sid)
        else:
            if self.polyline_tool.is_active() and self.polyline_tool.current_layer() == self._editing_scene_sid:
                self.polyline_tool.deactivate(status="Dibujar polilinea: desactivado.", cancelled=False)

        self._refresh_project_tree()

    # ============== Crear Isoshell (callback del diálogo) ==============

    def _create_isoshell_from_props(self, scene_layer_id: str, proj_layer_id: str, attr: str, vmin: float, vmax: float):
        try:
            poly = self.scene.build_isoshell(scene_layer_id, attr, vmin, vmax)
        except Exception as e:
            QMessageBox.warning(self, "Isoshell", f"No se pudo generar la envolvente:\n{e}")
            return

        name = f"Isoshell_{attr}_{vmin:.3g}_{vmax:.3g}"

        # Si hay proyecto abierto: guardar OBJ y registrar como capa
        try:
            if getattr(self.project, "root", None):
                out_dir = os.path.join(self.project.root, "generated")
                os.makedirs(out_dir, exist_ok=True)
                out_path = os.path.join(out_dir, f"{name}.obj")
                poly.save(out_path)

                meta = self.project.add_asset(
                    out_path, layer_type="obj", name=name,
                    group_path=self.layerTree.get_selected_group_path() or ""
                )
                new_sid = self.project.activate_layer(meta.id, show=True)
                try: self.scene.set_color(new_sid, (1.0, 0.9, 0.2))
                except Exception: pass
                self._refresh_project_tree()
                self.scene.zoom_to_layer(new_sid)
                self.statusBar().showMessage(f"Isoshell creado y registrado: {name}")
                return
        except Exception as e:
            QMessageBox.warning(self, "Isoshell",
                                f"Se generó la malla pero no se pudo registrar en el proyecto:\n{e}")

        # Sin proyecto: agregar a escena
        sid = self.scene.add_mesh(poly, name=name)
        try: self.scene.set_color(sid, (1.0, 0.9, 0.2))
        except Exception: pass
        self.scene.zoom_to_layer(sid)
        self.statusBar().showMessage(f"Isoshell creado en escena: {name}")

    # ================== Dibujo ==================


    def _on_draw_polyline_toggled(self, checked: bool):
        if checked:
            if not self._editing_layer_id or not self._editing_scene_sid:
                QMessageBox.information(self, "Dibujo", "Activa una capa en modo 'Editar...' antes de dibujar.")
                self.btn_draw_polyline.blockSignals(True)
                self.btn_draw_polyline.setChecked(False)
                self.btn_draw_polyline.blockSignals(False)
                return
            self.polyline_tool.activate(self._editing_scene_sid)
            # Establecer el snap actual en la herramienta
            current_snap = self.snap_selector.get_current_snap()
            self.polyline_tool.set_snap_type(current_snap)
            # Mostrar selector de snaps
            self.snap_selector.show_snap_selector()
        else:
            if self.polyline_tool.is_active():
                self.polyline_tool.deactivate(status="Dibujar polilinea: desactivado.", cancelled=False)
            # Ocultar selector de snaps
            self.snap_selector.hide_snap_selector()



    def _on_draw_polyline_commit(self, layer_sid: str, points: list[tuple[float, float, float]]):
        try:
            self.scene.append_polyline_to_layer(layer_sid, points)
        except Exception as e:
            QMessageBox.warning(self, "Dibujo", f"No se pudo crear la polilinea:\n{e}")
            return

        if not self._editing_layer_id or not self._editing_scene_sid:
            self._refresh_project_tree()
            return
        if layer_sid != self._editing_scene_sid:
            self._refresh_project_tree()
            return

        try:
            meta = next(m for m in self.project.manifest.layers if m.id == self._editing_layer_id)
            style = dict(meta.style or {})
        except Exception:
            style = {}

        geom_style = dict(style.get("geom") or {})
        lines_cfg = dict(geom_style.get("lines") or {})
        color = lines_cfg.get("color") or style.get("color") or [1.0, 1.0, 1.0]
        lines_cfg["color"] = [float(color[0]), float(color[1]), float(color[2])]
        try:
            lines_cfg["width"] = float(lines_cfg.get("width", 1.5))
        except Exception:
            lines_cfg["width"] = 1.5
        lines_cfg["visible"] = True
        geom_style["lines"] = lines_cfg
        style["geom"] = geom_style
        style.setdefault("color", lines_cfg["color"])
        try:
            self.project.set_layer_style(self._editing_layer_id, style)
        except Exception:
            pass

        self.statusBar().showMessage("Polilinea agregada a la capa.")
        self._refresh_project_tree()
        
        # Guardar automáticamente el proyecto después de agregar la polilínea
        try:
            self.project.save_project()
        except Exception as e:
            print(f"❌ Error guardando proyecto: {e}")



    def _on_draw_tool_finished(self, cancelled: bool):
        pass

    def _on_draw_poly_tool_finished(self, cancelled: bool):
        if self.btn_draw_polyline.isChecked():
            self.btn_draw_polyline.blockSignals(True)
            self.btn_draw_polyline.setChecked(False)
            self.btn_draw_polyline.blockSignals(False)
        if cancelled:
            self.statusBar().showMessage("Dibujar polilinea: cancelado.")
        # Ocultar selector de snaps cuando se termina la herramienta
        self.snap_selector.hide_snap_selector()

    def _on_snap_changed(self, snap_type: str):
        """Manejar cambio de tipo de snap."""
        snap_names = {
            'off': 'Snap Off',
            'point': 'Snap a Puntos',
            'poly': 'Snap a Polilíneas',
            'face': 'Snap a Caras'
        }
        snap_name = snap_names.get(snap_type, 'Snap Desconocido')
        self.statusBar().showMessage(f"Modo de snap: {snap_name}")
        
        # Establecer el tipo de snap en las herramientas de dibujo activas
        if hasattr(self, 'polyline_tool') and self.polyline_tool.is_active():
            self.polyline_tool.set_snap_type(snap_type)
        if hasattr(self, 'circle_tool') and self.circle_tool.is_active():
            self.circle_tool.set_snap_type(snap_type)
        if hasattr(self, 'rectangle_tool') and self.rectangle_tool.is_active():
            self.rectangle_tool.set_snap_type(snap_type)
        if hasattr(self, 'polygon_tool') and self.polygon_tool.is_active():
            self.polygon_tool.set_snap_type(snap_type)
            
        print(f"Snap cambiado a: {snap_type}")

    def _on_draw_circle_toggled(self, checked: bool):
        """Manejar activación/desactivación de herramienta de círculo."""
        if checked:
            if not self._editing_layer_id or not self._editing_scene_sid:
                QMessageBox.information(self, "Dibujo", "Activa una capa en modo 'Editar...' antes de dibujar.")
                self.btn_draw_circle.blockSignals(True)
                self.btn_draw_circle.setChecked(False)
                self.btn_draw_circle.blockSignals(False)
                return
            self.circle_tool.activate(self._editing_scene_sid)
            # Establecer el snap actual en la herramienta
            current_snap = self.snap_selector.get_current_snap()
            self.circle_tool.set_snap_type(current_snap)
            # Mostrar selector de snaps
            self.snap_selector.show_snap_selector()
            self.statusBar().showMessage(self.circle_tool.get_status_message())
        else:
            self.circle_tool.deactivate()
            # Ocultar selector de snaps
            self.snap_selector.hide_snap_selector()
            self.statusBar().showMessage("Herramienta de círculo desactivada")

    def _on_draw_rectangle_toggled(self, checked: bool):
        """Manejar activación/desactivación de herramienta de rectángulo."""
        if checked:
            if not self._editing_layer_id or not self._editing_scene_sid:
                QMessageBox.information(self, "Dibujo", "Activa una capa en modo 'Editar...' antes de dibujar.")
                self.btn_draw_rectangle.blockSignals(True)
                self.btn_draw_rectangle.setChecked(False)
                self.btn_draw_rectangle.blockSignals(False)
                return
            self.rectangle_tool.activate(self._editing_scene_sid)
            # Establecer el snap actual en la herramienta
            current_snap = self.snap_selector.get_current_snap()
            self.rectangle_tool.set_snap_type(current_snap)
            # Mostrar selector de snaps
            self.snap_selector.show_snap_selector()
            self.statusBar().showMessage(self.rectangle_tool.get_status_message())
        else:
            self.rectangle_tool.deactivate()
            # Ocultar selector de snaps
            self.snap_selector.hide_snap_selector()
            self.statusBar().showMessage("Herramienta de rectángulo desactivada")

    def _on_draw_circle_commit(self, layer_sid: str, circle_data):
        """Manejar commit de círculo."""
        try:
            self.scene.append_polydata_to_layer(layer_sid, circle_data)
            self.project.save_project()
        except Exception as e:
            QMessageBox.warning(self, "Dibujo", f"No se pudo crear el círculo:\n{e}")
            return

    def _on_draw_rectangle_commit(self, layer_sid: str, rectangle_data):
        """Manejar commit de rectángulo."""
        try:
            self.scene.append_polydata_to_layer(layer_sid, rectangle_data)
            self.project.save_project()
        except Exception as e:
            QMessageBox.warning(self, "Dibujo", f"No se pudo crear el rectángulo:\n{e}")
            return

    def _on_draw_circle_finished(self, cancelled: bool):
        """Manejar finalización de herramienta de círculo."""
        if self.btn_draw_circle.isChecked():
            self.btn_draw_circle.blockSignals(True)
            self.btn_draw_circle.setChecked(False)
            self.btn_draw_circle.blockSignals(False)
        if cancelled:
            self.statusBar().showMessage("Dibujar círculo: cancelado.")
        # Ocultar selector de snaps cuando se termina la herramienta
        self.snap_selector.hide_snap_selector()

    def _on_draw_rectangle_finished(self, cancelled: bool):
        """Manejar finalización de herramienta de rectángulo."""
        if self.btn_draw_rectangle.isChecked():
            self.btn_draw_rectangle.blockSignals(True)
            self.btn_draw_rectangle.setChecked(False)
            self.btn_draw_rectangle.blockSignals(False)
        if cancelled:
            self.statusBar().showMessage("Dibujar rectángulo: cancelado.")
        # Ocultar selector de snaps cuando se termina la herramienta
        self.snap_selector.hide_snap_selector()

    def _on_draw_polygon_toggled(self, checked: bool):
        """Manejar activación/desactivación de herramienta de polígono."""
        if checked:
            if not self._editing_layer_id or not self._editing_scene_sid:
                QMessageBox.information(self, "Dibujo", "Activa una capa en modo 'Editar...' antes de dibujar.")
                self.btn_draw_polygon.blockSignals(True)
                self.btn_draw_polygon.setChecked(False)
                self.btn_draw_polygon.blockSignals(False)
                return
            self.polygon_tool.activate(self._editing_scene_sid)
            # Establecer el snap actual en la herramienta
            current_snap = self.snap_selector.get_current_snap()
            self.polygon_tool.set_snap_type(current_snap)
            # Mostrar selector de snaps
            self.snap_selector.show_snap_selector()
            self.statusBar().showMessage(self.polygon_tool.get_status_message())
        else:
            self.polygon_tool.deactivate()
            # Ocultar selector de snaps
            self.snap_selector.hide_snap_selector()
            self.statusBar().showMessage("Herramienta de polígono desactivada")

    def _on_draw_polygon_commit(self, layer_sid: str, points):
        """Manejar commit de polígono."""
        try:
            print(f"🔄 MainWindow: Recibido commit de polígono - Capa: {layer_sid}, Puntos: {len(points)}")
            print(f"🔄 MainWindow: Puntos recibidos: {points}")
            self.scene.append_polyline_to_layer(layer_sid, points)
            print(f"🔄 MainWindow: Polígono agregado a la capa")
            self.project.save_project()
            print(f"🔄 MainWindow: Proyecto guardado")
        except Exception as e:
            print(f"❌ MainWindow: Error en commit de polígono: {e}")
            import traceback
            traceback.print_exc()
            QMessageBox.warning(self, "Dibujo", f"No se pudo crear el polígono:\n{e}")
            return

    def _on_draw_polygon_finished(self, cancelled: bool):
        """Manejar finalización de herramienta de polígono."""
        if self.btn_draw_polygon.isChecked():
            self.btn_draw_polygon.blockSignals(True)
            self.btn_draw_polygon.setChecked(False)
            self.btn_draw_polygon.blockSignals(False)
        if cancelled:
            self.statusBar().showMessage("Dibujar polígono: cancelado.")
        # Ocultar selector de snaps cuando se termina la herramienta
        self.snap_selector.hide_snap_selector()

    # ================== Herramientas de Edición ==================
    
    def _on_edit_select_toggled(self, checked: bool):
        """Manejar activación/desactivación de herramienta de selección."""
        if checked:
            self.statusBar().showMessage("Herramienta de selección activada - Click para seleccionar elementos")
            # TODO: Implementar lógica de selección
        else:
            self.statusBar().showMessage("Herramienta de selección desactivada")

    def _on_edit_delete_toggled(self, checked: bool):
        """Manejar activación/desactivación de herramienta de eliminación."""
        if checked:
            self.statusBar().showMessage("Herramienta de eliminación activada - Click para eliminar elementos")
            # TODO: Implementar lógica de eliminación
        else:
            self.statusBar().showMessage("Herramienta de eliminación desactivada")

    def _on_edit_copy_toggled(self, checked: bool):
        """Manejar activación/desactivación de herramienta de copia."""
        if checked:
            self.statusBar().showMessage("Herramienta de copia activada - Selecciona elementos para copiar")
            # TODO: Implementar lógica de copia
        else:
            self.statusBar().showMessage("Herramienta de copia desactivada")

    def _on_edit_move_toggled(self, checked: bool):
        """Manejar activación/desactivación de herramienta de movimiento."""
        if checked:
            self.statusBar().showMessage("Herramienta de movimiento activada - Selecciona y arrastra elementos")
            # TODO: Implementar lógica de movimiento
        else:
            self.statusBar().showMessage("Herramienta de movimiento desactivada")

    # ================== Cámara ==================

    def center_on_point(self):
        if self.camera_center_ctrl.is_active():
            self.camera_center_ctrl.deactivate(status="Centrar vista: cancelado.")
            return
        try:
            for btn in (self.btn_q_block, self.btn_q_geom, self.btn_q_poly, self.btn_q_meas):
                btn.setChecked(False)
        except Exception:
            pass
        self.camera_center_ctrl.activate()

    def _on_camera_centered(self, point):
        self._last_camera_center = tuple(point)

    def view_planta(self):
        self.plotter.view_vector((0, 0, 1), viewup=(0, 1, 0))
        # Asegurar proyección ortográfica
        self.scene.set_orthographic_projection(True)
        self.plotter.reset_camera()

    def view_norte(self):
        self.plotter.view_vector((0, 1, 0), viewup=(0, 0, 1))
        # Asegurar proyección ortográfica
        self.scene.set_orthographic_projection(True)
        self.plotter.reset_camera()

    def view_este(self):
        self.plotter.view_vector((1, 0, 0), viewup=(0, 0, 1))
        # Asegurar proyección ortográfica
        self.scene.set_orthographic_projection(True)
        self.plotter.reset_camera()

    # ================== Export ==================

    def export_obj_dialog(self):
        dlg = ExportOBJDialog(self, self.scene)
        if dlg.exec() != dlg.DialogCode.Accepted: return
        layer_ids = dlg.selected_layer_ids
        opts = dlg.options
        try:
            obj_path, mtl_path = export_layers_to_obj(
                self.scene, layer_ids, opts["out_path"],
                recenter_to_origin=opts["recenter"],
                zup_to_yup=opts["y_up"],
                use_layer_opacity=opts["use_opacity"],
            )
            QMessageBox.information(self, "Exportar OBJ",
                                    f"Exportación completada:\n{obj_path}\n{mtl_path}")
        except Exception as e:
            QMessageBox.warning(self, "Exportar OBJ", f"No se pudo exportar:\n{e}")

    def export_screen_dialog(self):
        w, h = (0, 0)
        try:
            rw = self.plotter.ren_win if hasattr(self.plotter, "ren_win") else None
            if rw: w, h = rw.GetSize()
        except Exception:
            pass

        dlg = ExportScreenDialog(self, current_size=(w, h))
        if dlg.exec() != dlg.DialogCode.Accepted: return
        opts = dlg.options

        try: self.scene.hide_snap_highlights(all=True)
        except Exception: pass

        try:
            out = save_screenshot(self.plotter, opts["path"], width=opts["width"], height=opts["height"])
            QMessageBox.information(self, "Exportar Pantalla", f"PNG guardado:\n{out}")
        except Exception as e:
            QMessageBox.warning(self, "Exportar Pantalla", f"No se pudo exportar:\n{e}")


    # ================== Herramientas ==================

    def _set_query_tile(self, which: int | None):
        self.query_group.setExclusive(False)
        self.btn_q_block.setChecked(which == 1)
        self.btn_q_geom.setChecked(which == 2)
        self.btn_q_poly.setChecked(which == 3)
        self.btn_q_meas.setChecked(which == 4)
        self.query_group.setExclusive(True)

    def _on_geom_toggled(self, checked: bool):
        if checked:
            if self.query.is_active(): self.query.deactivate()
            if self.polyline_ctrl.is_active(): self.polyline_ctrl.deactivate()
            if self.measure_ctrl.is_active(): self.measure_ctrl.deactivate()
            self.scene.hide_snap_highlights(all=True)
            self.geometry_ctrl.activate()
            self.statusBar().showMessage("Consultar (Geometría): ON")
        else:
            self.geometry_ctrl.deactivate()
            self.statusBar().showMessage("Consultar: Off")

    def _on_poly_toggled(self, checked: bool):
        if checked:
            if self.query.is_active(): self.query.deactivate()
            if self.geometry_ctrl.is_active(): self.geometry_ctrl.deactivate()
            if self.measure_ctrl.is_active(): self.measure_ctrl.deactivate()
            self.scene.hide_snap_highlights(all=True)
            self.polyline_ctrl.activate()
            self.statusBar().showMessage("Consultar (Polilíneas): ON")
        else:
            self.polyline_ctrl.deactivate()
            self.statusBar().showMessage("Consultar: Off")

    def _on_meas_toggled(self, checked: bool):
        if checked:
            if self.query.is_active(): self.query.deactivate()
            if self.geometry_ctrl.is_active(): self.geometry_ctrl.deactivate()
            if self.polyline_ctrl.is_active(): self.polyline_ctrl.deactivate()
            self.scene.hide_snap_highlights(all=True)
            self.measure_ctrl.activate()
            self.statusBar().showMessage("Consultar (Medir): ON — rotación con Shift+Izq. ESC para salir.")
        else:
            self.measure_ctrl.deactivate()
            self.statusBar().showMessage("Consultar: Off")

    def _on_block_toggled_aux(self, checked: bool):
        # No re-enfocar con cada clic: el enfoque 2D sólo se hace al pulsar el botón 2D
        try:
            if hasattr(self, "_obs_2d_focus") and self._obs_2d_focus:
                self.plotter.iren.remove_observer(self._obs_2d_focus)
        except Exception:
            pass
        self._obs_2d_focus = None

    # ---------- Sección: handlers ----------
    def _on_sec_2pt_toggled(self, checked: bool):
        if checked:
            # apagar otros controladores
            if self.query.is_active(): self.query.deactivate()
            if self.geometry_ctrl.is_active(): self.geometry_ctrl.deactivate()
            if self.polyline_ctrl.is_active(): self.polyline_ctrl.deactivate()
            if self.measure_ctrl.is_active(): self.measure_ctrl.deactivate()
            self.scene.hide_snap_highlights(all=True)
            # limpiar sección previa si existía
            try:
                self.scene.clear_section()
            except Exception:
                pass
            self.section_ctrl.activate_2pt()
            self.statusBar().showMessage("Sección (2 puntos): ON")
        else:
            self.section_ctrl.deactivate()
            self.statusBar().showMessage("Sección: Off")

    def _on_sec_3pt_toggled(self, checked: bool):
        if checked:
            if self.query.is_active(): self.query.deactivate()
            if self.geometry_ctrl.is_active(): self.geometry_ctrl.deactivate()
            if self.polyline_ctrl.is_active(): self.polyline_ctrl.deactivate()
            if self.measure_ctrl.is_active(): self.measure_ctrl.deactivate()
            self.scene.hide_snap_highlights(all=True)
            try:
                self.scene.clear_section()
            except Exception:
                pass
            self.section_ctrl.activate_3pt()
            self.statusBar().showMessage("Sección (3 puntos): ON")
        else:
            self.section_ctrl.deactivate()
            self.statusBar().showMessage("Sección: Off")

    def _on_section_plane_ready(self, plane: dict):
        # desactivar toggles para terminar la herramienta
        try:
            self.btn_sec_2pt.setChecked(False)
            self.btn_sec_3pt.setChecked(False)
        except Exception:
            pass
        self.statusBar().showMessage("Sección definida. Usa el botón 2D para ver en planta de la sección.")

    def _on_section_exit(self):
        # llamado desde ESC en el controller
        try:
            self.btn_sec_2pt.setChecked(False)
            self.btn_sec_3pt.setChecked(False)
        except Exception:
            pass
        self.statusBar().showMessage("Sección: Off")

    # ---------- Vista 2D/3D ----------
    def _set_interactor_style(self, style_obj):
        for iren_obj in (getattr(self.plotter, "iren", None), getattr(self.plotter, "interactor", None)):
            if iren_obj is None: continue
            if hasattr(iren_obj, "set_interactor_style"):
                try:
                    iren_obj.set_interactor_style(style_obj); return
                except Exception:
                    pass
            if hasattr(iren_obj, "SetInteractorStyle"):
                try:
                    getattr(iren_obj, "SetInteractorStyle")(style_obj); return
                except Exception:
                    pass

    def view_section_2d(self):
        # marcar tile 2D como activo
        try:
            if hasattr(self, "btn_sec_view2d"):
                self.btn_sec_view2d.setChecked(True)
        except Exception:
            pass
        # alinear cámara y bloquear rotación (estilo imagen)
        try:
            self.scene.set_view_to_section_2d()
        except Exception:
            pass
        try:
            img_style = vtk.vtkInteractorStyleImage()
            self._set_interactor_style(img_style)
        except Exception:
            pass
        # enfocar una vez tras cambiar el interactor (sin forzar durante la interacción)
        try:
            self.scene.focus_section_camera()
            QTimer.singleShot(50, lambda: self.scene.focus_section_camera())
        except Exception:
            pass
        self.statusBar().showMessage("Vista 2D sobre la sección. Usa 3D para volver.")

    def view_3d_reset(self):
        # marcar tile 3D como activo
        try:
            if hasattr(self, "btn_sec_view3d"):
                self.btn_sec_view3d.setChecked(True)
        except Exception:
            pass
        try:
            self.scene.set_view_3d_default()
        except Exception:
            pass
        try:
            self._set_interactor_style(self._base_iren_style)
        except Exception:
            pass
        # Asegurar que no quede ning�n observer de enfoque 2D activo
        try:
            if hasattr(self, "_obs_2d_focus") and self._obs_2d_focus:
                self.plotter.iren.remove_observer(self._obs_2d_focus)
        except Exception:
            pass
        self._obs_2d_focus = None
        # no hay observers persistentes en 2D para no pelear con pan/zoom
        self.statusBar().showMessage("Vista 3D restaurada (rotación con Shift+Izq).")

    def _query_on_info(self, info: dict):
        kind = (info.get("kind") or info.get("tipo") or "").lower()
        herramienta = (info.get("herramienta") or "").lower()
        es_poly = ("polil�nea" in herramienta) or ("polilinea" in herramienta) or (kind == "polyline")
        es_medir = (herramienta == "medir")

        if es_poly:
            if self.inspectDock.widget() is not self.polyInspector:
                self.inspectDock.setWidget(self.polyInspector)
            self.polyInspector.set_info(info)
        elif es_medir:
            if self.inspectDock.widget() is not self.measInspector:
                self.inspectDock.setWidget(self.measInspector)
            self.measInspector.set_info(info)
        else:
            if self.inspectDock.widget() is not self.inspector:
                self.inspectDock.setWidget(self.inspector)
            self.inspector.set_info(info)

        if not self.inspectDock.isVisible():
            self.inspectDock.show()

    def _query_on_status(self, msg: str):
        self.statusBar().showMessage(msg)

    def _query_on_exit(self):
        self._set_query_tile(None)

    # ================== Helpers UI ==================

    def _mk_tool_button(self, text: str, icon: QIcon, slot):
        b = QToolButton(self)
        b.setText(text)
        b.setIcon(icon)
        b.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextUnderIcon)
        b.setIconSize(QSize(44, 44))
        b.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
        b.clicked.connect(slot)
        return b

    def _mk_toggle_tool_button(self, text: str, icon: QIcon):
        b = QToolButton(self)
        b.setText(text)
        b.setIcon(icon)
        b.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextUnderIcon)
        b.setIconSize(QSize(44, 44))
        b.setCheckable(True)
        b.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
        return b



    def _kill_hw_selector(self):
        """Apaga cualquier helper/observer de PyVista que active el vtkOpenGLHardwareSelector."""
        for attr in ("_picker_helper", "_selection_observer", "_box_select_observer", "_rubber_band_style"):
            try:
                obj = getattr(self.plotter, attr, None)
                if obj is None: continue
                if isinstance(obj, int) and hasattr(self.plotter, "iren"):
                    self.plotter.iren.remove_observer(obj)
                elif hasattr(obj, "off"):
                    obj.off()
                setattr(self.plotter, attr, None)
            except Exception:
                pass

    # ================== Configuración de Icono ==================

    def _set_app_icon(self):
        """Configurar el icono de la aplicación"""
        try:
            # Buscar el archivo de icono en diferentes ubicaciones
            icon_paths = [
                # Ruta relativa desde el directorio de trabajo actual
                "app/ui/assets/icon.ico",
                "app/ui/assets/icon.png", 
                "app/ui/assets/logo.ico",
                "app/ui/assets/logo.png",
                "app/ui/assets/logo.ico.png",  # Caso especial: archivo guardado como .ico.png
                # Ruta absoluta desde el directorio del script
                os.path.join(os.path.dirname(__file__), "assets", "icon.ico"),
                os.path.join(os.path.dirname(__file__), "assets", "icon.png"),
                os.path.join(os.path.dirname(__file__), "assets", "logo.ico"),
                os.path.join(os.path.dirname(__file__), "assets", "logo.png"),
                os.path.join(os.path.dirname(__file__), "assets", "logo.ico.png"),
            ]
            
            icon_path = None
            for path in icon_paths:
                if os.path.exists(path):
                    icon_path = path
                    break
            
            if icon_path:
                icon = QIcon(icon_path)
                if not icon.isNull():
                    self.setWindowIcon(icon)
                    print(f"Icono de aplicación configurado: {icon_path}")
                else:
                    print(f"No se pudo cargar el icono: {icon_path}")
            else:
                print("No se encontró archivo de icono. Ubicaciones buscadas:")
                for path in icon_paths:
                    print(f"  - {path}")
                print("\nPara agregar un icono:")
                print("1. Coloca tu archivo de icono (.ico o .png) en: app/ui/assets/")
                print("2. Nómbralo como: icon.ico, icon.png, logo.ico, o logo.png")
                
        except Exception as e:
            print(f"Error al configurar el icono: {e}")

    # ================== Limpieza ==================

    def closeEvent(self, event):
        try:
            if hasattr(self, "query") and self.query.is_active(): self.query.deactivate()
            if hasattr(self, "geometry_ctrl") and self.geometry_ctrl.is_active(): self.geometry_ctrl.deactivate()
            if hasattr(self, "polyline_ctrl") and self.polyline_ctrl.is_active(): self.polyline_ctrl.deactivate()
            if hasattr(self, "measure_ctrl") and self.measure_ctrl.is_active(): self.measure_ctrl.deactivate()
            if hasattr(self, "polyline_tool") and self.polyline_tool.is_active(): self.polyline_tool.deactivate(status=None, cancelled=False)
            if hasattr(self, "camera_center_ctrl") and self.camera_center_ctrl.is_active(): self.camera_center_ctrl.deactivate(status=None)
        except Exception:
            pass
        super().closeEvent(event)

    # ================== Rutas: Gestor (CRUD + carga) ==================

    def start_new_route_named(self, name: str):
        """Activar selector de polilínea y, una vez elegida, abrir editor para guardar CSV."""
        self.route_manager.start_new_route_named(name)

    def edit_route_named(self, name: str):
        """Edita una ruta existente."""
        self.route_manager.edit_route_named(name)

    def load_all_routes(self):
        """Carga todas las rutas guardadas y posiciona los equipos en sus puntos de inicio."""
        self.route_manager.load_all_routes()

    def _setup_custom_interactor(self):
        """Configura el interactor personalizado directamente en el código fuente."""
        # La modificación ahora está en la librería de PyVista
        print("✅ Interactor personalizado configurado en PyVista:")
        print("   - LEFT: Libre (para dibujar)")
        print("   - LEFT+SHIFT: Rotación")
        print("   - MIDDLE: Pan (desplazamiento)")
        print("   - RIGHT: Libre")








