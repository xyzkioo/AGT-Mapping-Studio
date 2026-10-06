cmake_minimum_required(VERSION 3.8)
project(agt_map_studio)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(agt_pcd2grid_exporter REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PCL REQUIRED COMPONENTS common io)
find_package(yaml-cpp REQUIRED)
find_library(OCTOMAP_LIBRARY octomap REQUIRED)
find_library(OCTOMATH_LIBRARY octomath REQUIRED)
add_executable(studio_octomap_builder algorithms/build_octomap.cpp)
target_link_libraries(studio_octomap_builder ${OCTOMAP_LIBRARY} ${OCTOMATH_LIBRARY})
install(TARGETS studio_octomap_builder DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY algorithms DESTINATION share/${PROJECT_NAME}
  PATTERN "__pycache__" EXCLUDE)

# Qt6 is preferred when available, but this workspace currently provides Qt5.
find_package(Qt6 QUIET COMPONENTS Widgets OpenGLWidgets)
if(Qt6_FOUND)
  set(QT_WIDGETS_TARGET Qt6::Widgets)
  set(QT_OPENGL_TARGET Qt6::OpenGLWidgets)
else()
  find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL)
  set(QT_WIDGETS_TARGET Qt5::Widgets)
  set(QT_OPENGL_TARGET Qt5::OpenGL)
endif()

add_executable(map_viewer
  src/main.cpp
  src/io/PCDLoader.cpp
  src/viewer/CameraController.cpp
  src/viewer/PointCloudViewer.cpp
  src/viewer/ScalarFieldColorMap.cpp
  src/selection/SelectionBox.cpp
  src/selection/SelectionManager.cpp
  src/occupancy/GridMap.cpp
  src/occupancy/MapYamlLoader.cpp
  src/occupancy/OccupancyViewer.cpp
  src/occupancy/RefinementModel.cpp
  src/occupancy/commands/EraseRectangleCommand.cpp
  src/occupancy/commands/DrawObstacleCommand.cpp
  src/occupancy/commands/ForbiddenPolygonCommand.cpp
  src/occupancy/commands/FillPolygonCommand.cpp
  src/tools/ExternalToolRunner.cpp
  src/workflow/WorkflowSession.cpp
  src/ui/WorkflowPanel.cpp
  src/ui/MainWindow.cpp
  src/ui/NavigationAlgorithms.cpp
)

target_include_directories(map_viewer PRIVATE
  src
  ${PCL_INCLUDE_DIRS}
)

target_compile_definitions(map_viewer PRIVATE ${PCL_DEFINITIONS})

ament_target_dependencies(map_viewer
  ament_index_cpp
)

target_link_libraries(map_viewer
  ${PCL_LIBRARIES}
  ${QT_WIDGETS_TARGET}
  ${QT_OPENGL_TARGET}
  yaml-cpp
  agt_pcd2grid_exporter::agt_pcd2grid_exporter
  OpenGL::GL
)

