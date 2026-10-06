#include "workflow/WorkflowSession.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

namespace agt_map_studio {
namespace {

QString write_file(const QTemporaryDir &dir, const QString &name, const QByteArray &data) {
  const QString path = dir.filePath(name);
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(data);
  file.close();
  return path;
}

}  // namespace

TEST(WorkflowSessionTest, StagesStartMissingAndBecomeFresh) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString pcd = write_file(dir, "map.pcd", "not really a pcd");
  WorkflowSession session;
  session.set_work_dir(dir.filePath("work"));
  session.reset(pcd, QString());
  EXPECT_FALSE(session.source_pcd_sha256().isEmpty());
  EXPECT_EQ(session.state(WorkflowSession::Navigation), StageState::Missing);
  EXPECT_EQ(session.effective_pcd(), pcd);

  const QString nav_dir = dir.filePath("nav");
  QDir().mkpath(nav_dir);
  session.mark_done(WorkflowSession::Navigation, nav_dir, "abc");
  EXPECT_EQ(session.state(WorkflowSession::Navigation), StageState::Fresh);
}

TEST(WorkflowSessionTest, ThreeDEditsInvalidateDownstreamStages) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString pcd = write_file(dir, "map.pcd", "source");
  WorkflowSession session;
  session.set_work_dir(dir.filePath("work"));
  session.reset(pcd, QString());
  const QString reloc = dir.filePath("reloc");
  const QString nav = dir.filePath("nav");
  QDir().mkpath(reloc);
  QDir().mkpath(nav);
  session.mark_done(WorkflowSession::Relocalization, reloc, session.source_pcd_sha256());
  session.mark_done(WorkflowSession::Navigation, nav, "pgm");
  EXPECT_EQ(session.state(WorkflowSession::Relocalization), StageState::Fresh);

  // The user deletes points: refine is now required and downstream is stale.
  session.set_refinement_fingerprint("edit-1");
  EXPECT_EQ(session.state(WorkflowSession::Refine), StageState::Missing);
  EXPECT_EQ(session.effective_pcd(), pcd) << "unapplied edits never change the effective PCD";

  const QString refined = dir.filePath("refined");
  QDir().mkpath(refined);
  write_file(dir, "refined/map.pcd", "refined-cloud");
  session.mark_done(WorkflowSession::Refine, refined, "refined-sha");
  EXPECT_EQ(session.state(WorkflowSession::Refine), StageState::Fresh);
  EXPECT_EQ(session.effective_pcd_sha256(), "refined-sha");
  EXPECT_EQ(session.state(WorkflowSession::Relocalization), StageState::Stale);
  EXPECT_EQ(session.state(WorkflowSession::Navigation), StageState::Stale);
  EXPECT_FALSE(session.blocking_reasons_for_publish().isEmpty());

  // Editing again after refine makes refine itself stale.
  session.set_refinement_fingerprint("edit-2");
  EXPECT_EQ(session.state(WorkflowSession::Refine), StageState::Stale);
  EXPECT_EQ(session.effective_pcd(), pcd);
}

TEST(WorkflowSessionTest, TwoDEditsOnlyInvalidatePatchAndPublish) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString pcd = write_file(dir, "map.pcd", "source");
  WorkflowSession session;
  session.set_work_dir(dir.filePath("work"));
  session.reset(pcd, QString());
  const QString nav = dir.filePath("nav");
  QDir().mkpath(nav);
  session.mark_done(WorkflowSession::Relocalization, nav, session.source_pcd_sha256());
  session.mark_done(WorkflowSession::Navigation, nav, "pgm");
  session.set_patch_fingerprint("p1");
  EXPECT_EQ(session.state(WorkflowSession::Navigation), StageState::Fresh);
  EXPECT_EQ(session.state(WorkflowSession::Patch), StageState::Missing);
  EXPECT_TRUE(session.effective_navigation_dir().isEmpty());
  const QString patched = dir.filePath("patched");
  QDir().mkpath(patched);
  session.mark_done(WorkflowSession::Patch, patched, "pgm2");
  EXPECT_EQ(session.effective_navigation_dir(), patched);
  session.publish_target().map_id = "m";
  session.publish_target().map_version = "v1";
  EXPECT_TRUE(session.blocking_reasons_for_publish().isEmpty());
}

TEST(WorkflowSessionTest, SaveAndLoadRoundTrip) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString pcd = write_file(dir, "map.pcd", "source");
  WorkflowSession session;
  session.set_work_dir(dir.filePath("work"));
  session.reset(pcd, QString());
  session.converter().resolution = 0.05;
  session.publish_target().map_id = "demo";
  session.publish_target().map_version = "v9";
  session.set_editor_map_path(dir.filePath("edited/map.yaml"));
  const QString nav = dir.filePath("nav");
  QDir().mkpath(nav);
  session.mark_done(WorkflowSession::Navigation, nav, "pgm", "note");
  QString error;
  ASSERT_TRUE(session.save(&error)) << error.toStdString();

  WorkflowSession restored;
  ASSERT_TRUE(restored.load(session.session_file(), &error)) << error.toStdString();
  EXPECT_EQ(restored.source_pcd(), pcd);
  EXPECT_DOUBLE_EQ(restored.converter().resolution, 0.05);
  EXPECT_EQ(restored.publish_target().map_id, "demo");
  EXPECT_EQ(restored.editor_map_path(), dir.filePath("edited/map.yaml"));
  EXPECT_EQ(restored.record(WorkflowSession::Navigation).path, nav);
  EXPECT_EQ(restored.state(WorkflowSession::Navigation), StageState::Fresh);
}

TEST(WorkflowSessionTest, FixedNavigationIgnoresUnusedParametersButDetectsChangedRaster) {
  QTemporaryDir dir;
  const QString pcd = write_file(dir, "map.pcd", "source");
  const QString nav = dir.filePath("navigation");
  QDir().mkpath(nav);
  const QString pgm = write_file(dir, "navigation/map.pgm", "original-raster");
  const QJsonObject report{{"status", "complete"}, {"algorithms", QJsonObject{{"octomap", 1}}},
      {"implementation_sha256", QJsonObject{{"run_pipeline.py", "version"}}},
      {"outputs", QJsonObject{{"map_yaml", nav + "/map.yaml"}}}};
  write_file(dir, "report.json", QJsonDocument(report).toJson());
  WorkflowSession session;
  session.reset(pcd, QString());
  session.mark_done(WorkflowSession::Navigation, nav, WorkflowSession::sha256_file(pgm));
  session.converter().resolution = .11;
  session.converter().max_step = 2.;
  session.set_patch_fingerprint("edited-polygon");
  EXPECT_EQ(session.state(WorkflowSession::Navigation), StageState::Fresh);
  session.mark_done(WorkflowSession::Patch, nav, "patched");
  EXPECT_EQ(session.state(WorkflowSession::Patch), StageState::Fresh);
  write_file(dir, "navigation/map.pgm", "externally-changed-raster");
  EXPECT_EQ(session.state(WorkflowSession::Navigation), StageState::Stale);
}

TEST(WorkflowSessionTest, GenericNavigationStillUsesConverterParameters) {
  QTemporaryDir dir;
  WorkflowSession session;
  session.reset(write_file(dir, "map.pcd", "source"), QString());
  session.mark_done(WorkflowSession::Navigation, dir.path(), "pgm");
  session.converter().resolution = .11;
  EXPECT_EQ(session.state(WorkflowSession::Navigation), StageState::Stale);
}

}  // namespace agt_map_studio
