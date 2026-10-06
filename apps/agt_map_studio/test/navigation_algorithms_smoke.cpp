// End-to-end test of the existing navigation action; no extra UI controls.
#include "ui/MainWindow.hpp"
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
  timeout.start(60000);
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
