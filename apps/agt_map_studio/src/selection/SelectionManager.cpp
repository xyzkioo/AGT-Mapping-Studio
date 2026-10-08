#include "selection/SelectionManager.h"

#include <Eigen/Geometry>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>

#include <pcl/io/pcd_io.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <set>

namespace agt_map_studio {
namespace {

void emit_geometry(YAML::Emitter &emitter, const SelectionGeometry &geometry) {
  emitter << YAML::Key << "type" << YAML::Value << geometry.rule_type;
  if (geometry.rule_type == "remove_polygon") {
    emitter << YAML::Key << "points" << YAML::Value << YAML::BeginSeq;
    for (const auto &point : geometry.polygon_xy) {
      emitter << YAML::Flow << YAML::BeginSeq << point.x() << point.y() << YAML::EndSeq;
    }
    emitter << YAML::EndSeq;
    if (geometry.has_z_range) {
      emitter << YAML::Key << "z_range" << YAML::Value << YAML::Flow << YAML::BeginSeq
              << geometry.z_min << geometry.z_max << YAML::EndSeq;
    }
  } else if (geometry.rule_type == "remove_height_band") {
    emitter << YAML::Key << "min_z" << YAML::Value << geometry.z_min
            << YAML::Key << "max_z" << YAML::Value << geometry.z_max;
  } else if (geometry.rule_type == "remove_sphere") {
    emitter << YAML::Key << "center" << YAML::Value << YAML::Flow << YAML::BeginMap
            << YAML::Key << "x" << YAML::Value << geometry.center.x()
            << YAML::Key << "y" << YAML::Value << geometry.center.y()
            << YAML::Key << "z" << YAML::Value << geometry.center.z() << YAML::EndMap
            << YAML::Key << "radius" << YAML::Value << geometry.radius;
  } else {
    emitter << YAML::Key << "min" << YAML::Value << YAML::Flow << YAML::BeginMap
            << YAML::Key << "x" << YAML::Value << geometry.box.min.x()
            << YAML::Key << "y" << YAML::Value << geometry.box.min.y()
            << YAML::Key << "z" << YAML::Value << geometry.box.min.z() << YAML::EndMap
            << YAML::Key << "max" << YAML::Value << YAML::Flow << YAML::BeginMap
            << YAML::Key << "x" << YAML::Value << geometry.box.max.x()
            << YAML::Key << "y" << YAML::Value << geometry.box.max.y()
            << YAML::Key << "z" << YAML::Value << geometry.box.max.z() << YAML::EndMap;
  }
}

}  // namespace

void SelectionManager::reset(std::size_t point_count) {
  source_sha256_.clear();
  statuses_.assign(point_count, PointStatus::VISIBLE);
  selected_indices_.clear();
  selection_geometry_ = SelectionGeometry();
  history_.clear();
  undo_stack_.clear();
  redo_stack_.clear();
  next_operation_id_ = 1;
  hide_deleted_ = false;
  isolate_selected_ = false;
}

void SelectionManager::select_points(const std::vector<std::size_t> &indices,
                                     const AxisAlignedBoundingBox &box) {
  SelectionGeometry geometry;
  geometry.rule_type = "remove_box";
  geometry.box = box;
  select_points(indices, geometry);
}

void SelectionManager::select_points(const std::vector<std::size_t> &indices,
                                     const SelectionGeometry &geometry) {
  for (auto &status : statuses_) {
    if (status == PointStatus::SELECTED) status = PointStatus::VISIBLE;
  }
  selected_indices_.clear();
  for (const auto index : indices) {
    if (index < statuses_.size() && statuses_[index] == PointStatus::VISIBLE) {
      statuses_[index] = PointStatus::SELECTED;
      selected_indices_.push_back(index);
    }
  }
  selection_geometry_ = selected_indices_.empty() ? SelectionGeometry() : geometry;
}

void SelectionManager::invert_selection() {
  selected_indices_.clear();
  for (std::size_t i = 0; i < statuses_.size(); ++i) {
    if (statuses_[i] == PointStatus::SELECTED) {
      statuses_[i] = PointStatus::VISIBLE;
    } else if (statuses_[i] == PointStatus::VISIBLE) {
      statuses_[i] = PointStatus::SELECTED;
      selected_indices_.push_back(i);
    }
  }
  // Inverse selections use exact indices on export; the viewer may rebuild
  // an evidence AABB for display.
  selection_geometry_ = SelectionGeometry();
  selection_geometry_.rule_type = "remove_box";
}

bool SelectionManager::delete_selected() {
  if (selected_indices_.empty()) return false;
  DeleteCommand command;
  command.indices = selected_indices_;
  command.before_status.reserve(command.indices.size());
  command.geometry = selection_geometry_;
  command.operation_id = next_operation_id_++;
  for (const auto index : command.indices) {
    command.before_status.push_back(statuses_[index]);
    statuses_[index] = PointStatus::DELETED;
  }
  EditOperation entry;
  entry.id = command.operation_id;
  entry.type = command.geometry.rule_type;
  entry.box = command.geometry.box;
  entry.geometry = command.geometry;
  entry.point_count = command.indices.size();
  entry.indices = command.indices;
  entry.timestamp = timestamp_now();
  history_.push_back(entry);
  undo_stack_.push_back(std::move(command));
  redo_stack_.clear();
  clear_selection();
  return true;
}

bool SelectionManager::undo() {
  if (undo_stack_.empty()) return false;
  DeleteCommand command = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  for (std::size_t i = 0; i < command.indices.size(); ++i) {
    statuses_[command.indices[i]] = command.before_status[i];
  }
  if (auto *entry = operation(command.operation_id)) entry->undone = true;
  selection_geometry_ = command.geometry;
  redo_stack_.push_back(std::move(command));
  rebuild_selection_from_statuses();
  return true;
}

bool SelectionManager::redo() {
  if (redo_stack_.empty()) return false;
  DeleteCommand command = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  for (const auto index : command.indices) statuses_[index] = PointStatus::DELETED;
  if (auto *entry = operation(command.operation_id)) entry->undone = false;
  undo_stack_.push_back(std::move(command));
  clear_selection();
  return true;
}

std::size_t SelectionManager::visible_count() const {
  return static_cast<std::size_t>(std::count_if(
      statuses_.begin(), statuses_.end(),
      [](PointStatus status) { return status != PointStatus::DELETED; }));
}

std::size_t SelectionManager::selected_count() const {
  return static_cast<std::size_t>(std::count(statuses_.begin(), statuses_.end(),
                                              PointStatus::SELECTED));
}

std::size_t SelectionManager::deleted_count() const {
  return static_cast<std::size_t>(std::count(statuses_.begin(), statuses_.end(),
                                              PointStatus::DELETED));
}

bool SelectionManager::has_active_deletes() const {
  return std::any_of(history_.begin(), history_.end(),
                     [](const EditOperation &entry) { return !entry.undone; });
}

QString SelectionManager::active_fingerprint() const {
  if (!has_active_deletes()) return QString();
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for (const auto &entry : history_) {
    if (entry.undone) continue;
    YAML::Emitter canonical;
    canonical.SetDoublePrecision(17);
    canonical.SetFloatPrecision(9);
    canonical << YAML::BeginMap;
    emit_geometry(canonical, entry.geometry);
    canonical << YAML::Key << "indices" << YAML::Value << entry.indices << YAML::EndMap;
    const std::string text = canonical.c_str();
    hash.addData(text.data(), static_cast<int>(text.size()));
  }
  return QString::fromLatin1(hash.result().toHex().left(16));
}

std::string SelectionManager::timestamp_now() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  const auto fraction = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - seconds).count();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
  localtime_r(&time, &local_time);
  std::ostringstream stream;
  stream << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setfill('0') << std::setw(3) << fraction;
  return stream.str();
}

