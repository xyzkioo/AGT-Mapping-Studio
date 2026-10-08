#include "occupancy/RefinementModel.hpp"
#include "occupancy/commands/DrawObstacleCommand.hpp"
#include "occupancy/commands/EraseRectangleCommand.hpp"
#include "occupancy/commands/ForbiddenPolygonCommand.hpp"
#include "occupancy/commands/FillPolygonCommand.hpp"

#include <yaml-cpp/yaml.h>

#include <cmath>
#include <functional>

#include <chrono>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace agt_map_studio {

void RefinementModel::set_base_map(GridMap map, MapYamlMetadata metadata) {
  base_map_ = std::move(map);
  metadata_ = metadata;
  cell_overrides_.clear();
  forbidden_zones_.clear();
  history_.clear();
  undo_stack_.clear();
  redo_stack_.clear();
  next_operation_id_ = 1;
}

void RefinementModel::clear() {
  base_map_.clear();
  cell_overrides_.clear();
  forbidden_zones_.clear();
  history_.clear();
  undo_stack_.clear();
  redo_stack_.clear();
  next_operation_id_ = 1;
}

std::int8_t RefinementModel::effective_at(std::uint32_t pixel_x,
                                          std::uint32_t pixel_y) const {
  if (pixel_x >= base_map_.width() || pixel_y >= base_map_.height()) {
    return GridMap::kUnknown;
  }
  return effective_at_index(static_cast<std::size_t>(pixel_y) * base_map_.width() +
                            pixel_x);
}

std::int8_t RefinementModel::effective_at_index(std::size_t index) const {
  const auto override = cell_overrides_.find(index);
  if (override != cell_overrides_.end()) return override->second;
  if (index >= base_map_.cells().size()) return GridMap::kUnknown;
  return base_map_.cells()[index];
}

bool RefinementModel::execute(std::unique_ptr<GridCommand> command,
                              std::string *error) {
  if (!command || !has_map()) {
    if (error) *error = "a base occupancy map and command are required";
    return false;
  }
  command->redo(*this);
  history_.push_back(command->operation());
  next_operation_id_ = std::max(next_operation_id_, command->operation().id + 1U);
  undo_stack_.push_back(std::move(command));
  redo_stack_.clear();
  return true;
}

bool RefinementModel::undo() {
  if (undo_stack_.empty()) return false;
  auto command = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  command->undo(*this);
  set_history_undone(command->operation().id, true);
  redo_stack_.push_back(std::move(command));
  return true;
}

bool RefinementModel::redo() {
  if (redo_stack_.empty()) return false;
  auto command = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  command->redo(*this);
  set_history_undone(command->operation().id, false);
  undo_stack_.push_back(std::move(command));
  return true;
}

void RefinementModel::apply_cell_changes(const std::vector<CellChange> &changes,
                                         bool after) {
  for (const auto &change : changes) {
    if (change.index >= base_map_.cells().size()) continue;
    const std::int8_t value = after ? change.after : change.before;
    if (value == base_map_.cells()[change.index]) {
      cell_overrides_.erase(change.index);
    } else {
      cell_overrides_[change.index] = value;
    }
  }
}

void RefinementModel::add_forbidden_zone(
    std::size_t id, const std::vector<GridWorldPoint> &polygon) {
  forbidden_zones_.push_back({id, polygon});
}

void RefinementModel::remove_forbidden_zone(std::size_t id) {
  forbidden_zones_.erase(
      std::remove_if(forbidden_zones_.begin(), forbidden_zones_.end(),
                     [id](const ForbiddenZone &zone) { return zone.id == id; }),
      forbidden_zones_.end());
}

void RefinementModel::set_history_undone(std::size_t id, bool undone) {
  for (auto &entry : history_) {
    if (entry.id == id) entry.undone = undone;
  }
}

std::string RefinementModel::timestamp_now() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - seconds)
                                .count();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
  localtime_r(&time, &local_time);
  std::ostringstream stream;
  stream << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setfill('0') << std::setw(3) << milliseconds;
  return stream.str();
}

