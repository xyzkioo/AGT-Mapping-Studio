#include "ui/StudioStyle.hpp"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>
#include <QAction>
#include <QStyleOptionToolButton>

namespace agt_map_studio {

void apply_studio_style(QApplication &application) {
  application.setStyle(QStringLiteral("Fusion"));
  QFont font = application.font();
  font.setPointSize(10);
  application.setFont(font);
  QPalette palette;
  palette.setColor(QPalette::Window, QColor("#f3f5f9"));
  palette.setColor(QPalette::WindowText, QColor("#25334a"));
  palette.setColor(QPalette::Base, Qt::white);
  palette.setColor(QPalette::AlternateBase, QColor("#f3f5f9"));
  palette.setColor(QPalette::Text, QColor("#25334a"));
  palette.setColor(QPalette::Button, Qt::white);
  palette.setColor(QPalette::ButtonText, QColor("#25334a"));
  palette.setColor(QPalette::Highlight, QColor("#2563eb"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::ToolTipBase, QColor("#25334a"));
  palette.setColor(QPalette::ToolTipText, Qt::white);
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#94a0b3"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#94a0b3"));
  application.setPalette(palette);
  application.setStyleSheet(QStringLiteral(R"qss(
    QMainWindow { background: #f3f5f9; }
    QMainWindow::separator { width: 6px; height: 6px; background: #f3f5f9; }
    QMainWindow::separator:hover { background: #dce6fa; }
    QLabel { color: #25334a; background: transparent; }
    QLabel[role="muted"] { color: #748197; font-size: 12px; }
    QLabel[role="heading"] { color: #192a44; font-size: 18px; font-weight: 600; }
    QLabel[role="section"] { color: #748197; font-size: 11px; font-weight: 600; }
    QLabel#brandMark { background: #2563eb; color: white; border-radius: 7px;
                       font-size: 13px; font-weight: bold; padding: 7px; }
    QLabel#brandName { font-size: 16px; font-weight: 600; padding: 0 10px 0 2px; }
    QLabel#sourceName { font-weight: 600; }
    QLabel#sourceSummary { color: #748197; padding: 8px; background: #f4f7fc;
                           border-radius: 6px; font-size: 12px; }
    QLabel#emptyTitle { font-size: 25px; font-weight: 600; color: #192a44; }
    QLabel#emptySymbol { color: #2563eb; background: #eaf1ff; border-radius: 20px;
                        font-size: 32px; font-weight: 600; }
    QMenuBar { background: white; color: #526078; padding: 3px 12px; border-bottom: 1px solid #e5eaf1; }
    QMenuBar::item { padding: 5px 10px; background: transparent; border-radius: 4px; }
    QMenuBar::item:selected { background: #edf3ff; color: #2563eb; }
    QMenu { background: white; border: 1px solid #dce3ee; padding: 6px; }
    QMenu::item { padding: 7px 24px 7px 12px; border-radius: 4px; }
    QMenu::item:selected { background: #edf3ff; color: #2563eb; }
    QMenu::separator { height: 1px; background: #e5eaf1; margin: 5px; }
    QToolBar { background: white; border: none; border-bottom: 1px solid #e5eaf1;
               spacing: 5px; padding: 8px 12px; }
    QToolBar::separator { background: #e5eaf1; width: 1px; margin: 5px 8px; }
    QToolButton, QPushButton { color: #526078; background: white; border: 1px solid #dce3ee;
                              border-radius: 6px; padding: 7px 11px; }
    QToolButton:hover, QPushButton:hover { background: #f3f7ff; border-color: #b6c9ef; color: #2563eb; }
    QToolButton:pressed, QPushButton:pressed { background: #dce8ff; }
    QToolButton:checked, QPushButton:checked { background: #eaf1ff; color: #2563eb; border-color: #c5d7fb; }
    QToolButton:disabled, QPushButton:disabled { color: #9ca8ba; background: #f6f8fb; border-color: #e7ecf3; }
    QPushButton[role="primary"], QToolButton[role="primary"] { background: #2563eb; color: white;
                                                               border-color: #2563eb; font-weight: 600; }
    QPushButton[role="primary"]:hover, QToolButton[role="primary"]:hover { background: #1d4ed8; }
    QPushButton[role="primary"]:disabled, QToolButton[role="primary"]:disabled {
      background: #e7edf8; color: #98a8c3; border-color: #e7edf8;
    }
    QToolButton[role="danger"] { color: #c44a54; }
    QToolButton[role="danger"]:checked { background: #fff0f1; color: #bc3444; border-color: #f2c5ca; }
    QToolButton[role="danger"]:disabled { color: #aeb5c1; }
    QToolButton#viewSwitch { border: none; background: transparent; padding: 8px 14px; }
    QToolButton#viewSwitch:checked { background: #eaf1ff; color: #2563eb; }
    QToolButton#viewSwitch:hover { background: #f0f5ff; }
    QFrame#workspaceHeader { background: white; border-bottom: 1px solid #e5eaf1; }
    QWidget#emptyWorkspace { background: #fafbfd; }
    QDockWidget { color: #526078; font-weight: 600; }
    QDockWidget::title { background: white; padding: 12px; border-bottom: 1px solid #e5eaf1; }
    QWidget#editorPanel, QWidget#editorPage, QWidget#workflowPanel, QScrollArea, QWidget#workflowContent { background: white; }
    QFrame#workflowCard { background: white; border: 1px solid #e4eaf3; border-radius: 8px; }
    QLabel#stepNumber { background: #eef3fc; color: #57739f; border-radius: 7px; font-weight: 600; }
    QLabel#stepTitle { font-size: 13px; font-weight: 600; }
    QGroupBox { background: white; border: 1px solid #e4eaf3; border-radius: 8px;
                margin-top: 18px; padding: 14px 10px 10px; font-weight: 600; }
    QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;
                       left: 10px; padding: 0 5px; color: #526078; }
    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { background: #fafbfd; color: #25334a;
      border: 1px solid #dce3ee; border-radius: 5px; padding: 6px; min-height: 18px;
      selection-background-color: #2563eb;
    }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #7ca3f4; }
    QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
      color: #9ca8ba; background: #f6f8fb; border-color: #e7ecf3;
    }
    QComboBox::drop-down { border: none; width: 24px; }
    QComboBox QAbstractItemView { background: white; border: 1px solid #dce3ee;
      selection-background-color: #eaf1ff; selection-color: #2563eb; padding: 4px;
    }
    QCheckBox { spacing: 7px; color: #526078; font-weight: normal; }
    QCheckBox::indicator { width: 15px; height: 15px; }
    QTabWidget::pane { border: none; background: white; }
    QTabBar::tab { background: white; color: #748197; padding: 10px 15px; border-bottom: 2px solid transparent; }
    QTabBar::tab:selected { color: #2563eb; border-bottom-color: #2563eb; }
    QTabBar::tab:hover { background: #f4f7fc; }
    QPlainTextEdit { background: #f6f8fc; color: #526078; border: 1px solid #e4eaf3;
                     border-radius: 7px; padding: 8px; font-family: monospace; font-size: 12px; }
    QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
    QScrollBar::handle:vertical { background: #cdd6e4; border-radius: 3px; min-height: 32px; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    QProgressBar { background: #eaf0f8; border: none; border-radius: 3px; max-height: 6px; }
    QProgressBar::chunk { background: #2563eb; border-radius: 3px; }
    QStatusBar { background: white; color: #748197; border-top: 1px solid #e5eaf1; padding: 5px 10px; }
    QStatusBar::item { border: none; }
    QStatusBar QLabel { color: #748197; font-size: 12px; }
    /* Quiet tool rows, canvas controls and an optional workflow drawer. */
    QMainWindow { background: #f5f6f8; }
    QMainWindow::separator { width: 6px; height: 6px; background: #f5f6f8; }
    QMenuBar { border: none; background: white; padding: 2px 14px; }
    QLabel#brandMark { background: #edf2fa; color: #496791; padding: 6px 8px; border-radius: 6px; }
    QLabel#brandName { font-size: 16px; padding: 0 18px 0 6px; }
    QToolBar { background: transparent; border: none; spacing: 5px; padding: 0; }
    QToolBar#workspaceToolbar { background: white; padding: 12px 18px; border-bottom: 1px solid #eceef2; }
    QToolButton { background: transparent; border: 1px solid transparent; color: #536071; padding: 7px 10px; }
    QToolButton:hover { background: #f0f3f8; border-color: transparent; }
    QToolButton:checked { background: #eaf0fb; border-color: transparent; color: #2563eb; }
    QToolButton:disabled { background: transparent; border-color: transparent; color: #abb3bf; }
    QToolButton#toolRow { border: none; background: transparent; border-radius: 6px; padding: 0; }
    QToolButton#toolRow:hover { background: #f1f3f7; }
    QToolButton#toolRow:checked { background: #eaf0fb; }
    QToolButton#toolRow:focus { border: 1px solid #9cb8e9; }
    QToolButton::menu-button { background: transparent; border: none; width: 18px; }
    QToolButton::menu-button:hover { background: #eaf0fb; }
    QToolButton::menu-arrow { width: 7px; height: 7px; }
    QToolButton[role="primary"]::menu-button { background: #2563eb; }
    QToolButton#toolRow::menu-button { background: transparent; border: none; width: 22px; }
    QToolButton#workflowToggle { padding: 8px 12px; background: #f3f5f8; }
    QToolButton#workflowToggle:checked { background: #eaf0fb; }
    QFrame#workspaceHeader { background: white; border: none; border-top-left-radius: 12px; border-top-right-radius: 12px; }
    QLabel#sourceName { font-size: 20px; font-weight: 600; color: #263448; }
    QFrame#viewControl { background: #f0f2f6; border-radius: 8px; }
    QToolButton#viewSwitch { background: transparent; border: 1px solid transparent; padding: 7px 12px; }
    QToolButton#viewSwitch:checked { background: white; border: 1px solid #e5e9f0; border-radius: 6px; }
    QToolButton#viewSwitch:hover { background: #f9fafc; }
    QDockWidget { font-weight: normal; color: #707d8f; }
    QDockWidget::title { padding: 14px 16px 6px; border: none; background: white; }
    QTabBar::tab { color: #8591a2; padding: 10px 16px; border-bottom: 2px solid transparent; }
    QTabBar::tab:selected { color: #344863; border-bottom-color: #7193c1; }
    QLabel[role="section"] { color: #8a96a6; font-size: 11px; font-weight: 600; padding: 3px 10px; }
    QLabel[role="muted"] { color: #8994a3; font-size: 12px; }
    QWidget#occupancyParameters { background: #f4f6fa; border-radius: 7px; }
    QLabel#sourceSummary { background: #f4f6fa; color: #8491a2; padding: 10px; }
    QFrame#workflowCard { background: white; border: none; border-bottom: 1px solid #edf0f4; border-radius: 0; }
    QFrame#workflowCard QPushButton { background: transparent; border-color: #edf0f4; padding: 5px 9px; }
    QFrame#workflowCard QPushButton:disabled { border-color: transparent; background: transparent; }
    QSplitter::handle:horizontal { background: #eef0f4; }
    QSplitter::handle:horizontal:hover { background: #c4d4eb; }
    QStatusBar { background: #f5f6f8; border: none; padding: 3px 12px; }
    QToolTip { background: #25334a; color: white; border: none; padding: 6px; }
  )qss"));
}

QIcon studio_icon(StudioIcon icon, bool light) {
  QIcon result;
  for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Active}) {
    for (const int scale : {1, 2}) {
      QPixmap pixmap(20 * scale, 20 * scale);
      pixmap.setDevicePixelRatio(scale);
      pixmap.fill(Qt::transparent);
      QPainter painter(&pixmap);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setPen(QPen(mode == QIcon::Disabled ? QColor("#aeb9ca") : light ? QColor(Qt::white) : QColor("#61758a"),
                          1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      QPainterPath path;
      switch (icon) {
        case StudioIcon::Open:
          path.moveTo(2, 6); path.lineTo(2, 16); path.lineTo(17, 16);
          path.lineTo(19, 8); path.lineTo(7, 8); path.lineTo(5, 11); path.lineTo(2, 16);
          path.moveTo(2, 6); path.lineTo(7, 6); path.lineTo(9, 4); path.lineTo(16, 4); path.lineTo(16, 8);
          break;
        case StudioIcon::Cloud:
          path.moveTo(10, 2); path.lineTo(18, 6); path.lineTo(18, 14); path.lineTo(10, 18);
          path.lineTo(2, 14); path.lineTo(2, 6); path.closeSubpath();
          path.moveTo(2, 6); path.lineTo(10, 10); path.lineTo(18, 6);
          path.moveTo(10, 10); path.lineTo(10, 18);
          break;
        case StudioIcon::Map:
          path.moveTo(2, 4); path.lineTo(7, 2); path.lineTo(13, 5); path.lineTo(18, 3);
          path.lineTo(18, 16); path.lineTo(13, 18); path.lineTo(7, 15); path.lineTo(2, 17); path.closeSubpath();
          path.moveTo(7, 2); path.lineTo(7, 15); path.moveTo(13, 5); path.lineTo(13, 18);
          break;
        case StudioIcon::Navigate:
          path.moveTo(5, 2); path.lineTo(5, 17); path.lineTo(9, 13); path.lineTo(12, 18);
          path.lineTo(15, 16); path.lineTo(12, 11); path.lineTo(18, 10); path.closeSubpath();
          break;
        case StudioIcon::Select:
          painter.setPen(QPen(mode == QIcon::Disabled ? QColor("#aeb9ca") : light ? QColor(Qt::white) : QColor("#61758a"), 1.5, Qt::DashLine));
          path.addRect(QRectF(3, 3, 14, 14));
          break;
        case StudioIcon::Delete:
          path.moveTo(3, 5); path.lineTo(17, 5); path.moveTo(7, 5); path.lineTo(7, 2);
          path.lineTo(13, 2); path.lineTo(13, 5); path.moveTo(5, 5); path.lineTo(6, 18);
          path.lineTo(14, 18); path.lineTo(15, 5); path.moveTo(9, 8); path.lineTo(9, 15);
          path.moveTo(12, 8); path.lineTo(12, 15);
          break;
        case StudioIcon::Redo: painter.translate(20, 0); painter.scale(-1, 1); [[fallthrough]];
        case StudioIcon::Undo:
          path.moveTo(8, 3); path.lineTo(3, 8); path.lineTo(8, 12);
          path.moveTo(3, 8); path.lineTo(12, 8); path.cubicTo(19, 8, 19, 17, 12, 17);
          break;
        case StudioIcon::Fit:
          for (int i = 0; i < 4; ++i) {
            painter.save(); painter.translate(10, 10); painter.rotate(i * 90);
            painter.drawLine(QPointF(-7, -3), QPointF(-7, -7));
            painter.drawLine(QPointF(-7, -7), QPointF(-3, -7)); painter.restore();
          }
          break;
        case StudioIcon::Inspect:
          path.addEllipse(QRectF(2, 2, 11, 11));
          path.moveTo(11, 11); path.lineTo(18, 18);
          path.moveTo(5, 7.5); path.lineTo(10, 7.5);
          path.moveTo(7.5, 5); path.lineTo(7.5, 10);
          break;
        case StudioIcon::Erase:
          path.moveTo(3, 11); path.lineTo(11, 3); path.lineTo(17, 9);
          path.lineTo(9, 17); path.lineTo(7, 17); path.closeSubpath();
          path.moveTo(7, 7); path.lineTo(13, 13);
          path.moveTo(3, 18); path.lineTo(18, 18);
          break;
        case StudioIcon::Line:
          path.moveTo(4, 16); path.lineTo(16, 4);
          path.addEllipse(QRectF(2, 14, 4, 4)); path.addEllipse(QRectF(14, 2, 4, 4));
          break;
        case StudioIcon::Marker:
          path.addRoundedRect(QRectF(4, 4, 12, 12), 2, 2);
          path.moveTo(7, 10); path.lineTo(13, 10);
          path.moveTo(10, 7); path.lineTo(10, 13);
          break;
        case StudioIcon::Rectangle:
          path.addRoundedRect(QRectF(3, 4, 14, 12), 1, 1);
          path.moveTo(6, 7); path.lineTo(14, 13);
          break;
        case StudioIcon::Polygon:
          path.moveTo(3, 14); path.lineTo(6, 3); path.lineTo(16, 6);
          path.lineTo(17, 15); path.lineTo(9, 18); path.closeSubpath();
          break;
        case StudioIcon::Forbidden:
          path.addEllipse(QRectF(3, 3, 14, 14));
          path.moveTo(5, 15); path.lineTo(15, 5);
          break;
        case StudioIcon::Workflow:
          path.addRoundedRect(QRectF(2, 2, 5, 5), 1, 1);
          path.addRoundedRect(QRectF(13, 13, 5, 5), 1, 1);
          path.moveTo(7, 4.5); path.lineTo(15.5, 4.5); path.lineTo(15.5, 10);
          path.moveTo(13, 8); path.lineTo(15.5, 10.5); path.lineTo(18, 8);
          break;
        case StudioIcon::Settings:
          path.moveTo(3, 5); path.lineTo(17, 5);
          path.moveTo(3, 10); path.lineTo(17, 10);
          path.moveTo(3, 15); path.lineTo(17, 15);
          painter.setBrush(QColor("#ffffff"));
          path.addEllipse(QRectF(5, 3, 4, 4));
          path.addEllipse(QRectF(11, 8, 4, 4));
          path.addEllipse(QRectF(6, 13, 4, 4));
          break;
        case StudioIcon::Save:
          path.addRoundedRect(QRectF(3, 2, 14, 16), 1, 1);
          path.addRect(QRectF(6, 2, 8, 5)); path.addRect(QRectF(6, 11, 8, 7));
          break;
      }
      painter.drawPath(path);
      painter.end();
      result.addPixmap(pixmap, mode);
    }
  }
  return result;
}

StudioToolButton::StudioToolButton(QWidget *parent) : QToolButton(parent) {
  setObjectName(QStringLiteral("toolRow"));
  setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setIconSize(QSize(20, 20));
}

QSize StudioToolButton::sizeHint() const { return QSize(216, 38); }
QSize StudioToolButton::minimumSizeHint() const { return QSize(180, 38); }

void StudioToolButton::paintEvent(QPaintEvent *) {
  QStyleOptionToolButton option;
  initStyleOption(&option);
  option.text.clear();
  option.icon = QIcon();
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);
  icon().paint(&painter, QRect(10, (height() - 20) / 2, 20, 20), Qt::AlignCenter,
               isEnabled() ? QIcon::Normal : QIcon::Disabled);
  const QString shortcut = defaultAction() ? defaultAction()->shortcut().toString(QKeySequence::NativeText) : QString();
  const int reserved = shortcut.isEmpty() ? (menu() ? 22 : 10) : painter.fontMetrics().horizontalAdvance(shortcut) + 18;
  const QRect label_rect(40, 0, width() - 40 - reserved, height());
  painter.setPen(!isEnabled() ? QColor("#a3acb8") : isChecked() ? QColor("#2563eb") : QColor("#364152"));
  painter.drawText(label_rect, Qt::AlignLeft | Qt::AlignVCenter,
                   painter.fontMetrics().elidedText(text(), Qt::ElideRight, label_rect.width()));
  if (!shortcut.isEmpty()) {
    painter.setPen(QColor("#929ba8"));
    painter.drawText(QRect(width() - reserved, 0, reserved - 10, height()), Qt::AlignRight | Qt::AlignVCenter, shortcut);
  }
}

}  // namespace agt_map_studio