void SelectionManager::clear_selection() {
  for (auto &status : statuses_) {
    if (status == PointStatus::SELECTED) status = PointStatus::VISIBLE;
  }
  selected_indices_.clear();
  selection_geometry_ = SelectionGeometry();
}

void SelectionManager::rebuild_selection_from_statuses() {
  selected_indices_.clear();
  for (std::size_t i = 0; i < statuses_.size(); ++i) {
    if (statuses_[i] == PointStatus::SELECTED) selected_indices_.push_back(i);
  }
  if (selected_indices_.empty()) selection_geometry_ = SelectionGeometry();
}

EditOperation *SelectionManager::operation(std::size_t id) {
  for (auto &entry : history_) {
    if (entry.id == id) return &entry;
  }
  return nullptr;
}

namespace {
YAML::Node geometry_node(const SelectionGeometry &g) {
  YAML::Node n;
  n["type"] = g.rule_type;
  n["min"] = std::vector<float>{g.box.min.x(), g.box.min.y(), g.box.min.z()};
  n["max"] = std::vector<float>{g.box.max.x(), g.box.max.y(), g.box.max.z()};
  n["valid"] = g.box.valid;
  n["center"] = std::vector<double>{g.center.x(), g.center.y(), g.center.z()};
  n["radius"] = g.radius;
  n["z_min"] = g.z_min; n["z_max"] = g.z_max; n["has_z_range"] = g.has_z_range;
  n["polygon"] = YAML::Node(YAML::NodeType::Sequence);
  for (const auto &p : g.polygon_xy) n["polygon"].push_back(std::vector<double>{p.x(), p.y()});
  return n;
}
SelectionGeometry read_geometry(const YAML::Node &n) {
  SelectionGeometry g;
  g.rule_type = n["type"].as<std::string>();
  if (g.rule_type != "remove_box" && g.rule_type != "remove_polygon" &&
      g.rule_type != "remove_height_band" && g.rule_type != "remove_sphere")
    throw std::runtime_error("Invalid selection geometry type");
  auto minimum = n["min"].as<std::vector<float>>();
  auto maximum = n["max"].as<std::vector<float>>();
  auto center = n["center"].as<std::vector<double>>();
  if (minimum.size() != 3 || maximum.size() != 3 || center.size() != 3)
    throw std::runtime_error("Invalid selection geometry coordinates");
  g.box.min = Eigen::Vector3f(minimum.data()); g.box.max = Eigen::Vector3f(maximum.data());
  g.box.valid = n["valid"].as<bool>(); g.center = Eigen::Vector3d(center.data());
  g.radius = n["radius"].as<double>(); g.z_min = n["z_min"].as<double>();
  g.z_max = n["z_max"].as<double>(); g.has_z_range = n["has_z_range"].as<bool>();
  for (const auto &point : n["polygon"]) {
    auto xy = point.as<std::vector<double>>();
    if (xy.size() != 2) throw std::runtime_error("Invalid polygon vertex");
    g.polygon_xy.emplace_back(xy[0], xy[1]);
  }
  return g;
}
}

