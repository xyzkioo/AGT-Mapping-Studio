#include "occupancy/RefinementModel.hpp"
#include "occupancy/MapYamlLoader.hpp"
#include "occupancy/commands/DrawObstacleCommand.hpp"
#include "occupancy/commands/EraseRectangleCommand.hpp"
#include "occupancy/commands/ForbiddenPolygonCommand.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <algorithm>
#include <yaml-cpp/yaml.h>

namespace agt_map_studio {
namespace {

GridMap make_base_map() {
  GridMap map;
  std::string error;
  EXPECT_TRUE(map.set_geometry(5, 5, 1.0F, 0.0, 0.0, &error)) << error;
  std::fill(map.cells().begin(), map.cells().end(), GridMap::kFree);
  map.cells()[1U * map.width() + 1U] = GridMap::kOccupied;
  map.cells()[1U * map.width() + 2U] = GridMap::kOccupied;
  return map;
}

TEST(RefinementModelTest, EraseUndoRedoUsesSparseCellChanges) {
  RefinementModel model;
  model.set_base_map(make_base_map(), MapYamlMetadata());
  auto command = EraseRectangleCommand::create(model, {0.5, 0.5}, {2.5, 1.5});
  ASSERT_TRUE(command);
  ASSERT_EQ(command->operation().changes.size(), 2U);
  std::string error;
  ASSERT_TRUE(model.execute(std::move(command), &error)) << error;
  EXPECT_EQ(model.effective_at(1, 1), GridMap::kFree);
  EXPECT_EQ(model.effective_at(2, 1), GridMap::kFree);
  ASSERT_TRUE(model.undo());
  EXPECT_EQ(model.effective_at(1, 1), GridMap::kOccupied);
  ASSERT_TRUE(model.redo());
  EXPECT_EQ(model.effective_at(2, 1), GridMap::kFree);
}

TEST(RefinementModelTest, SelectedCellEraseStaysSparseAndRoundTripsHistoryAndPatch) {
  auto map = make_base_map();
  map.cells()[0] = GridMap::kUnknown;
  map.cells()[8] = GridMap::kOccupied;
  RefinementModel model;
  model.set_base_map(map, MapYamlMetadata());
  auto command = EraseRectangleCommand::create_cells(model, {0, 6, 6, 8, 24, 999});
  ASSERT_TRUE(command);
  ASSERT_EQ(command->operation().changes.size(), 2U);
  std::string error;
  ASSERT_TRUE(model.execute(std::move(command), &error)) << error;
  EXPECT_EQ(model.effective_at_index(0), GridMap::kUnknown);
  EXPECT_EQ(model.effective_at_index(6), GridMap::kFree);
  EXPECT_EQ(model.effective_at_index(7), GridMap::kOccupied); // Gap inside the bounding box.
  EXPECT_EQ(model.effective_at_index(8), GridMap::kFree);
  EXPECT_EQ(model.effective_at_index(24), GridMap::kFree);
  EXPECT_FALSE(EraseRectangleCommand::create_cells(model, {0, 6, 8, 24}));
  const auto directory = std::filesystem::temp_directory_path() / "agt_selected_cells_erase_test";
  std::filesystem::create_directories(directory);
  ASSERT_TRUE(model.save_refinement_yaml((directory / "history.yaml").string(), &error)) << error;
  ASSERT_TRUE(model.write_navigation_patch((directory / "patch.yaml").string(), &error)) << error;
  const auto patch = YAML::LoadFile((directory / "patch.yaml").string());
  ASSERT_EQ(patch["edits"].size(), 2U);
  EXPECT_EQ(patch["edits"][0]["mode"].as<std::string>(), "free");
  EXPECT_DOUBLE_EQ(patch["edits"][0]["polygon_m"][0][0].as<double>(), 1.);
  EXPECT_DOUBLE_EQ(patch["edits"][1]["polygon_m"][0][0].as<double>(), 3.);
  RefinementModel restored;
  restored.set_base_map(map, MapYamlMetadata());
  ASSERT_TRUE(restored.load_refinement_yaml((directory / "history.yaml").string(), &error)) << error;
  EXPECT_EQ(restored.effective_at_index(6), GridMap::kFree);
  ASSERT_TRUE(restored.undo());
  EXPECT_EQ(restored.effective_at_index(6), GridMap::kOccupied);
  EXPECT_EQ(restored.effective_at_index(8), GridMap::kOccupied);
  ASSERT_TRUE(restored.redo());
  EXPECT_EQ(restored.effective_at_index(8), GridMap::kFree);
  EXPECT_EQ(restored.effective_at_index(7), GridMap::kOccupied);
  std::filesystem::remove_all(directory);
}

TEST(RefinementModelTest, ObstacleLineChangesFreeCellsOnly) {
  RefinementModel model;
  model.set_base_map(make_base_map(), MapYamlMetadata());
  auto command = DrawObstacleCommand::create(model, {0.0, 0.0}, {3.0, 0.0}, 1.0);
  ASSERT_TRUE(command);
  std::string error;
  ASSERT_TRUE(model.execute(std::move(command), &error)) << error;
  EXPECT_EQ(model.effective_at(0, 0), GridMap::kOccupied);
  EXPECT_EQ(model.effective_at(1, 0), GridMap::kOccupied);
}

TEST(RefinementModelTest, ForbiddenPolygonDoesNotChangeOccupancy) {
  RefinementModel model;
  model.set_base_map(make_base_map(), MapYamlMetadata());
  const auto before = model.effective_at(1, 1);
  auto command = ForbiddenPolygonCommand::create(
      model, {{0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}});
  ASSERT_TRUE(command);
  std::string error;
  ASSERT_TRUE(model.execute(std::move(command), &error)) << error;
  EXPECT_EQ(model.effective_at(1, 1), before);
  ASSERT_EQ(model.forbidden_zones().size(), 1U);
  ASSERT_TRUE(model.undo());
  EXPECT_TRUE(model.forbidden_zones().empty());
  ASSERT_TRUE(model.redo());
  EXPECT_EQ(model.forbidden_zones().size(), 1U);
}

TEST(RefinementModelTest, RefinementRoundTripAndExport) {
  RefinementModel model;
  auto base = make_base_map();
  std::string error;
  base.set_source_paths("/tmp/base_map.yaml", "/tmp/map.pgm");
  model.set_base_map(base, MapYamlMetadata());
  auto command = EraseRectangleCommand::create(model, {0.5, 0.5}, {2.5, 1.5});
  ASSERT_TRUE(command);
  ASSERT_TRUE(model.execute(std::move(command), &error));
  auto forbidden = ForbiddenPolygonCommand::create(
      model, {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}});
  ASSERT_TRUE(forbidden);
  ASSERT_TRUE(model.execute(std::move(forbidden), &error));

