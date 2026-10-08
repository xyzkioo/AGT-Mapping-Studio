#pragma once

#include "occupancy/GridMap.hpp"

#include <QImage>
#include <QMouseEvent>
#include <QPointF>
#include <QVector>
#include <QWheelEvent>
#include <QWidget>
#include <vector>

namespace agt_map_studio {

class RefinementModel;

enum class OccupancyInteractionMode {
  View,
  Erase,
  Obstacle,
  Forbidden,
  FreePolygon,      // fill polygon -> free
  OccupiedPolygon,  // fill polygon -> occupied
  UnknownPolygon,   // fill polygon -> unknown
  FreeRectangle,   // drag rectangle -> free
  InspectXY,       // view-only: map cell/rectangle -> all-height cloud
  ObstacleMarker  // single click -> occupied footprint
};

inline bool occupancy_mode_uses_polygon(OccupancyInteractionMode mode) {
  return mode == OccupancyInteractionMode::Forbidden ||
         mode == OccupancyInteractionMode::FreePolygon ||
         mode == OccupancyInteractionMode::OccupiedPolygon ||
         mode == OccupancyInteractionMode::UnknownPolygon;
}

class OccupancyViewer : public QWidget {
  Q_OBJECT

public:
  explicit OccupancyViewer(QWidget *parent = nullptr);

  void set_map(GridMap map);
  void set_refinement_model(RefinementModel *model);
  void refresh();
  void clear_map();
  void reset_view();
  void fit_map();
  bool has_map() const { return !map_.empty(); }
  const GridMap &map() const { return map_; }
  double zoom() const { return zoom_; }
  void set_mode(OccupancyInteractionMode mode);
  OccupancyInteractionMode mode() const { return mode_; }
  void set_obstacle_width(double width_m);
  void cancel_polygon();
  int pending_polygon_vertices() const { return forbidden_polygon_world_.size(); }
  // Removes the last vertex of the polygon being drawn.
  void pop_polygon_vertex();
  void finish_polygon();
  static QString mode_name(OccupancyInteractionMode mode);
  double obstacle_width() const { return obstacle_width_m_; }

  void set_highlighted_cells(std::vector<std::size_t> cells);
  const std::vector<std::size_t> &highlighted_cells() const { return highlighted_cells_; }
  bool linked_inspection_enabled() const { return linked_inspection_enabled_; }
  void set_linked_inspection_enabled(bool enabled);

signals:
  void inspect_xy_requested(double min_x, double min_y, double max_x, double max_y);
  void inspection_unavailable();
  void clear_inspection_requested();
  void status_changed(const QString &text);
  void erase_rectangle_requested(double min_x, double min_y, double max_x,
                                 double max_y);
  void obstacle_line_requested(double start_x, double start_y, double end_x,
                               double end_y, double width_m);
  void forbidden_polygon_requested(const QVector<QPointF> &polygon);
  // value: GridMap::kFree / kOccupied / kUnknown
  void fill_polygon_requested(const QVector<QPointF> &polygon, int value);
  void polygon_vertex_count_changed(int count);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

private:
  void rebuild_image();
  void update_cursor_status(const QPointF &position);
  bool screen_to_world(const QPointF &position, GridWorldPoint *world) const;
  QPointF world_to_screen(const GridWorldPoint &world) const;
  void finish_forbidden_polygon();

  bool linked_inspection_enabled_ = false;
  std::vector<std::size_t> highlighted_cells_;
  QImage highlight_image_;
  GridMap map_;
  QImage image_;
  QPointF pan_ = QPointF(0.0, 0.0);
  double zoom_ = 1.0;
  bool panning_ = false;
  QPoint last_mouse_position_;
  QString cursor_status_;
  RefinementModel *refinement_model_ = nullptr;
  OccupancyInteractionMode mode_ = OccupancyInteractionMode::View;
  double obstacle_width_m_ = 0.2;
  bool editing_drag_ = false;
  QPoint edit_start_screen_;
  QPoint edit_current_screen_;
  QVector<QPointF> forbidden_polygon_world_;
};

}  // namespace agt_map_studio
