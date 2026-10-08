#include "selection/SelectionManager.h"

#include <QTemporaryDir>
#include <QFile>
#include <yaml-cpp/yaml.h>

#include <gtest/gtest.h>

#include <pcl/io/pcd_io.h>

#include <cstring>
#include <fstream>

namespace agt_map_studio {

TEST(SelectionManagerTest, DeleteUndoRedoPreservesPointStates) {
  SelectionManager manager;
  manager.reset(4);
  AxisAlignedBoundingBox box;
  box.min = Eigen::Vector3f(0.0F, 1.0F, 2.0F);
  box.max = Eigen::Vector3f(3.0F, 4.0F, 5.0F);
  box.valid = true;
  manager.select_points({1, 3}, box);
  EXPECT_EQ(manager.selected_count(), 2U);
  ASSERT_TRUE(manager.delete_selected());
  EXPECT_EQ(manager.deleted_count(), 2U);
  EXPECT_EQ(manager.visible_count(), 2U);
  ASSERT_TRUE(manager.undo());
  EXPECT_EQ(manager.deleted_count(), 0U);
  EXPECT_EQ(manager.visible_count(), 4U);
  ASSERT_TRUE(manager.redo());
  EXPECT_EQ(manager.deleted_count(), 2U);
  EXPECT_EQ(manager.history().size(), 1U);
  EXPECT_FALSE(manager.history().front().undone);
}

TEST(SelectionManagerTest, EmptyDeleteDoesNothing) {
  SelectionManager manager;
  manager.reset(3);
  EXPECT_FALSE(manager.delete_selected());
  EXPECT_FALSE(manager.undo());
  EXPECT_FALSE(manager.redo());
  EXPECT_EQ(manager.visible_count(), 3U);
}

TEST(SelectionManagerTest, ExportKeepsOriginalPclFields) {
  pcl::PCLPointCloud2 source;
  source.width = 4;
  source.height = 1;
  source.point_step = 16;
  source.row_step = source.width * source.point_step;
  source.fields = {
      pcl::PCLPointField{"x", 0, pcl::PCLPointField::FLOAT32, 1},
      pcl::PCLPointField{"y", 4, pcl::PCLPointField::FLOAT32, 1},
      pcl::PCLPointField{"z", 8, pcl::PCLPointField::FLOAT32, 1},
      pcl::PCLPointField{"intensity", 12, pcl::PCLPointField::FLOAT32, 1},
  };
  source.data.resize(source.row_step);
  for (std::size_t i = 0; i < source.width; ++i) {
    const float values[] = {static_cast<float>(i), 1.0F, 2.0F, 10.0F + i};
    std::memcpy(source.data.data() + i * source.point_step, values,
                sizeof(values));
  }

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString input = temporary.filePath("input.pcd");
  ASSERT_EQ(pcl::io::savePCDFile(input.toStdString(), source,
                                 Eigen::Vector4f::Zero(),
                                 Eigen::Quaternionf::Identity(), true), 0);
  LoadedPointCloud loaded;
  std::string error;
  ASSERT_TRUE(PCDLoader::load(input.toStdString(), &loaded, &error)) << error;

  SelectionManager manager;
  manager.reset(loaded.point_count());
  manager.select_points({1}, AxisAlignedBoundingBox());
  ASSERT_TRUE(manager.delete_selected());
  QString export_error;
  ASSERT_TRUE(manager.export_clean_map(loaded, temporary.filePath("clean_map"),
                                       input, &export_error))
      << export_error.toStdString();

  LoadedPointCloud clean;
  ASSERT_TRUE(PCDLoader::load(temporary.filePath("clean_map/map.pcd").toStdString(),
                              &clean, &error))
      << error;
  EXPECT_EQ(clean.point_count(), 3U);
  ASSERT_TRUE(clean.source);
  ASSERT_EQ(clean.source->fields.size(), 4U);
  EXPECT_EQ(clean.source->fields[3].name, "intensity");
}


TEST(SelectionManagerTest, SessionRestoresCommandsAndRejectsWrongSourceTransactionally) {
  QTemporaryDir temporary;
  SelectionManager manager; manager.reset(4);
  SelectionGeometry sphere; sphere.rule_type = "remove_sphere";
  sphere.center = Eigen::Vector3d(10, 20, 30); sphere.radius = 2;
  manager.select_points({0, 0}, sphere);
  ASSERT_EQ(manager.selected_count(), 1U);
  ASSERT_TRUE(manager.delete_selected());
  manager.select_points({2}, sphere); ASSERT_TRUE(manager.delete_selected());
  ASSERT_TRUE(manager.undo());
  EXPECT_EQ(manager.selection_geometry().rule_type, "remove_sphere");
  QString error;
  const auto path = temporary.filePath("state.yaml");
  ASSERT_TRUE(manager.save_state(path, "source-digest", &error)) << error.toStdString();
  SelectionManager restored;
  ASSERT_TRUE(restored.load_state(path, "source-digest", 4, &error)) << error.toStdString();
  EXPECT_EQ(restored.statuses(), manager.statuses());
  EXPECT_EQ(restored.active_fingerprint(), manager.active_fingerprint());
  EXPECT_EQ(restored.selection_geometry().center, sphere.center);
  ASSERT_TRUE(restored.redo()); EXPECT_EQ(restored.deleted_count(), 2U);
  ASSERT_TRUE(restored.undo()); ASSERT_TRUE(restored.undo()); EXPECT_EQ(restored.deleted_count(), 0U);
  ASSERT_TRUE(restored.delete_selected());
  EXPECT_EQ(restored.history().back().geometry.center, sphere.center);
  const auto fingerprint = restored.active_fingerprint();
  EXPECT_FALSE(restored.load_state(path, "wrong-source", 4, &error));
  EXPECT_EQ(restored.active_fingerprint(), fingerprint);
  auto corrupt = YAML::LoadFile(path.toStdString());
  corrupt["history"][0]["indices"] = std::vector<std::size_t>{999};
  std::ofstream stream(path.toStdString()); stream << corrupt; stream.close();
  EXPECT_FALSE(restored.load_state(path, "source-digest", 4, &error));
  EXPECT_EQ(restored.active_fingerprint(), fingerprint);
}

TEST(SelectionManagerTest, FingerprintIncludesExactPointsAndGeometry) {
  SelectionManager a, b, c; a.reset(2); b.reset(2); c.reset(2);
  SelectionGeometry sphere; sphere.rule_type = "remove_sphere";
  sphere.radius = 1.; sphere.center = Eigen::Vector3d(10, 0, 0);
  a.select_points({0}, sphere); ASSERT_TRUE(a.delete_selected());
  b.select_points({1}, sphere); ASSERT_TRUE(b.delete_selected());
  sphere.center.x() = 20.;
  c.select_points({0}, sphere); ASSERT_TRUE(c.delete_selected());
  EXPECT_NE(a.active_fingerprint(), b.active_fingerprint());
  EXPECT_NE(a.active_fingerprint(), c.active_fingerprint());
  ASSERT_TRUE(c.undo()); EXPECT_TRUE(c.active_fingerprint().isEmpty());
}

TEST(SelectionManagerTest, ExactRulesKeepSourceRowIdentityAcrossNonfinitePointsAndInverseSelection) {
  QTemporaryDir temporary;
  const QString input = temporary.filePath("input.pcd");
  QFile file(input); ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH 4\nHEIGHT 1\nPOINTS 4\nDATA ascii\n0 0 0\nnan 0 0\n1 0 0\n2 0 0\n"); file.close();
  LoadedPointCloud cloud; std::string detail;
  ASSERT_TRUE(PCDLoader::load(input.toStdString(), &cloud, &detail)) << detail;
  ASSERT_EQ(cloud.point_count(), 3U);
  SelectionManager manager; manager.reset(cloud.point_count());
  manager.select_points({1}, AxisAlignedBoundingBox()); manager.invert_selection();
  ASSERT_TRUE(manager.delete_selected());
  QString error; const QString path = temporary.filePath("rules.yaml");
  ASSERT_TRUE(manager.write_refinement_rules(path, input, &error, &cloud)) << error.toStdString();
  auto rule = YAML::LoadFile(path.toStdString())["operations"][0];
  EXPECT_EQ(rule["type"].as<std::string>(), "remove_indices");
  EXPECT_EQ(rule["indices"].as<std::vector<std::size_t>>(), (std::vector<std::size_t>{0, 3}));
  EXPECT_EQ(rule["source_point_count"].as<std::size_t>(), 4U);
  EXPECT_EQ(rule["source_sha256"].as<std::string>().size(), 64U);
  manager.bind_source_sha256(QString::fromStdString(rule["source_sha256"].as<std::string>()));
  ASSERT_TRUE(file.open(QIODevice::Append)); file.write("\n"); file.close();
  EXPECT_FALSE(manager.write_refinement_rules(path, input, &error, &cloud));
  EXPECT_EQ(YAML::LoadFile(path.toStdString())["operations"][0]["indices"].as<std::vector<std::size_t>>(),
            (std::vector<std::size_t>{0, 3}));
}

}  // namespace agt_map_studio