bool RefinementModel::save_refinement_yaml(const std::string &path,
                                           std::string *error) const {
  if (!has_map()) {
    if (error) *error = "no base occupancy map loaded";
    return false;
  }
  try {
    const std::filesystem::path file_path(path);
    if (!file_path.parent_path().empty()) {
      std::filesystem::create_directories(file_path.parent_path());
    }
    YAML::Emitter emitter;
    emitter << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1
            << YAML::Key << "base_map" << YAML::Value << YAML::BeginMap
            << YAML::Key << "yaml" << YAML::Value << base_map_.yaml_path()
            << YAML::Key << "width" << YAML::Value << base_map_.width()
            << YAML::Key << "height" << YAML::Value << base_map_.height()
            << YAML::Key << "resolution" << YAML::Value << base_map_.resolution()
            << YAML::Key << "origin" << YAML::Value << YAML::Flow << YAML::BeginSeq
            << base_map_.origin_x() << base_map_.origin_y() << YAML::EndSeq
            << YAML::EndMap << YAML::Key << "operations" << YAML::Value
            << YAML::BeginSeq;
    for (const auto &entry : history_) {
      emitter << YAML::BeginMap << YAML::Key << "id" << YAML::Value << entry.id
              << YAML::Key << "type" << YAML::Value << entry.type
              << YAML::Key << "timestamp" << YAML::Value << entry.timestamp
              << YAML::Key << "undone" << YAML::Value << entry.undone
              << YAML::Key << "geometry" << YAML::Value << YAML::BeginMap;
      if (entry.type == "erase_rectangle" && entry.geometry.size() >= 2U) {
        emitter << YAML::Key << "min" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << entry.geometry[0].x << entry.geometry[0].y << YAML::EndSeq
                << YAML::Key << "max" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << entry.geometry[1].x << entry.geometry[1].y << YAML::EndSeq;
      } else if (entry.type == "erase_selected_cells") {
        emitter << YAML::Key << "cell_size_m" << YAML::Value << entry.width_m
                << YAML::Key << "cells_xy" << YAML::Value << YAML::BeginSeq;
        for (const auto &point : entry.geometry)
          emitter << YAML::Flow << YAML::BeginSeq << point.x << point.y << YAML::EndSeq;
        emitter << YAML::EndSeq;
      } else if (entry.type == "draw_obstacle" && entry.geometry.size() >= 2U) {
        emitter << YAML::Key << "start" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << entry.geometry[0].x << entry.geometry[0].y << YAML::EndSeq
                << YAML::Key << "end" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << entry.geometry[1].x << entry.geometry[1].y << YAML::EndSeq
                << YAML::Key << "width_m" << YAML::Value << entry.width_m;
      } else {
        emitter << YAML::Key << "polygon" << YAML::Value << YAML::BeginSeq;
        for (const auto &point : entry.geometry) {
          emitter << YAML::Flow << YAML::BeginSeq << point.x << point.y
                  << YAML::EndSeq;
        }
        emitter << YAML::EndSeq;
      }
      emitter << YAML::EndMap << YAML::Key << "changes" << YAML::Value
              << YAML::BeginSeq;
      for (const auto &change : entry.changes) {
        const auto x = static_cast<std::uint32_t>(change.index % base_map_.width());
        const auto y = static_cast<std::uint32_t>(change.index / base_map_.width());
        emitter << YAML::BeginMap << YAML::Key << "pixel" << YAML::Value << YAML::Flow
                << YAML::BeginSeq << x << y << YAML::EndSeq << YAML::Key << "before"
                << YAML::Value << static_cast<int>(change.before) << YAML::Key << "after"
                << YAML::Value << static_cast<int>(change.after) << YAML::EndMap;
      }
      emitter << YAML::EndSeq << YAML::EndMap;
    }
    emitter << YAML::EndSeq << YAML::Key << "undo_ids" << YAML::Value << YAML::BeginSeq;
    for (const auto &command : undo_stack_) emitter << command->operation().id;
    emitter << YAML::EndSeq << YAML::Key << "redo_ids" << YAML::Value << YAML::BeginSeq;
    for (const auto &command : redo_stack_) emitter << command->operation().id;
    emitter << YAML::EndSeq << YAML::EndMap;
    std::ofstream stream(file_path);
    if (!stream) {
      if (error) *error = "cannot write refinement file: " + path;
      return false;
    }
    stream << emitter.c_str() << '\n';
    if (!stream.good()) {
      if (error) *error = "failed writing refinement file: " + path;
      return false;
    }
  } catch (const std::exception &exception) {
    if (error) *error = exception.what();
    return false;
  }
  return true;
}

