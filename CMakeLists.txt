cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "C:/Users/ctapi/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

project(GreenVisor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(VTK REQUIRED COMPONENTS
  CommonCore
  CommonDataModel
  FiltersSources
  FiltersExtraction
  InteractionStyle
  IOGeometry
  IOXML
  RenderingCore
  RenderingOpenGL2
  RenderingAnnotation
  GUISupportQt
)
find_package(OpenCASCADE REQUIRED)

set(VTK_MODULES
  VTK::CommonCore
  VTK::CommonDataModel
  VTK::FiltersSources
  VTK::FiltersExtraction
  VTK::InteractionStyle
  VTK::IOGeometry
  VTK::IOXML
  VTK::RenderingCore
  VTK::RenderingOpenGL2
  VTK::RenderingAnnotation
  VTK::GUISupportQt
)

add_executable(GreenVisor
  src/main.cpp
  src/gui/MainWindow.cpp
  src/gui/LayerModel.cpp
  src/gui/StartScreenDialog.cpp
  src/io/DxfLoader.cpp
  third_party/mineflow/mineflow.cpp
  src/GreenVisor.rc
  include/gui/MainWindow.h
  include/gui/LayerModel.h
  include/gui/StartScreenDialog.h
  include/io/DxfLoader.h
  third_party/mineflow/mineflow.h
)

target_include_directories(GreenVisor PRIVATE include third_party/mineflow)

target_link_libraries(GreenVisor PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Svg
  ${VTK_MODULES}
  ${OpenCASCADE_LIBRARIES}
)

vtk_module_autoinit(
  TARGETS GreenVisor
  MODULES ${VTK_MODULES}
)
