#include "occupancy/OccupancyViewer.hpp"

#include "occupancy/RefinementModel.hpp"

#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace agt_map_studio {

OccupancyViewer::OccupancyViewer(QWidget *parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setMinimumSize(320, 240);
}

void OccupancyViewer::set_refinement_model(RefinementModel *model) {
  refinement_model_ = model;
  refresh();
}

void OccupancyViewer::set_map(GridMap map) {
  highlighted_cells_.clear();
  highlight_image_ = QImage();
  map_ = std::move(map);
  rebuild_image();
  fit_map();
  cursor_status_.clear();
  emit status_changed(QStringLiteral("2D map: %1 x %2 | resolution: %3 m")
                          .arg(map_.width())
                          .arg(map_.height())
                          .arg(map_.resolution(), 0, 'f', 3));
  update();
}

void OccupancyViewer::clear_map() {
  highlighted_cells_.clear();
  highlight_image_ = QImage();
  map_.clear();
  image_ = QImage();
  cursor_status_.clear();
  update();
}

void OccupancyViewer::refresh() {
  rebuild_image();
  update();
}

void OccupancyViewer::set_mode(OccupancyInteractionMode mode) {
  mode_ = mode;
  editing_drag_ = false;
  if (!occupancy_mode_uses_polygon(mode_)) {
    forbidden_polygon_world_.clear();
    emit polygon_vertex_count_changed(0);
  }
  emit status_changed(QStringLiteral("2D mode: %1").arg(mode_name(mode_)));
  update();
}

QString OccupancyViewer::mode_name(OccupancyInteractionMode mode) {
  switch (mode) {
    case OccupancyInteractionMode::InspectXY: return QStringLiteral("Inspect XY");
    case OccupancyInteractionMode::View: return QStringLiteral("View");
    case OccupancyInteractionMode::Erase: return QStringLiteral("Erase");
    case OccupancyInteractionMode::Obstacle: return QStringLiteral("Obstacle");
    case OccupancyInteractionMode::ObstacleMarker: return QStringLiteral("Mark obstacle");
    case OccupancyInteractionMode::Forbidden: return QStringLiteral("Forbidden");
    case OccupancyInteractionMode::FreeRectangle: return QStringLiteral("Free rectangle");
    case OccupancyInteractionMode::FreePolygon: return QStringLiteral("Free polygon");
    case OccupancyInteractionMode::OccupiedPolygon: return QStringLiteral("Occupied polygon");
    case OccupancyInteractionMode::UnknownPolygon: return QStringLiteral("Unknown polygon");
  }
  return QString();
}

void OccupancyViewer::cancel_polygon() {
  editing_drag_ = false;
  forbidden_polygon_world_.clear();
  emit polygon_vertex_count_changed(0);
  update();
}

void OccupancyViewer::pop_polygon_vertex() {
  if (forbidden_polygon_world_.isEmpty()) return;
  forbidden_polygon_world_.removeLast();
  emit polygon_vertex_count_changed(forbidden_polygon_world_.size());
  update();
}

void OccupancyViewer::finish_polygon() { finish_forbidden_polygon(); }

void OccupancyViewer::set_obstacle_width(double width_m) {
  if (width_m > 0.0) obstacle_width_m_ = width_m;
  update();
}

void OccupancyViewer::rebuild_image() {
  image_ = QImage();
  if (map_.empty() || map_.width() > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      map_.height() > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return;
  }
  image_ = QImage(static_cast<int>(map_.width()), static_cast<int>(map_.height()),
                  QImage::Format_Grayscale8);
  for (std::uint32_t grid_y = 0; grid_y < map_.height(); ++grid_y) {
    auto *line = image_.scanLine(static_cast<int>(map_.height() - 1U - grid_y));
    for (std::uint32_t x = 0; x < map_.width(); ++x) {
      const std::int8_t value = refinement_model_
                                    ? refinement_model_->effective_at(x, grid_y)
                                    : map_.at(x, grid_y);
      line[x] = value == GridMap::kOccupied
                    ? 0U
                    : (value == GridMap::kFree ? 255U : 160U);
    }
  }
}