  const auto directory = std::filesystem::temp_directory_path() / "agt_refinement_test";
  std::filesystem::remove_all(directory);
  ASSERT_TRUE(model.save_refinement_yaml((directory / "map_refinement.yaml").string(),
                                         &error)) << error;
  RefinementModel restored;
  restored.set_base_map(make_base_map(), MapYamlMetadata());
  ASSERT_TRUE(restored.load_refinement_yaml(
      (directory / "map_refinement.yaml").string(), &error)) << error;
  EXPECT_EQ(restored.effective_at(1, 1), GridMap::kFree);
  EXPECT_EQ(restored.forbidden_zones().size(), 1U);
  ASSERT_TRUE(model.export_navigation_map((directory / "navigation_map").string(),
                                          &error)) << error;
  EXPECT_TRUE(std::filesystem::exists(directory / "navigation_map/map.pgm"));
  EXPECT_TRUE(std::filesystem::exists(directory / "navigation_map/map.yaml"));
  EXPECT_TRUE(std::filesystem::exists(directory / "navigation_map/map_refinement.yaml"));
  EXPECT_TRUE(std::filesystem::exists(directory / "navigation_map/metadata.yaml"));
  GridMap exported_map;
  MapYamlMetadata exported_metadata;
  ASSERT_TRUE(MapYamlLoader::load(
      (directory / "navigation_map/map.yaml").string(), &exported_map,
      &exported_metadata, &error)) << error;
  EXPECT_EQ(exported_map.width(), 5U);
  EXPECT_EQ(exported_map.at(1, 1), GridMap::kFree);
  std::filesystem::remove_all(directory);
}

}  // namespace
}  // namespace agt_map_studio

TEST(RefinementExportRegression, AllCellClassesSurviveCustomNegateAndThresholds) {
  using namespace agt_map_studio;
  for (int kind = 0; kind < 3; ++kind) {
    GridMap map; std::string error;
    ASSERT_TRUE(map.set_geometry(3, 1, 1., 0., 0., &error));
    map.cells() = {GridMap::kOccupied, GridMap::kFree, GridMap::kUnknown};
    MapYamlMetadata metadata; metadata.negate = kind == 1;
    if (kind == 2) metadata.free_thresh = .4;
    RefinementModel model; model.set_base_map(map, metadata);
    auto directory = std::filesystem::temp_directory_path() /
                     ("agt_trinary_export_test_" + std::to_string(kind));
    ASSERT_TRUE(model.export_navigation_map(directory.string(), &error)) << error;
    GridMap reloaded; MapYamlMetadata output;
    ASSERT_TRUE(MapYamlLoader::load((directory / "map.yaml").string(), &reloaded, &output, &error)) << error;
    EXPECT_EQ(reloaded.cells(), map.cells());
    EXPECT_FALSE(output.negate); EXPECT_EQ(output.mode, "trinary");
    std::filesystem::remove_all(directory);
  }
}
