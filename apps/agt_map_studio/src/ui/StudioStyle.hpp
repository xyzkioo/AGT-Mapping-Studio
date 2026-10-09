#pragma once

#include <QIcon>
#include <QToolButton>

class QApplication;

namespace agt_map_studio {

enum class StudioIcon {
  Open, Cloud, Map, Navigate, Select, Delete, Undo, Redo, Fit, Save,
  Inspect, Erase, Line, Marker, Rectangle, Polygon, Forbidden, Workflow, Settings
};

// A navigation-style tool row with a left aligned icon, label and shortcut.
class StudioToolButton : public QToolButton {
public:
  explicit StudioToolButton(QWidget *parent = nullptr);
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;
};

void apply_studio_style(QApplication &application);
QIcon studio_icon(StudioIcon icon, bool light = false);

}  // namespace agt_map_studio
