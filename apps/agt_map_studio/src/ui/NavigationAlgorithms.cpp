#include "ui/MainWindow.hpp"
#include "ui/WorkflowPanel.hpp"
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStatusBar>

namespace agt_map_studio {
bool MainWindow::run_mapping_algorithms() {
  if (session_.empty()) return false;
  const QString package = session_.source_package_dir().isEmpty()
      ? QFileInfo(session_.source_pcd()).absolutePath() : session_.source_package_dir();
  if (tool_runner_.is_running()) return true;
  if (has_unsaved_2d_edits()) {
    fail_queue(QStringLiteral("Save your 2D edits before processing a new map."));
    return true;
  }
  try { ament_index_cpp::get_package_share_directory("agt_map_runner"); }
  catch (const std::exception &error) { fail_queue(QString::fromUtf8(error.what())); return true; }
  QStringList inputs{"--map-package", package, "--point-cloud", session_.source_pcd()};
  const QString profile = qEnvironmentVariable("AGT_MAP_PROFILE");
  if (!profile.isEmpty()) inputs << "--profile" << profile;
  ToolInvocation listing;
  listing.program = "ros2";
  listing.arguments = QStringList{"run", "agt_map_runner", "map_runner", "--list-json"} + inputs;
  const ToolResult registry = ExternalToolRunner::run_blocking(listing, 10000);
  if (!registry.ok) { fail_queue(registry.error_summary + "\n" + registry.output); return true; }
  QJsonParseError parse_error;
  const QJsonObject catalog = QJsonDocument::fromJson(registry.output.toUtf8(), &parse_error).object();
  if (parse_error.error != QJsonParseError::NoError) {
    fail_queue(QStringLiteral("Invalid algorithm registry response: %1").arg(parse_error.errorString())); return true;
  }
  QStringList ids, labels;
  const QString requested = qEnvironmentVariable("AGT_MAP_ALGORITHM");
  for (const auto &value : catalog.value("algorithms").toArray()) {
    const auto entry = value.toObject();
    const QString id = entry.value("id").toString();
    if (!entry.value("available").toBool()) {
      if (id == requested) { fail_queue(QStringLiteral("Selected algorithm has missing input data: %1").arg(id)); return true; }
      continue;
    }
    ids << id; labels << QStringLiteral("%1 [%2]").arg(entry.value("name").toString(id), id);
  }
  if (ids.isEmpty()) {
    if (!session_.algorithm_available() && requested.isEmpty()) return false;
    fail_queue(QStringLiteral("No registered algorithm accepts this map's data. Check the map assets and algorithm registration.")); return true;
  }
  if (session_.has_3d_edits()) {
    fail_queue(QStringLiteral("Registered algorithms do not currently accept manual 3D deletion rules. "
        "The selected pipeline will not be replaced by the generic converter. "
        "Undo the 3D deletions before running this pipeline; keep a saved copy of your edits."));
    return true;
  }
  QString id = requested;
  if (!id.isEmpty() && !ids.contains(id)) { fail_queue(QStringLiteral("Algorithm is not registered: %1").arg(id)); return true; }
  if (id.isEmpty() && ids.size() == 1) id = ids.first();
  if (id.isEmpty()) {
    bool accepted = false;
    const QString selected = QInputDialog::getItem(this, QStringLiteral("Select algorithm"),
        QStringLiteral("Compatible registered algorithms"), labels, 0, false, &accepted);
    if (!accepted) return true;
    id = ids.at(labels.indexOf(selected));
  }
  const QString work = qEnvironmentVariableIsSet("AGT_MAP_OUTPUT_ROOT")
      ? qEnvironmentVariable("AGT_MAP_OUTPUT_ROOT")
      : (session_.work_dir().isEmpty() ? package + "_studio" : session_.work_dir());
  const QString output = QDir(work).filePath(QStringLiteral("processing_runs/run_%1")
      .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")));
  if (!QDir().mkpath(work)) { fail_queue(QStringLiteral("Cannot create work directory: %1").arg(work)); return true; }
  session_.set_work_dir(work);
  const QString source_fingerprint = session_.source_pcd_sha256();
  const QString edits_fingerprint = session_.refinement_fingerprint();
  const QString raster_fingerprint = QString::fromStdString(refinement_model_.active_fingerprint());
  const PublishTarget publish_target = session_.publish_target();
  ToolInvocation invocation;
  invocation.label = id;
  invocation.program = "ros2";
  invocation.arguments = QStringList{"run", "agt_map_runner", "map_runner", "--algorithm", id,
                                    "--output", output} + inputs;
  workflow_panel_->append_log(QStringLiteral("\nRegistered algorithm: %1\nOutput: %2\n").arg(id, output));
  run_tool(invocation,[this,output,source_fingerprint,edits_fingerprint,raster_fingerprint,publish_target](const ToolResult &result) {
    if (!result.ok) return fail_queue(result.error_summary);
    if (session_.source_pcd_sha256()!=source_fingerprint || session_.refinement_fingerprint()!=edits_fingerprint ||
        has_unsaved_2d_edits() || QString::fromStdString(refinement_model_.active_fingerprint())!=raster_fingerprint) {
      fail_queue(QStringLiteral("Inputs changed during processing. Current view preserved. Result: %1").arg(output)); return;
    }
    QFile file(QDir(output).filePath("result.json"));
    if (!file.open(QIODevice::ReadOnly)) return fail_queue(QStringLiteral("Algorithm result.json is missing."));
    QJsonParseError parse_error;
    const auto report=QJsonDocument::fromJson(file.readAll(), &parse_error).object();
    if (parse_error.error!=QJsonParseError::NoError || report.value("status").toString()!="complete")
      return fail_queue(QStringLiteral("Algorithm did not return a complete result."));
    QString map_package, pcd, occupancy;
    for (const auto &value : report.value("outputs").toArray()) {
      const auto artifact=value.toObject();
      const QString type=artifact.value("type").toString(), path=artifact.value("path").toString();
      if (type=="map_package") map_package=path;
      else if (type=="point_cloud") pcd=path;
      else if (type=="occupancy_map") occupancy=path;
    }
    QString error;
    if (!map_package.isEmpty()) { if (!open_mapping_package(map_package,&error)) return fail_queue(error); }
    else if (!pcd.isEmpty()) { if (!open_pcd(pcd,&error)) return fail_queue(error); }
    if (!occupancy.isEmpty() && !open_occupancy_map(occupancy,&error)) return fail_queue(error);
    session_.publish_target() = publish_target;
    workflow_panel_->set_publish_target(publish_target);
    if (map_package.isEmpty() && pcd.isEmpty() && occupancy.isEmpty())
      return fail_queue(QStringLiteral("The algorithm returned no supported map artifacts."));
    session_.set_work_dir(QDir(output).filePath("studio_session"));
    QDir().mkpath(session_.work_dir()); save_session_quietly();
    workflow_panel_->append_log(result.output);
    refresh_workflow();
    statusBar()->showMessage(QStringLiteral("Pipeline completed: %1").arg(output),15000);
    continue_queue();
  });
  return true;
}
}  // namespace agt_map_studio