bool RefinementModel::load_refinement_yaml(const std::string &path,
                                           std::string *error) {
  if (!has_map()) {
    if (error) *error = "load a base map before loading refinement";
    return false;
  }
  try {
    const YAML::Node root = YAML::LoadFile(path);
    const YAML::Node operations = root["operations"];
    const auto base = root["base_map"];
    if (base && base["width"] &&
        (base["width"].as<unsigned>() != base_map_.width() ||
         base["height"].as<unsigned>() != base_map_.height() ||
         std::abs(base["resolution"].as<double>()-base_map_.resolution()) > 1e-6 ||
         std::abs(base["origin"][0].as<double>()-base_map_.origin_x()) > 1e-6 ||
         std::abs(base["origin"][1].as<double>()-base_map_.origin_y()) > 1e-6)) {
      if (error) *error = "refinement geometry differs from the loaded map";
      return false;
    }
    if (!operations || !operations.IsSequence()) {
      if (error) *error = "refinement operations must be a sequence";
      return false;
    }
    cell_overrides_.clear();
    forbidden_zones_.clear();
    history_.clear();
    undo_stack_.clear();
    redo_stack_.clear();
    next_operation_id_ = 1;
    for (const auto &node : operations) {
      RefinementOperation operation;
      operation.id = node["id"].as<std::size_t>();
      operation.type = node["type"].as<std::string>();
      operation.timestamp = node["timestamp"].as<std::string>("");
      operation.undone = node["undone"].as<bool>(false);
      const YAML::Node geometry = node["geometry"];
      if (operation.type == "erase_rectangle") {
        for (const char *key : {"min", "max"}) {
          const auto point = geometry[key];
          operation.geometry.push_back({point[0].as<double>(), point[1].as<double>()});
        }
      } else if (operation.type == "erase_selected_cells") {
        operation.width_m = geometry["cell_size_m"].as<double>();
        if (!(operation.width_m > 0.)) throw std::runtime_error("invalid selected cell size");
        for (const auto &point : geometry["cells_xy"])
          operation.geometry.push_back({point[0].as<double>(), point[1].as<double>()});
      } else if (operation.type == "draw_obstacle") {
        for (const char *key : {"start", "end"}) {
          const auto point = geometry[key];
          operation.geometry.push_back({point[0].as<double>(), point[1].as<double>()});
        }
        operation.width_m = geometry["width_m"].as<double>();
      } else if (geometry["polygon"]) {
        for (const auto &point : geometry["polygon"]) {
          operation.geometry.push_back({point[0].as<double>(), point[1].as<double>()});
        }
      }
      for (const auto &change : node["changes"]) {
        const auto pixel = change["pixel"];
        if (pixel[0].as<unsigned>() >= base_map_.width() || pixel[1].as<unsigned>() >= base_map_.height())
          throw std::runtime_error("refinement pixel is outside the map");
        const std::size_t index = static_cast<std::size_t>(pixel[1].as<std::uint32_t>()) *
                                  base_map_.width() + pixel[0].as<std::uint32_t>();
        operation.changes.push_back(
            {index, static_cast<std::int8_t>(change["before"].as<int>()),
             static_cast<std::int8_t>(change["after"].as<int>())});
      }
      if (!operation.undone) {
        apply_cell_changes(operation.changes, true);
        if (operation.type == "forbidden_polygon") {
          add_forbidden_zone(operation.id, operation.geometry);
        }
      }
      history_.push_back(operation);
      next_operation_id_ = std::max(next_operation_id_, operation.id + 1U);
    }
    const auto command_for = [this](std::size_t id) -> std::unique_ptr<GridCommand> {
      for (const auto &op : history_) {
        if (op.id != id) continue;
        if (op.type == "erase_rectangle" || op.type == "erase_selected_cells")
          return std::make_unique<EraseRectangleCommand>(op);
        if (op.type == "draw_obstacle") return std::make_unique<DrawObstacleCommand>(op);
        if (op.type == "forbidden_polygon") return std::make_unique<ForbiddenPolygonCommand>(op);
        if (op.type == "fill_free_polygon" || op.type == "fill_occupied_polygon" || op.type == "fill_unknown_polygon")
          return std::make_unique<FillPolygonCommand>(op);
      }
      throw std::runtime_error("unknown operation in saved undo/redo stack");
    };
    if (root["undo_ids"]) {
      for (const auto &id : root["undo_ids"]) undo_stack_.push_back(command_for(id.as<std::size_t>()));
      for (const auto &id : root["redo_ids"]) redo_stack_.push_back(command_for(id.as<std::size_t>()));
    } else {
      for (const auto &op : history_) if (!op.undone) undo_stack_.push_back(command_for(op.id));
    }
  } catch (const std::exception &exception) {
    if (error) *error = exception.what();
    return false;
  }
  return true;
}