bool SelectionManager::save_state(const QString &path, const QString &source_sha256,
                                   QString *error) const {
  try {
    YAML::Node root;
    root["version"] = 1; root["source_sha256"] = source_sha256.toStdString();
    root["point_count"] = statuses_.size(); root["next_id"] = next_operation_id_;
    root["selected"] = selected_indices_; root["selection_geometry"] = geometry_node(selection_geometry_);
    root["hide_deleted"] = hide_deleted_; root["isolate_selected"] = isolate_selected_;
    root["history"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto &entry : history_) {
      YAML::Node n;
      n["id"] = entry.id; n["geometry"] = geometry_node(entry.geometry);
      n["indices"] = entry.indices; n["timestamp"] = entry.timestamp; n["undone"] = entry.undone;
      root["history"].push_back(n);
    }
    root["undo"] = YAML::Node(YAML::NodeType::Sequence);
    root["redo"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto &cmd : undo_stack_) root["undo"].push_back(cmd.operation_id);
    for (const auto &cmd : redo_stack_) root["redo"].push_back(cmd.operation_id);
    YAML::Emitter out; out.SetDoublePrecision(17); out.SetFloatPrecision(9); out << root;
    QSaveFile file(path);
    const QByteArray bytes(out.c_str());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
      throw std::runtime_error("Cannot save 3D editing state");
    return true;
  } catch (const std::exception &ex) {
    if (error) *error = QString::fromUtf8(ex.what());
    return false;
  }
}