void OccupancyViewer::set_linked_inspection_enabled(bool enabled) {
  linked_inspection_enabled_ = enabled;
  if (!enabled) set_highlighted_cells({});
}

void OccupancyViewer::set_highlighted_cells(std::vector<std::size_t> cells) {
  std::sort(cells.begin(), cells.end());
  cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
  cells.erase(std::remove_if(cells.begin(), cells.end(), [this](std::size_t i) {
    return i >= map_.cells().size();
  }), cells.end());
  if (cells == highlighted_cells_) return;
  highlighted_cells_ = std::move(cells);
  highlight_image_ = QImage();
  if (!map_.empty() && !highlighted_cells_.empty()) {
    highlight_image_ = QImage(map_.width(), map_.height(), QImage::Format_ARGB32);
    highlight_image_.fill(Qt::transparent);
    for (const auto i : highlighted_cells_) {
      highlight_image_.setPixel(i % map_.width(), map_.height() - 1 - i / map_.width(),
                               qRgba(240, 35, 35, 255));
    }
  }
  update();
}

void OccupancyViewer::reset_view() {
  zoom_ = 1.0;
  pan_ = QPointF((width() - image_.width()) * 0.5,
                 (height() - image_.height()) * 0.5);
  update();
}

void OccupancyViewer::fit_map() {
  if (image_.isNull() || width() <= 0 || height() <= 0) return;
  const double margin = 20.0;
  zoom_ = std::clamp(std::min((width() - margin) / image_.width(),
                              (height() - margin) / image_.height()),
                     0.05, 50.0);
  pan_ = QPointF((width() - image_.width() * zoom_) * 0.5,
                 (height() - image_.height() * zoom_) * 0.5);
  update();
}

void OccupancyViewer::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#edf1f7"));
  if (!image_.isNull()) {
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.save();
    painter.translate(pan_);
    painter.scale(zoom_, zoom_);
    painter.drawImage(QPointF(0.0, 0.0), image_);
    painter.drawImage(QPointF(0.0, 0.0), highlight_image_);
    painter.restore();
  } else {
    painter.setPen(QColor("#526078"));
    painter.drawText(rect(), Qt::AlignCenter,
                     QStringLiteral("Open a Nav2 map.yaml to view occupancy map"));
  }
  painter.setPen(QColor("#526078"));
  const QString dimensions = map_.empty()
                                 ? QStringLiteral("No map")
                                 : QStringLiteral("Map: %1 x %2 | %3 m/cell | zoom %4x")
                                       .arg(map_.width())
                                       .arg(map_.height())
                                       .arg(map_.resolution(), 0, 'f', 3)
                                       .arg(zoom_, 0, 'f', 2);
  painter.fillRect(8, 8, std::min(width() - 16, 360), 24,
                   QColor(255, 255, 255, 235));
  painter.drawText(14, 25, dimensions);
  if (!cursor_status_.isEmpty()) {
    painter.fillRect(8, height() - 34, std::min(width() - 16, 620), 24,
                     QColor(255, 255, 255, 235));
    painter.drawText(14, height() - 17, cursor_status_);
  }
  if (!image_.isNull() && refinement_model_) {
    painter.save();
    painter.setPen(QPen(QColor(220, 30, 30, 220), 2));
    painter.setBrush(QColor(230, 30, 30, 65));
    for (const auto &zone : refinement_model_->forbidden_zones()) {
      QPolygonF polygon;
      for (const auto &point : zone.polygon) polygon << world_to_screen(point);
      if (polygon.size() >= 3) painter.drawPolygon(polygon);
    }
    painter.setPen(QPen(QColor(230, 30, 30, 230), 2, Qt::DashLine));
    painter.setBrush(QColor(230, 30, 30, 45));
    if ((mode_ == OccupancyInteractionMode::Erase ||
         mode_ == OccupancyInteractionMode::FreeRectangle ||
         mode_ == OccupancyInteractionMode::InspectXY) && editing_drag_) {
      if (mode_ == OccupancyInteractionMode::FreeRectangle) {
        painter.setPen(QPen(QColor(40, 170, 80, 230), 2, Qt::DashLine));
        painter.setBrush(QColor(40, 170, 80, 45));
      }
      painter.drawRect(QRect(edit_start_screen_, edit_current_screen_).normalized());
    } else if (mode_ == OccupancyInteractionMode::Obstacle && editing_drag_) {
      const double pixel_width = std::max(2.0, obstacle_width_m_ /
                                                    map_.resolution() * zoom_);
      painter.setPen(QPen(QColor(40, 110, 240, 230), pixel_width,
                          Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawLine(edit_start_screen_, edit_current_screen_);
    } else if (occupancy_mode_uses_polygon(mode_) &&
               !forbidden_polygon_world_.isEmpty()) {
      QPolygonF polygon;
      for (const auto &point : forbidden_polygon_world_) {
        polygon << world_to_screen({point.x(), point.y()});
      }
      QColor color(240, 30, 30);
      if (mode_ == OccupancyInteractionMode::FreePolygon) color = QColor(40, 170, 80);
      if (mode_ == OccupancyInteractionMode::OccupiedPolygon) color = QColor(40, 110, 240);
      if (mode_ == OccupancyInteractionMode::UnknownPolygon) color = QColor(120, 120, 120);
      painter.setPen(QPen(QColor(color.red(), color.green(), color.blue(), 230), 2,
                          Qt::DashLine));
      painter.setBrush(QColor(color.red(), color.green(), color.blue(), 65));
      if (!polygon.isEmpty()) {
        painter.drawLine(polygon.last(), QPointF(last_mouse_position_));
        painter.drawText(polygon.last() + QPointF(8, -8),
                         QStringLiteral("%1 pts | double-click/Enter close | Backspace undo | Esc")
                             .arg(polygon.size()));
      }
      painter.drawPolyline(polygon);
      if (polygon.size() >= 3) painter.drawLine(polygon.last(), polygon.first());
    }
    painter.restore();
  }
}

void OccupancyViewer::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (!has_map()) return;
  if (event->oldSize().isEmpty()) fit_map();
  update();
}