bool RefinementModel::export_navigation_map(const std::string &output_dir,
                                            std::string *error) const {
  namespace fs = std::filesystem;
  static std::atomic<unsigned long> sequence{0};
  fs::path staging, backup;
  bool moved_old = false;
  try {
    const fs::path output = fs::absolute(output_dir).lexically_normal();
    if (output == output.root_path()) throw std::runtime_error("invalid map output directory");
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                        "_" + std::to_string(++sequence);
    staging = output.parent_path()/(output.filename().string()+".tmp_"+suffix);
    backup = output.parent_path()/(output.filename().string()+".backup_"+suffix);
    fs::create_directories(output.parent_path());
    if (fs::exists(output)) {
      if (!fs::is_directory(output) || fs::is_symlink(output)) throw std::runtime_error("map output must be a real directory");
      fs::copy(output, staging, fs::copy_options::recursive);
    } else fs::create_directory(staging);
    if (!write_navigation_files(staging.string(), error) || !write_keepout_zones((staging/"keepout_zones.yaml").string(), error)) {
      fs::remove_all(staging);
      return false;
    }
    if (fs::exists(output)) { fs::rename(output, backup); moved_old = true; }
    try { fs::rename(staging, output); }
    catch (...) { if (moved_old) fs::rename(backup, output); throw; }
    if (moved_old) { std::error_code ignored; fs::remove_all(backup, ignored); }
    return true;
  } catch (const std::exception &exception) {
    if (!staging.empty()) { std::error_code ignored; fs::remove_all(staging, ignored); }
    if (error) *error = exception.what();
    return false;
  }
}

bool RefinementModel::write_navigation_files(const std::string &output_dir,
                                             std::string *error) const {
  if (!has_map()) {
    if (error) *error = "no base occupancy map loaded";
    return false;
  }
  try {
    std::filesystem::create_directories(output_dir);
    const std::filesystem::path directory(output_dir);
    std::ofstream pgm(directory / "map.pgm", std::ios::binary);
    if (!pgm) {
      if (error) *error = "cannot write navigation map PGM";
      return false;
    }
    pgm << "P5\n" << base_map_.width() << ' ' << base_map_.height() << "\n255\n";
    for (std::int64_t image_y = static_cast<std::int64_t>(base_map_.height()) - 1;
         image_y >= 0; --image_y) {
      for (std::uint32_t x = 0; x < base_map_.width(); ++x) {
        const auto value = effective_at(x, static_cast<std::uint32_t>(image_y));
        unsigned char pixel = value == GridMap::kOccupied ? 0U
                             : value == GridMap::kFree ? 254U : 205U;
        if (metadata_.negate && value != GridMap::kUnknown) pixel = 255U - pixel;
        pgm.write(reinterpret_cast<const char *>(&pixel), 1);
      }
    }
    YAML::Emitter map_yaml;
    map_yaml << YAML::BeginMap << YAML::Key << "image" << YAML::Value << "map.pgm"
             << YAML::Key << "resolution" << YAML::Value << base_map_.resolution()
             << YAML::Key << "origin" << YAML::Value << YAML::Flow << YAML::BeginSeq
             << base_map_.origin_x() << base_map_.origin_y() << 0.0 << YAML::EndSeq
             << YAML::Key << "occupied_thresh" << YAML::Value << metadata_.occupied_thresh
             << YAML::Key << "free_thresh" << YAML::Value << metadata_.free_thresh
             << YAML::Key << "negate" << YAML::Value << (metadata_.negate ? 1 : 0)
             << YAML::Key << "mode" << YAML::Value << metadata_.mode << YAML::EndMap;
    std::ofstream yaml_stream(directory / "map.yaml");
    yaml_stream << map_yaml.c_str() << '\n';
    std::string refinement_error;
    if (!save_refinement_yaml((directory / "map_refinement.yaml").string(),
                              &refinement_error)) {
      if (error) *error = refinement_error;
      return false;
    }
    YAML::Emitter metadata;
    metadata << YAML::BeginMap << YAML::Key << "base_map_yaml" << YAML::Value
             << base_map_.yaml_path() << YAML::Key << "width" << YAML::Value
             << base_map_.width() << YAML::Key << "height" << YAML::Value
             << base_map_.height() << YAML::Key << "active_overrides" << YAML::Value
             << cell_overrides_.size() << YAML::Key << "forbidden_zones" << YAML::Value
             << forbidden_zones_.size() << YAML::Key << "operation_count" << YAML::Value
             << history_.size() << YAML::EndMap;
    std::ofstream metadata_stream(directory / "metadata.yaml");
    metadata_stream << metadata.c_str() << '\n';
    if (!pgm.good() || !yaml_stream.good() || !metadata_stream.good()) {
      if (error) *error = "failed writing navigation map asset";
      return false;
    }
  } catch (const std::exception &exception) {
    if (error) *error = exception.what();
    return false;
  }
  return true;
}