bool SelectionManager::load_state(const QString &path, const QString &source_sha256,
                                   std::size_t point_count, QString *error) {
  try {
    auto root = YAML::LoadFile(path.toStdString());
    if (source_sha256.isEmpty() || root["version"].as<int>() != 1 ||
        root["source_sha256"].as<std::string>() != source_sha256.toStdString() ||
        root["point_count"].as<std::size_t>() != point_count)
      throw std::runtime_error("3D editing state does not match the source PCD");
    SelectionManager candidate; candidate.reset(point_count);
    candidate.bind_source_sha256(source_sha256);
    candidate.next_operation_id_ = root["next_id"].as<std::size_t>();
    std::set<std::size_t> ids;
    for (const auto &n : root["history"]) {
      EditOperation entry;
      entry.id = n["id"].as<std::size_t>();
      if (!entry.id || entry.id >= candidate.next_operation_id_ || !ids.insert(entry.id).second)
        throw std::runtime_error("Invalid edit operation ID");
      entry.geometry = read_geometry(n["geometry"]); entry.box = entry.geometry.box;
      entry.type = entry.geometry.rule_type; entry.timestamp = n["timestamp"].as<std::string>();
      entry.undone = n["undone"].as<bool>(); entry.indices = n["indices"].as<std::vector<std::size_t>>();
      entry.point_count = entry.indices.size();
      std::set<std::size_t> unique;
      for (auto i : entry.indices) {
        if (i >= point_count || !unique.insert(i).second)
          throw std::runtime_error("Invalid edit point index");
        if (!entry.undone) {
          if (candidate.statuses_[i] == PointStatus::DELETED)
            throw std::runtime_error("Overlapping active deletions");
          candidate.statuses_[i] = PointStatus::DELETED;
        }
      }
      candidate.history_.push_back(std::move(entry));
    }
    std::set<std::size_t> stacked;
    for (const auto &key : {"undo", "redo"}) {
      auto &stack = std::string(key) == "undo" ? candidate.undo_stack_ : candidate.redo_stack_;
      for (const auto &id_node : root[key]) {
        const auto id = id_node.as<std::size_t>();
        const auto *entry = candidate.operation(id);
        if (!entry || !stacked.insert(id).second || entry->undone != (std::string(key) == "redo"))
          throw std::runtime_error("Invalid undo/redo stack");
        DeleteCommand cmd; cmd.indices = entry->indices; cmd.geometry = entry->geometry;
        cmd.operation_id = id; cmd.before_status.assign(cmd.indices.size(), PointStatus::SELECTED);
        stack.push_back(std::move(cmd));
      }
    }
    for (const auto &entry : candidate.history_)
      if (!entry.undone && !stacked.count(entry.id)) throw std::runtime_error("Missing active undo command");
    auto selected = root["selected"].as<std::vector<std::size_t>>();
    for (auto i : selected) {
      if (i >= point_count || candidate.statuses_[i] != PointStatus::VISIBLE)
        throw std::runtime_error("Invalid selected point index");
      candidate.statuses_[i] = PointStatus::SELECTED;
    }
    candidate.selected_indices_ = std::move(selected);
    candidate.selection_geometry_ = read_geometry(root["selection_geometry"]);
    candidate.hide_deleted_ = root["hide_deleted"].as<bool>();
    candidate.isolate_selected_ = root["isolate_selected"].as<bool>();
    *this = std::move(candidate);
    return true;
  } catch (const std::exception &ex) {
    if (error) *error = QString::fromUtf8(ex.what());
    return false;
  }
}