void OccupancyViewer::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    cancel_polygon();
    if (linked_inspection_enabled_ && mode_ == OccupancyInteractionMode::InspectXY) {
      emit clear_inspection_requested();
      set_highlighted_cells({});
    }
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Backspace && occupancy_mode_uses_polygon(mode_)) {
    pop_polygon_vertex();
    event->accept();
    return;
  }
  if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
      occupancy_mode_uses_polygon(mode_)) {
    finish_forbidden_polygon();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_R) {
    reset_view();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_F) {
    fit_map();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void OccupancyViewer::mousePressEvent(QMouseEvent *event) {
  setFocus();
  if (event->button() == Qt::LeftButton && mode_ == OccupancyInteractionMode::InspectXY &&
      !linked_inspection_enabled_) {
    emit inspection_unavailable();
  } else if (event->button() == Qt::RightButton ||
             (event->button() == Qt::LeftButton && mode_ == OccupancyInteractionMode::View)) {
    panning_ = true;
    last_mouse_position_ = event->pos();
  } else if (event->button() == Qt::LeftButton && occupancy_mode_uses_polygon(mode_)) {
    GridWorldPoint world;
    if (screen_to_world(QPointF(event->pos()), &world)) {
      forbidden_polygon_world_.push_back(QPointF(world.x, world.y));
      emit polygon_vertex_count_changed(forbidden_polygon_world_.size());
      update();
    }
  } else if (event->button() == Qt::LeftButton) {
    GridWorldPoint world;
    if (screen_to_world(QPointF(event->pos()), &world)) {
      editing_drag_ = true;
      edit_start_screen_ = event->pos();
      edit_current_screen_ = event->pos();
    }
  }
  event->accept();
}

void OccupancyViewer::mouseMoveEvent(QMouseEvent *event) {
  if (occupancy_mode_uses_polygon(mode_) && !forbidden_polygon_world_.isEmpty()) {
    last_mouse_position_ = event->pos();
    update();
  }
  if (panning_) {
    pan_ += QPointF(event->pos() - last_mouse_position_);
    last_mouse_position_ = event->pos();
    update();
  } else if (editing_drag_) {
    edit_current_screen_ = event->pos();
    update();
  }
  update_cursor_status(QPointF(event->pos()));
  event->accept();
}

void OccupancyViewer::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) panning_ = false;
  if (event->button() == Qt::LeftButton && editing_drag_) {
    edit_current_screen_ = event->pos();
    GridWorldPoint first;
    GridWorldPoint second;
    const bool valid = screen_to_world(QPointF(edit_start_screen_), &first) &&
                       screen_to_world(QPointF(edit_current_screen_), &second);
    editing_drag_ = false;
    if (valid && mode_ == OccupancyInteractionMode::InspectXY && linked_inspection_enabled_) {
      // screen_to_world returns cell corners. Include both endpoint cells,
      // even for a single click, and use half-open XY bounds in the cloud.
      const double max_x = std::max(first.x, second.x) + map_.resolution();
      const double max_y = std::max(first.y, second.y) + map_.resolution();
      std::vector<std::size_t> cells;
      int x0, y0, x1, y1;
      if (map_.world_to_pixel(first.x, first.y, &x0, &y0) &&
          map_.world_to_pixel(second.x, second.y, &x1, &y1)) {
        for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
          for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x)
            cells.push_back(static_cast<std::size_t>(y) * map_.width() + x);
      }
      emit inspect_xy_requested(std::min(first.x, second.x), std::min(first.y, second.y),
                                max_x, max_y);
      set_highlighted_cells(std::move(cells));
    } else if (valid && mode_ == OccupancyInteractionMode::ObstacleMarker &&
               (edit_current_screen_ - edit_start_screen_).manhattanLength() <= 4) {
      // Snap to whole cells, with a minimum footprint of one cell. Reuse
      // occupied polygon edits so history, undo and patch export stay identical.
      const double resolution = map_.resolution();
      const int cells = std::max(1, static_cast<int>(std::ceil(obstacle_width_m_ / resolution)));
      const double offset = ((cells - 1) / 2) * resolution;
      const double x0 = first.x - offset;
      const double y0 = first.y - offset;
      const double x1 = x0 + cells * resolution;
      const double y1 = y0 + cells * resolution;
      emit clear_inspection_requested();
      set_highlighted_cells({});
      emit fill_polygon_requested(
          QVector<QPointF>{QPointF(x0, y0), QPointF(x1, y0),
                          QPointF(x1, y1), QPointF(x0, y1)}, GridMap::kOccupied);
    } else if (valid && mode_ == OccupancyInteractionMode::Erase) {
      emit erase_rectangle_requested(std::min(first.x, second.x),
                                     std::min(first.y, second.y),
                                     std::max(first.x, second.x),
                                     std::max(first.y, second.y));
    } else if (valid && mode_ == OccupancyInteractionMode::FreeRectangle &&
               first.x != second.x && first.y != second.y) {
      const double min_x = std::min(first.x, second.x);
      const double min_y = std::min(first.y, second.y);
      const double max_x = std::max(first.x, second.x);
      const double max_y = std::max(first.y, second.y);
      emit fill_polygon_requested(
          QVector<QPointF>{QPointF(min_x, min_y), QPointF(max_x, min_y),
                          QPointF(max_x, max_y), QPointF(min_x, max_y)},
          GridMap::kFree);
    } else if (valid && mode_ == OccupancyInteractionMode::Obstacle &&
               edit_start_screen_ != edit_current_screen_) {
      emit obstacle_line_requested(first.x, first.y, second.x, second.y,
                                   obstacle_width_m_);
    }
    update();
  }
  event->accept();
}