bool RefinementModel::has_active_operations() const {
  return std::any_of(history_.begin(), history_.end(),
                     [](const RefinementOperation &entry) { return !entry.undone; });
}

std::string RefinementModel::active_fingerprint() const {
  if (!has_active_operations()) return std::string();
  std::ostringstream stream;
  for (const auto &entry : history_) {
    if (entry.undone) continue;
    stream << entry.id << ':' << entry.type << ':' << entry.changes.size() << ':'
           << entry.geometry.size() << ':' << entry.width_m << ';';
    stream << std::setprecision(17);
    for (const auto &point : entry.geometry) stream << point.x << ',' << point.y << ';';
    for (const auto &change : entry.changes)
      stream << change.index << ':' << static_cast<int>(change.before) << ':' << static_cast<int>(change.after) << ';';
  }
  std::ostringstream hex;
  hex << std::hex << std::hash<std::string>{}(stream.str());
  return hex.str();
}

std::size_t RefinementModel::patch_edit_count() const {
  return static_cast<std::size_t>(std::count_if(
      history_.begin(), history_.end(), [](const RefinementOperation &entry) {
        return !entry.undone && entry.type != "forbidden_polygon" && !entry.changes.empty();
      }));
}

namespace {

std::vector<GridWorldPoint> patch_polygon_for(const RefinementOperation &entry,
                                              std::string *mode) {
  std::vector<GridWorldPoint> polygon;
  if (entry.type == "erase_rectangle" && entry.geometry.size() >= 2U) {
    *mode = "free";
    const auto &a = entry.geometry[0];
    const auto &b = entry.geometry[1];
    polygon = {{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}};
  } else if (entry.type == "draw_obstacle" && entry.geometry.size() >= 2U) {
    *mode = "occupied";
    const auto &a = entry.geometry[0];
    const auto &b = entry.geometry[1];
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = std::hypot(dx, dy);
    const double half = std::max(entry.width_m, 0.02) * 0.5;
    double nx = 0.0;
    double ny = half;
    double ex = 0.0;
    double ey = 0.0;
    if (length > 1e-9) {
      nx = -dy / length * half;
      ny = dx / length * half;
      ex = dx / length * half;  // extend the stroke by its half width (round caps)
      ey = dy / length * half;
    }
    polygon = {{a.x - ex + nx, a.y - ey + ny}, {b.x + ex + nx, b.y + ey + ny},
               {b.x + ex - nx, b.y + ey - ny}, {a.x - ex - nx, a.y - ey - ny}};
  } else if (entry.type == "fill_free_polygon") {
    *mode = "free";
    polygon = entry.geometry;
  } else if (entry.type == "fill_occupied_polygon") {
    *mode = "occupied";
    polygon = entry.geometry;
  } else if (entry.type == "fill_unknown_polygon") {
    *mode = "unknown";
    polygon = entry.geometry;
  }
  return polygon;
}

}  // namespace