bool SelectionManager::write_refinement_rules(const QString &path, const QString &source_path,
                                              QString *error, const LoadedPointCloud *cloud) const {
  try {
    QDir().mkpath(QFileInfo(path).absolutePath());
    YAML::Emitter emitter;
    emitter << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1
            << YAML::Key << "generator" << YAML::Value << "agt_map_studio"
            << YAML::Key << "source_pcd" << YAML::Value << source_path.toStdString()
            << YAML::Key << "operations" << YAML::Value << YAML::BeginSeq;
    QByteArray source_digest;
    if (cloud) {
      QFile source(source_path);
      QCryptographicHash digest(QCryptographicHash::Sha256);
      if (!source.open(QIODevice::ReadOnly) || !digest.addData(&source))
        throw std::runtime_error("Cannot read source PCD for exact rules");
      source_digest = digest.result().toHex();
      if (!source_sha256_.isEmpty() && source_digest != source_sha256_.toLatin1())
        throw std::runtime_error("Source PCD changed since loading; reopen it before exporting rules");
      if (!cloud->source || cloud->source_indices.size() != statuses_.size())
        throw std::runtime_error("Point cloud does not match edit state");
    }
    for (const auto &entry : history_) {
      if (entry.undone) continue;
      emitter << YAML::BeginMap;
      if (cloud) {
        emitter << YAML::Key << "type" << YAML::Value << "remove_indices"
                << YAML::Key << "source_sha256" << YAML::Value << source_digest.toStdString()
                << YAML::Key << "source_point_count" << YAML::Value
                << static_cast<std::size_t>(cloud->source->width) * cloud->source->height
                << YAML::Key << "indices" << YAML::Value << YAML::BeginSeq;
        for (auto index : entry.indices) emitter << cloud->source_indices.at(index);
        emitter << YAML::EndSeq << YAML::Key << "selection_geometry" << YAML::Value << YAML::BeginMap;
        emit_geometry(emitter, entry.geometry);
        emitter << YAML::EndMap;
      } else {
        emit_geometry(emitter, entry.geometry);
      }
      emitter << YAML::Key << "studio_operation_id" << YAML::Value << entry.id
              << YAML::Key << "studio_point_count" << YAML::Value << entry.point_count
              << YAML::EndMap;
    }
    emitter << YAML::EndSeq << YAML::EndMap;
    QSaveFile stream(path);
    const QByteArray bytes(emitter.c_str());
    if (!stream.open(QIODevice::WriteOnly) || stream.write(bytes) != bytes.size() || !stream.commit()) {
      if (error) *error = QStringLiteral("Cannot write refinement rules: %1").arg(path);
      return false;
    }
  } catch (const std::exception &exception) {
    if (error) *error = QString::fromUtf8(exception.what());
    return false;
  }
  return true;
}