install(TARGETS map_viewer
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  option(STUDIO_ALGORITHM_SMOKE "Run fixed pipeline using current MID360 assets" OFF)
  if(STUDIO_ALGORITHM_SMOKE)
    get_target_property(STUDIO_SOURCES map_viewer SOURCES)
    list(REMOVE_ITEM STUDIO_SOURCES src/main.cpp)
    add_executable(navigation_algorithms_smoke test/navigation_algorithms_smoke.cpp ${STUDIO_SOURCES})
    target_include_directories(navigation_algorithms_smoke PRIVATE src ${PCL_INCLUDE_DIRS})
    target_compile_definitions(navigation_algorithms_smoke PRIVATE ${PCL_DEFINITIONS})
    ament_target_dependencies(navigation_algorithms_smoke ament_index_cpp)
    target_link_libraries(navigation_algorithms_smoke ${PCL_LIBRARIES} ${QT_WIDGETS_TARGET}
      ${QT_OPENGL_TARGET} yaml-cpp agt_pcd2grid_exporter::agt_pcd2grid_exporter OpenGL::GL)
    add_test(NAME test_navigation_algorithms_smoke COMMAND navigation_algorithms_smoke)
    set_tests_properties(test_navigation_algorithms_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
      TIMEOUT 70)
    add_test(NAME test_standalone_2d_workflow COMMAND navigation_algorithms_smoke --check-2d)
    set_tests_properties(test_standalone_2d_workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    add_test(NAME test_file_dialog_navigation COMMAND navigation_algorithms_smoke --check-file-dialog)
    set_tests_properties(test_file_dialog_navigation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME test_algorithm_pipeline
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/test_algorithm_pipeline.py)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_pcd_loader
    test/test_pcd_loader.cpp
    src/io/PCDLoader.cpp
  )
  target_include_directories(test_pcd_loader PRIVATE src ${PCL_INCLUDE_DIRS})
  target_compile_definitions(test_pcd_loader PRIVATE ${PCL_DEFINITIONS})
  ament_target_dependencies(test_pcd_loader ament_index_cpp)
  target_link_libraries(test_pcd_loader ${PCL_LIBRARIES})

  ament_add_gtest(test_selection_manager
    test/test_selection_manager.cpp
    src/io/PCDLoader.cpp
    src/selection/SelectionManager.cpp
  )
  target_include_directories(test_selection_manager PRIVATE src ${PCL_INCLUDE_DIRS})
  target_compile_definitions(test_selection_manager PRIVATE ${PCL_DEFINITIONS})
  target_link_libraries(test_selection_manager
    ${PCL_LIBRARIES}
    ${QT_WIDGETS_TARGET}
    yaml-cpp
  )

  ament_add_gtest(test_occupancy_viewer
    test/test_occupancy_viewer.cpp
    src/occupancy/GridMap.cpp
    src/occupancy/MapYamlLoader.cpp
    src/occupancy/RefinementModel.cpp
    src/occupancy/commands/EraseRectangleCommand.cpp
    src/occupancy/commands/DrawObstacleCommand.cpp
    src/occupancy/commands/ForbiddenPolygonCommand.cpp
    src/occupancy/commands/FillPolygonCommand.cpp
  )
  target_include_directories(test_occupancy_viewer PRIVATE src)
  target_link_libraries(test_occupancy_viewer yaml-cpp)

  ament_add_gtest(test_occupancy_refinement
    test/test_occupancy_refinement.cpp
    src/occupancy/GridMap.cpp
    src/occupancy/MapYamlLoader.cpp
    src/occupancy/RefinementModel.cpp
    src/occupancy/commands/EraseRectangleCommand.cpp
    src/occupancy/commands/DrawObstacleCommand.cpp
    src/occupancy/commands/ForbiddenPolygonCommand.cpp
    src/occupancy/commands/FillPolygonCommand.cpp
  )
  target_include_directories(test_occupancy_refinement PRIVATE src)
  target_link_libraries(test_occupancy_refinement yaml-cpp)

  ament_add_gtest(test_patch_export
    test/test_patch_export.cpp
    src/occupancy/GridMap.cpp
    src/occupancy/MapYamlLoader.cpp
    src/occupancy/RefinementModel.cpp
    src/occupancy/commands/EraseRectangleCommand.cpp
    src/occupancy/commands/DrawObstacleCommand.cpp
    src/occupancy/commands/ForbiddenPolygonCommand.cpp
    src/occupancy/commands/FillPolygonCommand.cpp
  )
  target_include_directories(test_patch_export PRIVATE src)
  target_link_libraries(test_patch_export yaml-cpp)

  ament_add_gtest(test_refinement_rules_export
    test/test_refinement_rules_export.cpp
    src/io/PCDLoader.cpp
    src/selection/SelectionManager.cpp
  )
  target_include_directories(test_refinement_rules_export PRIVATE src ${PCL_INCLUDE_DIRS})
  target_compile_definitions(test_refinement_rules_export PRIVATE ${PCL_DEFINITIONS})
  target_link_libraries(test_refinement_rules_export
    ${PCL_LIBRARIES}
    ${QT_WIDGETS_TARGET}
    yaml-cpp
  )

  ament_add_gtest(test_workflow_session
    test/test_workflow_session.cpp
    src/workflow/WorkflowSession.cpp
  )
  target_include_directories(test_workflow_session PRIVATE src)
  target_link_libraries(test_workflow_session ${QT_WIDGETS_TARGET} yaml-cpp)
endif()

ament_package()