bool RefinementModel::write_navigation_patch(const std::string &path,
                                             std::string *error) const {
  if (!has_map()) {
    if (error) *error = "no base occupancy map loaded";
    return false;
  }
  try {
    const std::filesystem::path file_path(path);
    if (!file_path.parent_path().empty()) {
      std::filesystem::create_directories(file_path.parent_path());
    }
    YAML::Emitter emitter;
    emitter << YAML::BeginMap
            << YAML::Key << "generator" << YAML::Value << "agt_map_studio"
            << YAML::Key << "base_map_yaml" << YAML::Value << base_map_.yaml_path()
            << YAML::Key << "created_at" << YAML::Value << timestamp_now()
            << YAML::Key << "edits" << YAML::Value << YAML::BeginSeq;
    for (const auto &entry : history_) {
      if (entry.undone || entry.type == "forbidden_polygon") continue;
      if (entry.type == "erase_selected_cells") {
        // Keep sparse selections sparse in the published patch as well.
        // A bounding rectangle could erase an unselected pole between cells.
        const double half = entry.width_m * .5;
        for (const auto &center : entry.geometry) {
          emitter << YAML::BeginMap << YAML::Key << "mode" << YAML::Value << "free"
                  << YAML::Key << "note" << YAML::Value
                  << ("studio op " + std::to_string(entry.id) + " erase_selected_cells")
                  << YAML::Key << "polygon_m" << YAML::Value << YAML::BeginSeq;
          for (const auto &point : std::vector<GridWorldPoint>{
                   {center.x-half, center.y-half}, {center.x+half, center.y-half},
                   {center.x+half, center.y+half}, {center.x-half, center.y+half}})
            emitter << YAML::Flow << YAML::BeginSeq << point.x << point.y << YAML::EndSeq;
          emitter << YAML::EndSeq << YAML::EndMap;
        }
        continue;
      }
      std::string mode;
      const auto polygon = patch_polygon_for(entry, &mode);
      if (polygon.size() < 3U || mode.empty()) continue;
      emitter << YAML::BeginMap << YAML::Key << "mode" << YAML::Value << mode
              << YAML::Key << "note" << YAML::Value
              << ("studio op " + std::to_string(entry.id) + " " + entry.type + " @ " +
                  entry.timestamp)
              << YAML::Key << "polygon_m" << YAML::Value << YAML::BeginSeq;
      for (const auto &point : polygon) {
        emitter << YAML::Flow << YAML::BeginSeq << point.x << point.y << YAML::EndSeq;
      }
      emitter << YAML::EndSeq << YAML::EndMap;
    }
    emitter << YAML::EndSeq << YAML::EndMap;
    std::ofstream stream(file_path);
    stream << emitter.c_str() << '\n';
    if (!stream.good()) {
      if (error) *error = "failed writing navigation patch: " + path;
      return false;
    }
  } catch (const std::exception &exception) {
    if (error) *error = exception.what();
    return false;
  }
  return true;
}

bool RefinementModel::write_keepout_zones(const std::string &path,
                                          std::string *error) const {
  try {
    const std::filesystem::path file_path(path);
    if (!file_path.parent_path().empty()) {
      std::filesystem::create_directories(file_path.parent_path());
    }
    YAML::Emitter emitter;
    emitter << YAML::BeginMap
            << YAML::Key << "version" << YAML::Value << 1
            << YAML::Key << "generator" << YAML::Value << "agt_map_studio"
            << YAML::Key << "frame_id" << YAML::Value << "map"
            << YAML::Key << "base_map_yaml" << YAML::Value << base_map_.yaml_path()
            << YAML::Key << "zones" << YAML::Value << YAML::BeginSeq;
    for (const auto &zone : forbidden_zones_) {
      emitter << YAML::BeginMap << YAML::Key << "id" << YAML::Value << zone.id
              << YAML::Key << "type" << YAML::Value << "keepout"
              << YAML::Key << "polygon_m" << YAML::Value << YAML::BeginSeq;
      for (const auto &point : zone.polygon) {
        emitter << YAML::Flow << YAML::BeginSeq << point.x << point.y << YAML::EndSeq;
      }
      emitter << YAML::EndSeq << YAML::EndMap;
    }
    emitter << YAML::EndSeq << YAML::EndMap;
    std::ofstream stream(file_path);
    stream << emitter.c_str() << '\n';
    if (!stream.good()) {
      if (error) *error = "failed writing keepout zones: " + path;
      return false;
    }
  } catch (const std::exception &exception) {
    if (error) *error = exception.what();
    return false;
  }
  return true;
}

}  // namespace agt_map_studio