bool SelectionManager::export_clean_map(const LoadedPointCloud &cloud,
                                        const QString &output_dir,
                                        const QString &source_path,
                                        QString *error) const {
  if (!cloud.source || cloud.source_indices.size() != cloud.point_count() ||
      statuses_.size() != cloud.point_count()) {
    if (error) *error = QStringLiteral("Cloud/state layer size mismatch");
    return false;
  }
  QDir directory;
  if (!directory.mkpath(output_dir)) {
    if (error) *error = QStringLiteral("Cannot create export directory: %1").arg(output_dir);
    return false;
  }

  pcl::PCLPointCloud2 output = *cloud.source;
  output.width = static_cast<std::uint32_t>(visible_count());
  output.height = 1;
  output.row_step = output.width * output.point_step;
  output.data.clear();
  output.data.reserve(static_cast<std::size_t>(output.row_step));
  for (std::size_t i = 0; i < statuses_.size(); ++i) {
    if (statuses_[i] == PointStatus::DELETED) continue;
    const std::size_t source_index = cloud.source_indices[i];
    const std::size_t row = cloud.source->width == 0 ? 0 : source_index / cloud.source->width;
    const std::size_t col = cloud.source->width == 0 ? 0 : source_index % cloud.source->width;
    const std::size_t row_step = cloud.source->row_step == 0
                                     ? cloud.source->width * cloud.source->point_step
                                     : cloud.source->row_step;
    const std::size_t offset = row * row_step + col * cloud.source->point_step;
    if (offset + cloud.source->point_step > cloud.source->data.size()) {
      if (error) *error = QStringLiteral("Source record offset is outside PCD data");
      return false;
    }
    output.data.insert(output.data.end(), cloud.source->data.begin() + offset,
                       cloud.source->data.begin() + offset + cloud.source->point_step);
  }
  // Binary output: smaller and lossless compared with ASCII text.
  if (pcl::io::savePCDFile((QDir(output_dir).filePath("map.pcd")).toStdString(),
                           output, Eigen::Vector4f::Zero(),
                           Eigen::Quaternionf::Identity(), true) != 0) {
    if (error) *error = QStringLiteral("Failed to write clean map PCD");
    return false;
  }

  try {
    YAML::Emitter history_emitter;
    history_emitter << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1
                    << YAML::Key << "operations" << YAML::Value << YAML::BeginSeq;
    for (const auto &entry : history_) {
      history_emitter << YAML::BeginMap << YAML::Key << "id" << YAML::Value << entry.id
                      << YAML::Key << "undone" << YAML::Value << entry.undone
                      << YAML::Key << "point_count" << YAML::Value << entry.point_count
                      << YAML::Key << "timestamp" << YAML::Value << entry.timestamp
                      << YAML::Key << "rule" << YAML::Value << YAML::BeginMap;
      emit_geometry(history_emitter, entry.geometry);
      history_emitter << YAML::EndMap << YAML::EndMap;
    }
    history_emitter << YAML::EndSeq << YAML::EndMap;
    std::ofstream history_stream(QDir(output_dir).filePath("edit_history.yaml").toStdString());
    history_stream << history_emitter.c_str() << '\n';

    QString rules_error;
    if (!write_refinement_rules(QDir(output_dir).filePath("refinement.yaml"), source_path,
                                &rules_error, &cloud)) {
      if (error) *error = rules_error;
      return false;
    }

    YAML::Emitter metadata_emitter;
    metadata_emitter << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1
                     << YAML::Key << "source_pcd" << YAML::Value << source_path.toStdString()
                     << YAML::Key << "original_points" << YAML::Value << cloud.point_count()
                     << YAML::Key << "visible_points" << YAML::Value << visible_count()
                     << YAML::Key << "deleted_points" << YAML::Value << deleted_count()
                     << YAML::Key << "pcd_format" << YAML::Value << "binary"
                     << YAML::Key << "publishable" << YAML::Value << false
                     << YAML::Key << "note" << YAML::Value
                     << "clean_map is a preview artifact; publish through refined_mapping_source"
                     << YAML::Key << "fields" << YAML::Value << YAML::BeginSeq;
    for (const auto &field : cloud.source->fields) metadata_emitter << field.name;
    metadata_emitter << YAML::EndSeq << YAML::Key << "exported_at" << YAML::Value
                     << timestamp_now() << YAML::EndMap;
    std::ofstream metadata_stream(QDir(output_dir).filePath("metadata.yaml").toStdString());
    metadata_stream << metadata_emitter.c_str() << '\n';
    if (!history_stream.good() || !metadata_stream.good()) {
      if (error) *error = QStringLiteral("Failed while writing export metadata");
      return false;
    }
  } catch (const std::exception &exception) {
    if (error) *error = QStringLiteral("Metadata export failed: %1").arg(exception.what());
    return false;
  }
  return true;
}

}  // namespace agt_map_studio
