// End-to-end test of the existing navigation action; no extra UI controls.
#include "ui/MainWindow.hpp"
#include "ui/StudioStyle.hpp"
#include "viewer/PointCloudViewer.hpp"
#include "occupancy/OccupancyViewer.hpp"
#include "ui/WorkflowPanel.hpp"
#include "workflow/WorkflowSession.hpp"
#include "ui/StudioFileDialog.hpp"
#include <QScrollBar>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QImage>
#include <cmath>
#include <QMessageBox>
#include <QAbstractButton>
#include <yaml-cpp/yaml.h>
#include <QApplication>
#include <QAction>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTimer>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <QDir>
#include <iostream>

int main(int argc,char **argv) {
  QApplication application(argc,argv);
  agt_map_studio::apply_studio_style(application);
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--check-edit-persistence") {
    using namespace agt_map_studio;
    QTemporaryDir temp;
    const auto write = [](const QString &path, const QByteArray &data) {
      QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    const QByteArray pcd = "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH 3\nHEIGHT 1\nPOINTS 3\nDATA ascii\n0 0 0\n1 0 0\n2 0 0\n";
    if (!write(temp.filePath("map.pcd"), pcd)) return 110;
    QString error;
    MainWindow first{QString()};
    if (!first.open_pcd(temp.filePath("map.pcd"), &error)) return 111;
    auto *viewer = first.findChild<PointCloudViewer*>();
    QMetaObject::invokeMethod(&first, "set_mode_delete", Qt::DirectConnection);
    viewer->select_xy_region(-.5, -.5, .5, .5);
    QMetaObject::invokeMethod(&first, "delete_selected", Qt::DirectConnection);
    if (!first.save_session(&error)) { std::cerr << error.toStdString(); return 112; }
    const QString session_path = temp.filePath("map_studio/studio_session.yaml");
    MainWindow restored{QString()};
    if (!restored.open_session(session_path, &error)) { std::cerr << error.toStdString(); return 113; }
    auto *back = restored.findChild<PointCloudViewer*>();
    QMetaObject::invokeMethod(&restored, "set_mode_delete", Qt::DirectConnection);
    if (back->stats_text() != viewer->stats_text()) return 114;
    QMetaObject::invokeMethod(&restored, "undo_edit", Qt::DirectConnection);
    if (back->stats_text() == viewer->stats_text()) return 115;
    QMetaObject::invokeMethod(&restored, "redo_edit", Qt::DirectConnection);
    if (back->stats_text() != viewer->stats_text()) return 116;
    const auto before = back->stats_text();
    if (!write(temp.filePath("map.pcd"), pcd + "\n")) return 117;
    if (restored.open_session(session_path, &error) || back->stats_text() != before) return 118;

    // Undoing all saved deletes is still dirty. Discard on close must retain the saved session.
    write(temp.filePath("map.pcd"), pcd);
    QFile descriptor(session_path); descriptor.open(QIODevice::ReadOnly);
    const QByteArray saved_descriptor = descriptor.readAll(); descriptor.close();
    QMetaObject::invokeMethod(&restored, "undo_edit", Qt::DirectConnection);
    bool discard_prompt = false;
    QTimer discard_dialog;
    QObject::connect(&discard_dialog, &QTimer::timeout, [&]() {
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *message = qobject_cast<QMessageBox*>(widget)) {
          if (message->standardButtons().testFlag(QMessageBox::Discard)) {
            discard_prompt = true;
            message->button(QMessageBox::Discard)->click();
          }
        }
      }
    });
    discard_dialog.start(5);
    if (!restored.close() || !discard_prompt) return 126;
    discard_dialog.stop();
    descriptor.open(QIODevice::ReadOnly);
    if (descriptor.readAll() != saved_descriptor) return 127;
    descriptor.close();

    QDir(temp.path()).mkpath("fake/bin");
    QDir(temp.path()).mkpath("fake/lib/agt_map_refinement_core");
    const QString fake = temp.filePath("fake/bin/ros2");
    if (!write(fake, "#!/usr/bin/python3\nimport sys,time,pathlib,shutil\ntime.sleep(0.2)\na=sys.argv;o=pathlib.Path(a[a.index('--output')+1]);o.mkdir();shutil.copy(pathlib.Path(a[a.index('--map-package')+1])/'map.pcd',o/'map.pcd')\n")) return 119;
    QFile::setPermissions(fake, QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    const QString entry = temp.filePath("fake/lib/agt_map_refinement_core/apply_map_refinement");
    write(entry, "#!/bin/sh\nexit 0\n");
    QFile::setPermissions(entry, QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    qputenv("PATH", (temp.filePath("fake/bin") + ":" + qEnvironmentVariable("PATH")).toUtf8());
    qputenv("AMENT_PREFIX_PATH", (temp.filePath("fake") + ":" + qEnvironmentVariable("AMENT_PREFIX_PATH")).toUtf8());
    bool rejected = false;
    QTimer dialogs;
    QObject::connect(&dialogs, &QTimer::timeout, [&]() {
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *message = qobject_cast<QMessageBox*>(widget)) {
          if (message->text().contains("Inputs changed")) rejected = true;
          message->accept();
        }
      }
    });
    dialogs.start(5);
    for (int changed = 0; changed < 2; ++changed) {
      const QString package = temp.filePath(QString("package_%1").arg(changed));
      QDir().mkpath(package); write(package + "/map.pcd", pcd); write(package + "/manifest.yaml", "{}\n");
      MainWindow running{QString()};
      if (!running.open_mapping_package(package, &error)) return 120;
      auto *view = running.findChild<PointCloudViewer*>();
      QMetaObject::invokeMethod(&running, "set_mode_delete", Qt::DirectConnection);
      view->select_xy_region(-.5, -.5, .5, .5);
      QMetaObject::invokeMethod(&running, "delete_selected", Qt::DirectConnection);
      QMetaObject::invokeMethod(&running, "run_refine", Qt::DirectConnection);
      if (changed) {
        view->select_xy_region(.5, -.5, 1.5, .5);
        QMetaObject::invokeMethod(&running, "delete_selected", Qt::DirectConnection);
      }
      QElapsedTimer wait; wait.start();
      while (wait.elapsed() < 1500) { QApplication::processEvents(); QThread::msleep(2); }
      if (!running.save_session(&error)) return 121;
      WorkflowSession saved;
      if (!saved.load(package + "_studio/studio_session.yaml", &error)) return 122;
      const bool fresh = saved.state(WorkflowSession::Refine) == StageState::Fresh;
      if (fresh == bool(changed)) return 123;
      if (changed && !rejected) return 124;
      auto rules = YAML::LoadFile((package + "_studio/refinement.yaml").toStdString());
      if (rules["operations"].size() != 1 || rules["operations"][0]["type"].as<std::string>() != "remove_indices") return 125;
    }
    std::cout << "3D session, undo/redo, source binding and async input guard passed\n";
    return 0;
  }

  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--check-xy-link") {
    QTemporaryDir temp;
    const auto write = [](const QString &path, const QByteArray &data) {
      QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    QDir(temp.path()).mkpath("navigation");
    const QByteArray pcd = "# .PCD v0.7\nVERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH 4\nHEIGHT 1\nPOINTS 4\nDATA ascii\n0.25 0.25 -5\n0.25 0.25 8\n1.25 0.25 2\n-0.25 0.25 3\n";
    QByteArray pgm("P2\n6 6\n255\n");
    for (int i = 0; i < 36; ++i) pgm += (i == 14 ? "205 " : "0 "); // Unknown at grid (2,3).
    if (!write(temp.filePath("map.pcd"), pcd) || !write(temp.filePath("manifest.yaml"), "{}\n") ||
        !write(temp.filePath("navigation/map.pgm"), pgm) ||
        !write(temp.filePath("navigation/map.yaml"), "image: map.pgm\nresolution: 0.5\norigin: [-1, -1, 0]\n")) return 80;
    agt_map_studio::MainWindow window(QString{});
    QString error;
    if (!window.open_mapping_package(temp.path(), &error)) { std::cerr << error.toStdString(); return 81; }
    auto *occupancy = window.findChild<agt_map_studio::OccupancyViewer*>();
    auto *viewer = window.findChild<agt_map_studio::PointCloudViewer*>();
    if (!occupancy->linked_inspection_enabled() || !occupancy->has_map()) return 82;
    viewer->set_z_window(true, 0., 1.); // Linked inspection must ignore the edit Z gate.
    if (viewer->select_xy_region(0., 0., .5, .5) != 2) return 83;
    if (occupancy->highlighted_cells() != std::vector<std::size_t>{14}) return 84;
    viewer->set_mode(agt_map_studio::InteractionMode::Select);
    QMetaObject::invokeMethod(&window, "show_linked_view", Qt::DirectConnection);
    if (viewer->mode() != agt_map_studio::InteractionMode::Navigate ||
        occupancy->highlighted_cells() != std::vector<std::size_t>{14}) return 101;
    viewer->save_view(temp.filePath("camera_before.yaml"), &error);
    QMouseEvent orbit_press(QEvent::MouseButtonPress, QPointF(100, 100), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent orbit_move(QEvent::MouseMove, QPointF(160, 125), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent orbit_release(QEvent::MouseButtonRelease, QPointF(160, 125), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewer, &orbit_press);
    QApplication::sendEvent(viewer, &orbit_move);
    QApplication::sendEvent(viewer, &orbit_release);
    viewer->save_view(temp.filePath("camera_after.yaml"), &error);
    const auto before = YAML::LoadFile(temp.filePath("camera_before.yaml").toStdString())["camera"]["position"];
    const auto after = YAML::LoadFile(temp.filePath("camera_after.yaml").toStdString())["camera"]["position"];
    double camera_change = 0.;
    for (int i = 0; i < 3; ++i) camera_change += std::abs(before[i].as<double>() - after[i].as<double>());
    if (camera_change < 0.001 || occupancy->highlighted_cells() != std::vector<std::size_t>{14}) return 102;
    if (viewer->select_xy_region(-.5, 0., 1.5, .5) != 4 || occupancy->highlighted_cells().size() != 3) return 85;
    if (viewer->select_xy_region(50., 50., 51., 51.) != 0 || !occupancy->highlighted_cells().empty()) return 86;
    occupancy->resize(600, 600);
    occupancy->fit_map();
    occupancy->set_mode(agt_map_studio::OccupancyInteractionMode::InspectXY);
    const double zoom = occupancy->zoom();
    const QPointF cell((600 - 6 * zoom) / 2 + 2.5 * zoom,
                       (600 - 6 * zoom) / 2 + 3.5 * zoom);
    QMouseEvent press(QEvent::MouseButtonPress, cell, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, cell, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(occupancy, &press);
    QApplication::sendEvent(occupancy, &release);
    if (occupancy->highlighted_cells() != std::vector<std::size_t>{14}) return 87;
    // Check the actual painter output, not only the selection bookkeeping.
    // Render the raster widget alone: offscreen Qt cannot create a GL context
    // for its sibling point-cloud widget when rendering the parent window.
    agt_map_studio::OccupancyViewer raster;
    raster.resize(600, 600);
    raster.set_map(occupancy->map());
    raster.set_highlighted_cells(occupancy->highlighted_cells());
    QImage display(raster.size(), QImage::Format_ARGB32);
    display.fill(Qt::transparent);
    raster.render(&display);
    if (display.pixelColor(cell.toPoint()) != QColor(240, 35, 35)) return 96;
    if (display.pixelColor(cell.toPoint() + QPoint(static_cast<int>(zoom), 0)) != QColor(0, 0, 0)) return 97;
    // A drag includes both endpoint cells; the range covers all four points.
    const QPointF first(cell.x() - zoom, cell.y());
    const QPointF last(cell.x() + 2 * zoom, cell.y());
    QMouseEvent drag_press(QEvent::MouseButtonPress, first, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent drag_release(QEvent::MouseButtonRelease, last, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(occupancy, &drag_press);
    QApplication::sendEvent(occupancy, &drag_release);
    if (occupancy->highlighted_cells().size() != 4) return 98;
    viewer->mark_edit_state_dirty(); // A status refresh must preserve the full rectangle.
    if (occupancy->highlighted_cells().size() != 4) return 99;
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(occupancy, &escape);
    if (!occupancy->highlighted_cells().empty()) return 100;
    // Red is a display overlay: the grid and serialized PGM remain occupied/black.
    if (occupancy->map().at(2, 2) != agt_map_studio::GridMap::kOccupied) return 88;
    if (!window.save_occupancy_map(temp.filePath("saved"), &error) ||
        qGray(QImage(temp.filePath("saved/map.pgm")).pixel(2, 3)) != 0) return 89;
    if (!window.open_occupancy_map(temp.filePath("navigation/map.yaml"), &error) ||
        occupancy->linked_inspection_enabled() || !occupancy->highlighted_cells().empty()) return 90;
    occupancy->inspect_xy_requested(0., 0., .5, .5); // Hand-picked files cannot reactivate linking.
    if (!occupancy->highlighted_cells().empty()) return 91;
    if (!window.open_mapping_package(temp.path(), &error)) return 92;
    if (!window.open_pcd(temp.filePath("map.pcd"), &error) || occupancy->linked_inspection_enabled()) return 93;
    // Also support the published package layout.
    QDir(temp.path()).mkpath("localization");
    if (!QFile::rename(temp.filePath("map.pcd"), temp.filePath("localization/global_map.pcd"))) return 94;
    if (!window.open_mapping_package(temp.path(), &error) || !occupancy->linked_inspection_enabled()) return 95;
    // Erase the existing reverse-linked selection without redrawing a rectangle.
    if (window.findChild<QAction*>(QStringLiteral("erase_current_2d_selection"))) return 109;
    auto *erase_selection = window.findChild<QAction*>(QStringLiteral("erase_rect"));
    if (!erase_selection || viewer->select_xy_region(0., 0., .5, .5) != 2) return 103;
    const auto original_cloud = viewer->cloud().xyz;
    erase_selection->trigger();
    if (!occupancy->highlighted_cells().empty() || viewer->cloud().xyz != original_cloud) return 104;
    if (!window.save_occupancy_map(temp.filePath("erased"), &error)) return 105;
    const QImage erased(temp.filePath("erased/map.pgm"));
    if (qGray(erased.pixel(2, 3)) != 254 || qGray(erased.pixel(3, 3)) != 0) return 106;
    QMetaObject::invokeMethod(&window, "show_2d_view", Qt::DirectConnection);
    QMetaObject::invokeMethod(&window, "undo_edit", Qt::DirectConnection);
    if (!window.save_occupancy_map(temp.filePath("erase_undone"), &error) ||
        qGray(QImage(temp.filePath("erase_undone/map.pgm")).pixel(2, 3)) != 0) return 107;
    QMetaObject::invokeMethod(&window, "redo_edit", Qt::DirectConnection);
    if (!window.save_occupancy_map(temp.filePath("erase_redone"), &error) ||
        qGray(QImage(temp.filePath("erase_redone/map.pgm")).pixel(2, 3)) != 254) return 108;
    // Without a highlighted selection the same action only enters drag-to-erase mode.
    occupancy->set_mode(agt_map_studio::OccupancyInteractionMode::View);
    erase_selection->trigger();
    if (occupancy->mode() != agt_map_studio::OccupancyInteractionMode::Erase ||
        !occupancy->highlighted_cells().empty() ||
        occupancy->map().at(3, 2) != agt_map_studio::GridMap::kOccupied) return 110;
    // Click markers must work for both free and unknown cells, even below cell size.
    auto *marker = window.findChild<QAction*>(QStringLiteral("mark_obstacle"));
    if (!marker) return 111;
    occupancy->set_obstacle_width(0.01);
    marker->trigger();
    occupancy->resize(600, 600);
    occupancy->fit_map();
    const double marker_zoom = occupancy->zoom();
    const QPointF marker_cell((600 - 6 * marker_zoom) / 2 + 2.5 * marker_zoom,
                             (600 - 6 * marker_zoom) / 2 + 3.5 * marker_zoom);
    for (int row = 0; row < 2; ++row) {
      const QPointF position = marker_cell - QPointF(0, row * marker_zoom);
      QMouseEvent press(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QMouseEvent release(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(occupancy, &press);
      QApplication::sendEvent(occupancy, &release);
      const QString output = temp.filePath(QStringLiteral("marker_%1").arg(row));
      if (!window.save_occupancy_map(output, &error) ||
          qGray(QImage(output + "/map.pgm").pixel(2, 3 - row)) != 0) return 112;
    }
    if (viewer->cloud().xyz != original_cloud) return 113;
    QMetaObject::invokeMethod(&window, "undo_edit", Qt::DirectConnection);
    if (!window.save_occupancy_map(temp.filePath("marker_undone"), &error) ||
        qGray(QImage(temp.filePath("marker_undone/map.pgm")).pixel(2, 2)) != 205) return 114;
    QMetaObject::invokeMethod(&window, "redo_edit", Qt::DirectConnection);
    if (!window.save_occupancy_map(temp.filePath("marked"), &error) ||
        qGray(QImage(temp.filePath("marked/map.pgm")).pixel(2, 2)) != 0) return 115;
    if (!window.open_occupancy_map(temp.filePath("marked/map.yaml"), &error) ||
        occupancy->map().at(2, 3) != agt_map_studio::GridMap::kOccupied) return 116;
    std::cout << "Package-only XY linking, all heights, negative origin, reverse highlighting, empty selection, single click and unchanged saved raster passed\n";
    return 0;
  }
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--check-file-dialog") {
    QTemporaryDir temp;
    for (int i = 0; i < 160; ++i) {
      QFile file(temp.filePath(QStringLiteral("map_%1.yaml").arg(i, 3, 10, QChar('0'))));
      if (!file.open(QIODevice::WriteOnly)) return 60;
      file.write("image: map.pgm\n");
    }
    agt_map_studio::StudioFileDialog dialog(nullptr, QStringLiteral("Choose map"), temp.path(),
        QStringLiteral("Maps (*.yaml);;All files (*)"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.resize(650, 400);
    dialog.show();
    auto *view = dialog.findChild<QTreeView*>(QStringLiteral("treeView"));
    if (!view || !dialog.testOption(QFileDialog::DontUseNativeDialog)) return 61;
    QElapsedTimer deadline;
    deadline.start();
    while (deadline.elapsed() < 3000 && view->model()->rowCount(view->rootIndex()) < 160) {
      QApplication::processEvents();
      QThread::msleep(1);
    }
    QApplication::processEvents();
    if (view->model()->rowCount(view->rootIndex()) != 160 || !view->hasFocus()) return 62;
    view->setCurrentIndex(view->model()->index(0, 0, view->rootIndex()));
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(view, &down);
    if (view->currentIndex().row() != 1) return 63;
    QKeyEvent page(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
    QApplication::sendEvent(view, &page);
    if (view->currentIndex().row() <= 1) return 64;
    view->verticalScrollBar()->setValue(0);
    const QPointF local(40, 40);
    QWheelEvent wheel(local, view->viewport()->mapToGlobal(local.toPoint()), QPoint(), QPoint(0,-1200),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view->viewport(), &wheel);
    if (view->verticalScrollBar()->value() <= 0) return 65;
    const QModelIndex last = view->model()->index(159, 0, view->rootIndex());
    view->setCurrentIndex(last);
    if (!dialog.selectedFiles().value(0).endsWith("map_159.yaml")) return 66;
    dialog.reject();
    std::cout << "Qt file dialog focus, Down/PageDown, mouse wheel and file selection passed\n";
    return 0;
  }
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--check-2d") {
    QTemporaryDir temp;
    const auto write = [](const QString &path, const QByteArray &data) {
      QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    QByteArray pgm("P2\n5 5\n255\n");
    for (int i = 0; i < 25; ++i) pgm += "254 ";
    if (!write(temp.filePath("map.pgm"), pgm) ||
        !write(temp.filePath("map.yaml"), "image: map.pgm\nresolution: 1.0\norigin: [0, 0, 0]\n")) return 40;
    agt_map_studio::MainWindow window(QString{});
    QString error;
    if (!window.open_occupancy_map(temp.filePath("map.yaml"), &error)) return 41;
    auto *occupancy = window.findChild<agt_map_studio::OccupancyViewer*>();
    const QVector<QPointF> polygon{{1.,1.},{3.,1.},{3.,3.},{1.,3.}};
    occupancy->fill_polygon_requested(polygon, 100);
    occupancy->forbidden_polygon_requested(polygon);
    // Cancel switching an unsaved map; the existing edit must remain.
    QTimer::singleShot(0, [&]() {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox*>(widget)) box->button(QMessageBox::Cancel)->click();
    });
    if (window.open_occupancy_map(temp.filePath("map.yaml"), &error)) return 42;
    const QString output = temp.filePath("saved");
    if (!window.save_occupancy_map(output, &error)) { std::cerr << error.toStdString(); return 43; }
    auto zones = YAML::LoadFile((output+"/keepout_zones.yaml").toStdString());
    if (zones["zones"].size() != 1 || qGray(QImage(output+"/map.pgm").pixel(2,2)) != 0) return 44;
    if (!window.open_occupancy_map(output+"/map.yaml", &error)) { std::cerr << error.toStdString(); return 45; }
    QMetaObject::invokeMethod(&window, "undo_edit", Qt::DirectConnection); // keepout
    QMetaObject::invokeMethod(&window, "undo_edit", Qt::DirectConnection); // raster
    const QString undone = temp.filePath("undone");
    if (!window.save_occupancy_map(undone, &error) || qGray(QImage(undone+"/map.pgm").pixel(2,2)) != 254) return 46;
    if (!window.open_occupancy_map(undone+"/map.yaml", &error)) return 47;
    QMetaObject::invokeMethod(&window, "redo_edit", Qt::DirectConnection);
    QMetaObject::invokeMethod(&window, "redo_edit", Qt::DirectConnection);
    const QString redone = temp.filePath("redone");
    if (!window.save_occupancy_map(redone, &error) || qGray(QImage(redone+"/map.pgm").pixel(2,2)) != 0) return 48;
    if (YAML::LoadFile((redone+"/keepout_zones.yaml").toStdString())["zones"].size() != 1) return 49;
    // Export must fail safely when its destination is a file.
    if (!write(temp.filePath("blocked"), "keep-this-file") ||
        window.save_occupancy_map(temp.filePath("blocked"), &error)) return 50;
    QFile protected_file(temp.filePath("blocked"));
    protected_file.open(QIODevice::ReadOnly);
    if (protected_file.readAll() != "keep-this-file") return 51;
    if (!window.save_occupancy_map(redone, &error)) return 52;
    agt_map_studio::MainWindow keepout_only(QString{});
    if (!keepout_only.open_occupancy_map(temp.filePath("map.yaml"), &error)) return 53;
    keepout_only.findChild<agt_map_studio::OccupancyViewer*>()->forbidden_polygon_requested(polygon);
    QTimer::singleShot(0, [&]() {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox*>(widget)) box->button(QMessageBox::Cancel)->click();
    });
    if (keepout_only.close()) return 54;
    if (!keepout_only.save_occupancy_map(temp.filePath("keepout_only"), &error)) return 55;
    if (YAML::LoadFile(temp.filePath("keepout_only/keepout_zones.yaml").toStdString())["zones"].size() != 1) return 56;
    std::cout << "Standalone 2D edit/save, canceled switch, keepouts, history undo/redo and failed-save checks passed\n";
    return 0;
  }
  if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--check-alignment") {
    QTemporaryDir temp;
    const QString copy = temp.filePath("studio_session.yaml");
    if (!QFile::copy(QString::fromLocal8Bit(argv[2]), copy)) return 20;
    agt_map_studio::WorkflowPanel panel;
    agt_map_studio::ConverterParameters params;
    params.resolution = .11; params.max_step = 2.; params.max_slope_deg = 18.;
    int emissions = 0;
    QObject::connect(&panel, &agt_map_studio::WorkflowPanel::parameters_changed, [&]() { ++emissions; });
    panel.set_converter(params);
    agt_map_studio::ConverterParameters restored;
    panel.read_converter(&restored);
    if (emissions || restored.resolution != .11 || restored.max_step != 2. || restored.max_slope_deg != 18.) return 21;
    agt_map_studio::MainWindow window(QString{});
    QString error;
    agt_map_studio::WorkflowSession original;
    if (!original.load(copy, &error)) return 22;
    const QString base_pgm = original.record(agt_map_studio::WorkflowSession::Navigation).path+"/map.pgm";
    const QString base_hash = agt_map_studio::WorkflowSession::sha256_file(base_pgm);
    if (!window.open_session(copy, &error)) { std::cerr << error.toStdString(); return 22; }
    auto *workflow = window.findChild<agt_map_studio::WorkflowPanel*>();
    if (!workflow) return 23;
    workflow->read_converter(&restored);
    // Algorithm parameters belong to the plugin, not to generic converter controls.
    bool locked = false;
    for (auto *group : workflow->findChildren<QGroupBox*>())
      if (group->title().contains(QStringLiteral("Converter parameters"))) locked = !group->isEnabled();
    if (!locked) return 25;
    const QVector<QPointF> polygon{{-50., 0.}, {-49., 0.}, {-49., 1.}, {-50., 1.}};
    auto *occupancy = window.findChild<agt_map_studio::OccupancyViewer*>();
    if (!occupancy) return 26;
    occupancy->fill_polygon_requested(polygon, 100);
    if (!QMetaObject::invokeMethod(&window, "run_patch", Qt::DirectConnection)) return 27;
    if (!window.statusBar()->currentMessage().startsWith(QStringLiteral("2D edits saved: "))) return 28;
    agt_map_studio::WorkflowSession session;
    if (!session.load(copy, &error) || session.state(agt_map_studio::WorkflowSession::Patch) != agt_map_studio::StageState::Fresh) return 29;
    const QString output = session.record(agt_map_studio::WorkflowSession::Patch).path;
    if (!output.startsWith(temp.path()) || !QFile::exists(output+"/map_refinement.yaml")) return 30;
    const auto map = YAML::LoadFile((output+"/map.yaml").toStdString());
    const double resolution = map["resolution"].as<double>();
    const int x = static_cast<int>(std::floor((-49.5-map["origin"][0].as<double>())/resolution));
    const int y = static_cast<int>(std::floor((.5-map["origin"][1].as<double>())/resolution));
    const QImage raster(output+"/map.pgm");
    if (raster.isNull() || qGray(raster.pixel(x, raster.height()-1-y)) != 0 ||
        agt_map_studio::WorkflowSession::sha256_file(base_pgm) != base_hash) return 31;
    agt_map_studio::MainWindow reopened(QString{});
    if (!reopened.open_session(copy, &error)) return 32;
    const QString second = temp.filePath("session_roundtrip");
    if (!reopened.save_occupancy_map(second, &error) ||
        qGray(QImage(second+"/map.pgm").pixel(x, raster.height()-1-y)) != 0) return 33;
    std::cout << "Parameter restore, fixed controls and edited-map save passed in temporary session\n";
    return 0;
  }
  const QString profile_path = qEnvironmentVariable("AGT_MAP_PROFILE");
  if (profile_path.isEmpty()) { std::cout << "Set AGT_MAP_PROFILE to run the dataset integration test\n"; return 77; }
  QFile profile(profile_path);
  if (!profile.open(QIODevice::ReadOnly)) return 2;
  const auto preset=QJsonDocument::fromJson(profile.readAll()).object();
  agt_map_studio::MainWindow window(QString{});
  QString error;
  if (!window.open_mapping_package(QDir(QFileInfo(profile_path).absolutePath()).absoluteFilePath(preset.value("package").toString()),&error)) {
    std::cerr<<error.toStdString()<<'\n'; return 3;
  }
  agt_map_studio::PublishTarget original_target;
  window.findChild<agt_map_studio::WorkflowPanel*>()->read_publish_target(&original_target);
  if (qEnvironmentVariableIsSet("AGT_TEST_3D_GUARD")) {
    auto *viewer = window.findChild<agt_map_studio::PointCloudViewer*>();
    viewer->select_height_band(-10000,10000);
    QMetaObject::invokeMethod(&window,"set_mode_delete",Qt::DirectConnection);
    QMetaObject::invokeMethod(&window,"delete_selected",Qt::DirectConnection);
    QString message;
    QTimer dismiss;
    QObject::connect(&dismiss,&QTimer::timeout,[&]() {
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *box = qobject_cast<QMessageBox*>(widget)) { message=box->text();box->accept(); }
      }
    });
    dismiss.start(20);
    QMetaObject::invokeMethod(&window,"run_navigation",Qt::DirectConnection);
    if (!message.contains("do not currently accept manual 3D deletion rules")) return 71;
    std::cout << "Manual 3D deletions explicitly rejected; no silent converter fallback\n";
    return 0;
  }
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout,&QTimer::timeout,[&]() {application.exit(4);});
  const int requested_timeout = qEnvironmentVariableIntValue("AGT_TEST_TIMEOUT_MS");
  timeout.start(requested_timeout > 0 ? requested_timeout : 60000);
  if (qEnvironmentVariableIsSet("AGT_TEST_SHOW_WINDOW")) window.show();
  QTimer poll;
  QObject::connect(&poll,&QTimer::timeout,[&]() {
    const QString message=window.statusBar()->currentMessage();
    if (!message.startsWith(QStringLiteral("Pipeline completed: "))) return;
    const QString output=message.mid(QStringLiteral("Pipeline completed: ").size());
    QFile result(QDir(output).filePath("result.json"));
    if (!result.open(QIODevice::ReadOnly)) {application.exit(5);return;}
    const auto manifest=QJsonDocument::fromJson(result.readAll()).object();
    QString point_cloud, occupancy;
    for (const auto &value : manifest.value("outputs").toArray()) {
      const auto artifact=value.toObject();
      if (artifact.value("type").toString()=="point_cloud") point_cloud=artifact.value("path").toString();
      if (artifact.value("type").toString()=="occupancy_map") occupancy=artifact.value("path").toString();
    }
    auto *viewer=window.findChild<agt_map_studio::PointCloudViewer*>();
    const QString expected_override=qEnvironmentVariable("AGT_TEST_POINT_COUNT");
    const unsigned expected=expected_override.isEmpty()?737997:expected_override.toUInt();
    bool passed=manifest.value("status").toString()=="complete" && viewer &&
      viewer->cloud().point_count()==expected && QFile::exists(occupancy) &&
      QFile::exists(QDir(output).filePath("studio_session/studio_session.yaml"));
    agt_map_studio::WorkflowSession saved_session;
    passed=passed && saved_session.load(QDir(output).filePath("studio_session/studio_session.yaml"),&error) &&
        saved_session.publish_target().map_id==original_target.map_id &&
        saved_session.state(agt_map_studio::WorkflowSession::Navigation)==agt_map_studio::StageState::Fresh;
    if (expected_override.isEmpty()) {
      QFile report(QDir(QFileInfo(point_cloud).absolutePath()).filePath("report.json"));
      if (!report.open(QIODevice::ReadOnly)) {application.exit(5);return;}
      const auto data=QJsonDocument::fromJson(report.readAll()).object();
      passed=passed && data.value("output_points").toInt()==737997 &&
        data.value("grid_counts").toObject().value("254").toInt()==
          (manifest.value("algorithm_id").toString()=="agt.lizhi_map_pipeline" ? 219553 : 149032);
    }
    std::cout<<"Navigation action output: "<<output.toStdString()<<'\n';
    application.exit(passed?0:6);
  });
  poll.start(100);
  QTimer::singleShot(0,[&]() {
    if (!QMetaObject::invokeMethod(&window,"run_navigation",Qt::DirectConnection)) application.exit(7);
  });
  return application.exec();
}