void OccupancyViewer::mouseDoubleClickEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && occupancy_mode_uses_polygon(mode_)) {
    finish_forbidden_polygon();
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void OccupancyViewer::wheelEvent(QWheelEvent *event) {
  if (image_.isNull()) return;
  const QPointF cursor(event->pos());
  const QPointF image_before = (cursor - pan_) / zoom_;
  const double steps = event->angleDelta().y() / 120.0;
  zoom_ = std::clamp(zoom_ * std::pow(1.15, steps), 0.05, 50.0);
  pan_ = cursor - image_before * zoom_;
  update_cursor_status(cursor);
  update();
  event->accept();
}

void OccupancyViewer::update_cursor_status(const QPointF &position) {
  if (map_.empty() || zoom_ <= 0.0) return;
  const QPointF image_position = (position - pan_) / zoom_;
  const int image_x = static_cast<int>(std::floor(image_position.x()));
  const int image_y = static_cast<int>(std::floor(image_position.y()));
  if (image_x < 0 || image_y < 0 || image_x >= static_cast<int>(map_.width()) ||
      image_y >= static_cast<int>(map_.height())) {
    cursor_status_.clear();
    return;
  }
  const int grid_y = static_cast<int>(map_.height()) - 1 - image_y;
  const GridWorldPoint world = map_.pixel_to_world(image_x, grid_y);
  const std::int8_t occupancy = refinement_model_
                                    ? refinement_model_->effective_at(
                                          static_cast<std::uint32_t>(image_x),
                                          static_cast<std::uint32_t>(grid_y))
                                    : map_.at(static_cast<std::uint32_t>(image_x),
                                              static_cast<std::uint32_t>(grid_y));
  cursor_status_ = QStringLiteral("pixel=(%1,%2) world=(%3,%4) occupancy=%5")
                       .arg(image_x)
                       .arg(grid_y)
                       .arg(world.x, 0, 'f', 3)
                       .arg(world.y, 0, 'f', 3)
                       .arg(occupancy);
  emit status_changed(cursor_status_);
  update();
}

bool OccupancyViewer::screen_to_world(const QPointF &position,
                                      GridWorldPoint *world) const {
  if (!world || map_.empty() || zoom_ <= 0.0) return false;
  const QPointF image_position = (position - pan_) / zoom_;
  const int image_x = static_cast<int>(std::floor(image_position.x()));
  const int image_y = static_cast<int>(std::floor(image_position.y()));
  if (image_x < 0 || image_y < 0 || image_x >= static_cast<int>(map_.width()) ||
      image_y >= static_cast<int>(map_.height())) {
    return false;
  }
  const int grid_y = static_cast<int>(map_.height()) - 1 - image_y;
  const auto converted = map_.pixel_to_world(image_x, grid_y);
  *world = converted;
  return true;
}

QPointF OccupancyViewer::world_to_screen(const GridWorldPoint &world) const {
  const double grid_x = (world.x - map_.origin_x()) / map_.resolution();
  const double grid_y = (world.y - map_.origin_y()) / map_.resolution();
  const double image_y = static_cast<double>(map_.height()) - 1.0 - grid_y;
  return pan_ + QPointF(grid_x, image_y) * zoom_;
}

void OccupancyViewer::finish_forbidden_polygon() {
  if (forbidden_polygon_world_.size() >= 3U) {
    if (forbidden_polygon_world_.size() >= 2U &&
        forbidden_polygon_world_.back() == forbidden_polygon_world_[
                                                   forbidden_polygon_world_.size() - 2U]) {
      forbidden_polygon_world_.removeLast();
    }
    if (forbidden_polygon_world_.size() >= 3U) {
      if (mode_ == OccupancyInteractionMode::Forbidden) {
        emit forbidden_polygon_requested(forbidden_polygon_world_);
      } else if (mode_ == OccupancyInteractionMode::FreePolygon) {
        emit fill_polygon_requested(forbidden_polygon_world_, GridMap::kFree);
      } else if (mode_ == OccupancyInteractionMode::OccupiedPolygon) {
        emit fill_polygon_requested(forbidden_polygon_world_, GridMap::kOccupied);
      } else if (mode_ == OccupancyInteractionMode::UnknownPolygon) {
        emit fill_polygon_requested(forbidden_polygon_world_, GridMap::kUnknown);
      }
    }
  }
  forbidden_polygon_world_.clear();
  emit polygon_vertex_count_changed(0);
  update();
}

}  // namespace agt_map_studio
