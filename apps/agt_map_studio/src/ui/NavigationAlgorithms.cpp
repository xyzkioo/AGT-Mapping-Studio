#include "ui/MainWindow.hpp"
#include "ui/WorkflowPanel.hpp"
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStatusBar>
#include <yaml-cpp/yaml.h>

namespace agt_map_studio {
bool MainWindow::run_mapping_algorithms() {
  // The original navigation action now runs the fixed chain on a mapping source.
  // Manual refinement continues to use its established converter path.
  if (session_.empty() || session_.has_3d_edits()) return false;
  QString package = session_.source_package_dir();
  if (package.isEmpty()) {
    const QString adjacent = QFileInfo(session_.source_pcd()).absolutePath();
    if (QFileInfo::exists(QDir(adjacent).filePath("poses_timed.txt"))) package = adjacent;
  }
  if (package.isEmpty() || !QFileInfo::exists(QDir(package).filePath("poses_timed.txt")) ||
      !QFileInfo(QDir(package).filePath("patches")).isDir()) return false;
  if (tool_runner_.is_running()) return true;
  if (has_unsaved_2d_edits()) {
    fail_queue(QStringLiteral("Save your 2D edits first. The pipeline will generate and load a new map."));
    return true;
  }
  QString share;
  try {
    share = QString::fromStdString(ament_index_cpp::get_package_share_directory("agt_map_studio"));
    const QString manifest = QDir(package).filePath("manifest.yaml");
    if (QFileInfo::exists(manifest)) {
      const YAML::Node metadata = YAML::LoadFile(manifest.toStdString());
      if (metadata["package_kind"].as<std::string>("") == "studio_algorithm_mapping_source")
        package = QString::fromStdString(metadata["parent_package"].as<std::string>());
    }
  } catch (const std::exception &error) {
    fail_queue(QString::fromUtf8(error.what()));
    return true;
  }
  const QString profile = QDir(share).filePath("algorithms/current_map_profile.json");
  QFile file(profile);
  if (!file.open(QIODevice::ReadOnly)) {
    fail_queue(QStringLiteral("Pipeline configuration is missing. Rebuild agt_map_studio."));
    return true;
  }
  const QJsonObject preset = QJsonDocument::fromJson(file.readAll()).object();
  const QString baseline = preset.value("assets").toObject().value("baseline").toString();
  const QString output = QDir::cleanPath(QDir(QFileInfo(baseline).absolutePath()).absoluteFilePath(
    QStringLiteral("../studio_pipeline_runs/run_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"))));
  const QString work = QDir::cleanPath(QDir(output).absoluteFilePath("../studio_work"));
  // A mapping source may be read-only; all job logs live in the output area.
  if (!QDir().mkpath(work)) {
    fail_queue(QStringLiteral("Cannot create the pipeline work directory: %1").arg(work));
    return true;
  }
  session_.set_work_dir(work);
  const QString source_fingerprint = session_.source_pcd_sha256();
  const QString edits_fingerprint = session_.refinement_fingerprint();
  const QString raster_fingerprint = QString::fromStdString(refinement_model_.active_fingerprint());
  ToolInvocation invocation;
  invocation.label = QStringLiteral("CenterPoint + vehicle body filter + isolated point filter + OctoMap");
  invocation.program = QStringLiteral("/usr/bin/python3");
  invocation.arguments = QStringList{"-u",QDir(share).filePath("algorithms/run_pipeline.py"),
    "--profile",profile,"--package",package,"--output",output,
    "--octomap-builder",QDir(share).filePath("../../lib/agt_map_studio/studio_octomap_builder")};
  workflow_panel_->append_log(QStringLiteral("\nFixed pipeline; CenterPoint reuses cached detections and recomputes tracking.\nOutput: %1\n").arg(output));
  run_tool(invocation,[this,output,source_fingerprint,edits_fingerprint,raster_fingerprint](const ToolResult &result) {
    if (!result.ok) return fail_queue(result.error_summary);
    if (session_.source_pcd_sha256()!=source_fingerprint ||
        session_.refinement_fingerprint()!=edits_fingerprint || has_unsaved_2d_edits() ||
        QString::fromStdString(refinement_model_.active_fingerprint()) != raster_fingerprint) {
      fail_queue(QStringLiteral("Processing completed, but the source or edits changed during the run. Current view preserved. New output: %1").arg(output));
      return;
    }
    QString error;
    if (!open_mapping_package(output,&error) ||
        !open_occupancy_map(QDir(output).filePath("navigation/map.yaml"),&error)) return fail_queue(error);
    session_.set_work_dir(QDir(output).filePath("studio_session"));
    QDir().mkpath(session_.work_dir());
    save_session_quietly();
    workflow_panel_->append_log(result.output);
    workflow_panel_->append_log(QStringLiteral("\nLoaded the new point cloud and 2D map: %1\n").arg(output));
    refresh_workflow();
    statusBar()->showMessage(QStringLiteral("Pipeline completed: %1").arg(output),15000);
    continue_queue();
  });
  return true;
}
}  // namespace agt_map_studio
