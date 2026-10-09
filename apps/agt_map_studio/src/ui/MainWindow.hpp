#pragma once

#include "occupancy/GridMap.hpp"
#include "occupancy/OccupancyViewer.hpp"
#include "occupancy/RefinementModel.hpp"
#include "selection/SelectionManager.h"
#include "tools/ExternalToolRunner.hpp"
#include "viewer/PointCloudViewer.hpp"
#include "workflow/WorkflowSession.hpp"

#include <QMainWindow>
#include <QSplitter>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>
#include <vector>

class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QMenu;
class QToolBar;
class QTabWidget;

namespace agt_map_studio {

class WorkflowPanel;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(const QString &config_path, QWidget *parent = nullptr);

  bool open_pcd(const QString &path, QString *error = nullptr);
  bool set_scalar_color_field(const QString &field_name,
                              std::optional<float> minimum,
                              std::optional<float> maximum, QString *error = nullptr);
  // A mapping package directory (map.pcd + manifest.yaml ...) or a v3 map
  // package directory (localization/global_map.pcd + navigation/map.yaml).
  bool open_mapping_package(const QString &directory, QString *error = nullptr);
  bool open_occupancy_map(const QString &path, QString *error = nullptr);
  bool save_occupancy_map(const QString &directory, QString *error = nullptr);
  bool save_session(QString *error = nullptr);
  bool open_session(const QString &session_file, QString *error = nullptr);
  // Lightweight post-mapping review: keep the PCD as provenance, but load
  // only the generated 2D map into the UI.
  bool open_mapping_review(const QString &package_dir, const QString &map_yaml,
                           const QString &review_output, QString *error = nullptr);

protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void open_pcd_dialog();
  void open_mapping_package_dialog();
  void open_occupancy_map_dialog();
  void open_session_dialog();
  void save_view_dialog();
  void export_clean_map_dialog();
  void generate_occupancy_preview_dialog();
  void save_refinement_dialog();
  void save_2d_map_dialog();
  void save_2d_map_as_dialog();
  void export_navigation_map_dialog();
  void confirm_mapping_review();
  void export_refinement_rules_dialog();
  void export_navigation_patch_dialog();
  void reset_camera();
  void undo_edit();
  void redo_edit();
  void set_mode_navigate();
  void set_mode_select();
  void set_mode_delete();
  void delete_selected();
  void select_height_band_dialog();
  void set_isometric_view();
  void set_front_view();
  void set_top_view();
  void show_3d_view();
  void show_2d_view();
  void show_linked_view();
  void set_occupancy_mode(OccupancyInteractionMode mode);
  void erase_selected_2d_cells();
  void toggle_axis(bool checked);
  void toggle_background(bool checked);
  void toggle_height_coloring(bool checked);
  void color_by_scalar_field_dialog();
  void show_controls();
  void show_workflow_help();
  void show_stats(const QString &text);

  // Workflow steps (each is asynchronous; completion continues the queue).
  void run_refine();
  void run_relocalization();
  void run_navigation();
  void run_patch();
  void run_publish();
  void run_all_pending();
  void cancel_tool();

private:
  using StepFn = std::function<void()>;

  void create_actions();
  void create_workspace();
  void update_workspace_title();
  void sync_xy_highlight();
  void disable_xy_link();
  void create_workflow_dock();
  void load_config(const QString &path);
  void apply_erase_rectangle(double min_x, double min_y, double max_x, double max_y);
  void apply_obstacle_line(double start_x, double start_y, double end_x, double end_y,
                           double width_m);
  void apply_forbidden_polygon(const QVector<QPointF> &polygon);
  void apply_fill_polygon(const QVector<QPointF> &polygon, int value);
  void refresh_occupancy_view();
  void sync_edit_fingerprints();
  void refresh_workflow();
  void set_source(const QString &pcd_path, const QString &package_dir);
  QString stamp() const;
  bool ensure_work_dir(QString *error);
  bool require_tools(const QStringList &required);
  QString tool_input_snapshot() const;
  void run_tool(const ToolInvocation &invocation, std::function<void(const ToolResult &)> on_done);
  void continue_queue();
  void fail_queue(const QString &message);
  bool replay_2d_history(const std::vector<RefinementOperation> &history);
  bool load_navigation_dir_into_2d(const QString &directory, QString *error);
  bool write_pipeline_config(QString *error) const;
  void save_session_quietly();
  bool run_mapping_algorithms();
  bool protect_unsaved_edits(bool *discarded = nullptr);
  bool activate_pcd(const QString &path, LoadedPointCloud loaded, QString *error);
  bool save_2d_interactively(bool choose_directory);
  bool has_unsaved_2d_edits() const;

  PointCloudViewer *viewer_ = nullptr;
  OccupancyViewer *occupancy_viewer_ = nullptr;
  QSplitter *view_splitter_ = nullptr;
  QList<int> linked_view_sizes_;
  QTabWidget *editor_tabs_ = nullptr;
  QAction *show_3d_action_ = nullptr;
  QAction *show_2d_action_ = nullptr;
  QAction *linked_action_ = nullptr;
  QLabel *source_name_label_ = nullptr;
  QLabel *workspace_caption_ = nullptr;
  QLabel *occupancy_tool_hint_ = nullptr;
  QWidget *occupancy_parameters_ = nullptr;
  QAction *finish_polygon_action_ = nullptr;
  QAction *undo_vertex_action_ = nullptr;
  QString map_view_path_;
  bool xy_link_enabled_ = false;
  QString linked_package_root_;
  std::vector<std::size_t> linked_selected_indices_;
  bool xy_highlight_cache_valid_ = false;
  QToolBar *toolbar_3d_ = nullptr;
  QToolBar *occupancy_toolbar_ = nullptr;
  QDockWidget *workflow_dock_ = nullptr;
  WorkflowPanel *workflow_panel_ = nullptr;
  QComboBox *selection_tool_combo_ = nullptr;
  QCheckBox *z_window_check_ = nullptr;
  QDoubleSpinBox *z_min_spin_ = nullptr;
  QDoubleSpinBox *z_max_spin_ = nullptr;
  QDoubleSpinBox *sphere_radius_spin_ = nullptr;
  QAction *hide_deleted_action_ = nullptr;
  QAction *isolate_selection_action_ = nullptr;
  QAction *show_axis_action_ = nullptr;
  QAction *dark_background_action_ = nullptr;
  QAction *height_coloring_action_ = nullptr;
  QAction *mode_navigate_action_ = nullptr;
  QAction *mode_select_action_ = nullptr;
  QAction *mode_delete_action_ = nullptr;
  QLabel *edit_state_label_ = nullptr;
  QMenu *view_menu_ = nullptr;
  QMenu *tools_menu_ = nullptr;
  QAction *confirm_review_action_ = nullptr;
  QAction *save_2d_action_ = nullptr;
  QAction *save_2d_as_action_ = nullptr;

  RefinementModel refinement_model_;
  SelectionManager selection_manager_;
  WorkflowSession session_;
  ExternalToolRunner tool_runner_;
  std::function<void(const ToolResult &)> tool_callback_;
  std::vector<StepFn> step_queue_;
  bool queue_running_ = false;
  QString source_path_;
  QString default_map_root_;
  QString review_base_map_;
  QString review_output_;
  bool review_mode_ = false;
  QString saved_2d_fingerprint_;
  QString saved_2d_directory_;
  QString saved_3d_fingerprint_;
};

}  // namespace agt_map_studio
