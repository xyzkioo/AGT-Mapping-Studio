#include "ui/WorkflowPanel.hpp"
#include "tools/ExternalToolRunner.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QFileInfo>
#include <QTabWidget>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace agt_map_studio {

WorkflowPanel::WorkflowPanel(QWidget *parent) : QWidget(parent) {
  auto *outer = new QVBoxLayout(this);
  setObjectName(QStringLiteral("workflowPanel"));
  outer->setContentsMargins(0, 0, 0, 0);
  auto *tabs = new QTabWidget(this);
  tabs->setObjectName(QStringLiteral("workflowTabs"));

  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *host = new QWidget(scroll);
  host->setObjectName(QStringLiteral("workflowContent"));
  auto *layout = new QVBoxLayout(host);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(10);

  source_label_ = new QLabel(QStringLiteral("No source loaded"), host);
  source_label_->setWordWrap(true);
  source_label_->setObjectName(QStringLiteral("sourceSummary"));
  source_label_->setTextFormat(Qt::PlainText);
  layout->addWidget(source_label_);

  steps_[0] = make_step(1, QStringLiteral("3D refinement"),
                        QStringLiteral("Apply point cloud edits to a derived map."), host);
  steps_[1] = make_step(2, QStringLiteral("Relocalization"),
                        QStringLiteral("Build localization assets from the effective cloud."), host);
  steps_[2] = make_step(3, QStringLiteral("Navigation layers"),
                        QStringLiteral("Generate and validate the navigation map."), host);
  steps_[3] = make_step(4, QStringLiteral("2D refinement"),
                        QStringLiteral("Apply the edits drawn on the navigation map."), host);
  steps_[4] = make_step(5, QStringLiteral("Publish map"),
                        QStringLiteral("Save a new version to the map library."), host);
  for (auto &row : steps_) layout->addWidget(row.title->parentWidget());

  auto *parameters_scroll = new QScrollArea(tabs);
  parameters_scroll->setWidgetResizable(true);
  parameters_scroll->setFrameShape(QFrame::NoFrame);
  parameters_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *parameters_host = new QWidget(parameters_scroll);
  auto *parameters_layout = new QVBoxLayout(parameters_host);
  parameters_layout->setContentsMargins(12, 12, 12, 12);
  parameters_layout->setSpacing(14);
  auto *converter_box = new QGroupBox(QStringLiteral("Converter parameters"), parameters_host);
  converter_box_ = converter_box;
  auto *form = new QFormLayout(converter_box);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setSpacing(10);
  resolution_ = new QDoubleSpinBox(converter_box);
  resolution_->setRange(0.02, 1.0);
  resolution_->setSingleStep(0.01);
  resolution_->setDecimals(3);
  resolution_->setValue(0.10);
  margin_ = new QDoubleSpinBox(converter_box);
  margin_->setRange(0.0, 20.0);
  margin_->setDecimals(2);
  margin_->setValue(1.0);
  min_points_ = new QSpinBox(converter_box);
  min_points_->setRange(1, 100);
  min_points_->setValue(2);
  max_step_ = new QDoubleSpinBox(converter_box);
  max_step_->setRange(0.01, 2.0);
  max_step_->setDecimals(3);
  max_step_->setValue(0.22);
  max_slope_ = new QDoubleSpinBox(converter_box);
  max_slope_->setRange(1.0, 89.0);
  max_slope_->setDecimals(1);
  max_slope_->setValue(20.0);
  use_trajectory_ = new QCheckBox(QStringLiteral("Use mapping trajectory"), converter_box);
  use_trajectory_->setChecked(true);
  use_trajectory_->setToolTip(QStringLiteral("Use poses.txt as a drivable prior"));
  form->addRow(QStringLiteral("Resolution (m)"), resolution_);
  form->addRow(QStringLiteral("Margin (m)"), margin_);
  form->addRow(QStringLiteral("Min points / cell"), min_points_);
  form->addRow(QStringLiteral("Max step (m)"), max_step_);
  form->addRow(QStringLiteral("Max slope (deg)"), max_slope_);
  form->addRow(use_trajectory_);
  parameters_layout->addWidget(converter_box);
  for (auto *spin : {resolution_, margin_, max_step_, max_slope_}) {
    connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { emit parameters_changed(); });
  }
  connect(min_points_, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int) { emit parameters_changed(); });
  connect(use_trajectory_, &QCheckBox::toggled, this, [this](bool) { emit parameters_changed(); });

  auto *publish_box = new QGroupBox(QStringLiteral("Publish target"), parameters_host);
  auto *publish_form = new QFormLayout(publish_box);
  publish_form->setRowWrapPolicy(QFormLayout::WrapAllRows);
  publish_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  map_root_ = new QLineEdit(publish_box);
  map_id_ = new QLineEdit(publish_box);
  map_id_->setPlaceholderText(QStringLiteral("e.g. warehouse_mid360"));
  map_version_ = new QLineEdit(publish_box);
  map_version_->setPlaceholderText(QStringLiteral("e.g. v004-studio"));
  activate_ = new QCheckBox(QStringLiteral("Activate after publishing"), publish_box);
  publish_form->addRow(QStringLiteral("Map root"), map_root_);
  publish_form->addRow(QStringLiteral("Map id"), map_id_);
  publish_form->addRow(QStringLiteral("Version"), map_version_);
  publish_form->addRow(activate_);
  parameters_layout->addWidget(publish_box);
  parameters_layout->addStretch();
  parameters_scroll->setWidget(parameters_host);
  for (auto *edit : {map_root_, map_id_, map_version_}) {
    connect(edit, &QLineEdit::textChanged, this, [this](const QString &) { emit parameters_changed(); });
  }
  connect(activate_, &QCheckBox::toggled, this, [this](bool) { emit parameters_changed(); });

  auto *buttons = new QHBoxLayout();
  run_all_ = new QPushButton(QStringLiteral("Run pending steps"), host);
  cancel_ = new QPushButton(QStringLiteral("Cancel"), host);
  run_all_->setProperty("role", "primary");
  cancel_->setEnabled(false);
  buttons->addWidget(run_all_);
  buttons->addWidget(cancel_);

  connect(run_all_, &QPushButton::clicked, this, &WorkflowPanel::run_all_requested);
  connect(cancel_, &QPushButton::clicked, this, &WorkflowPanel::cancel_requested);

  progress_label_ = new QLabel(QStringLiteral("Idle"), host);
  progress_ = new QProgressBar(host);
  progress_->setRange(0, 1);
  progress_->setValue(0);
  progress_->setFixedHeight(6);
  progress_->setTextVisible(false);


  layout->addStretch();
  auto *log_page = new QWidget(tabs);
  auto *log_layout = new QVBoxLayout(log_page);
  log_layout->setContentsMargins(12, 12, 12, 12);
  auto *log_title = new QLabel(QStringLiteral("Tool output"), log_page);
  log_title->setProperty("role", "heading");
  log_layout->addWidget(log_title);
  log_ = new QPlainTextEdit(log_page);
  log_->setReadOnly(true);
  log_->setMaximumBlockCount(4000);
  log_->setMinimumHeight(160);
  log_->setPlaceholderText(QStringLiteral("External tool output appears here."));
  log_layout->addWidget(log_, 1);

  scroll->setWidget(host);
  tabs->addTab(scroll, QStringLiteral("Workflow"));
  tabs->addTab(parameters_scroll, QStringLiteral("Parameters"));
  tabs->addTab(log_page, QStringLiteral("Log"));
  outer->addWidget(tabs, 1);
  auto *footer = new QWidget(this);
  auto *footer_layout = new QVBoxLayout(footer);
  footer_layout->setContentsMargins(12, 10, 12, 12);
  footer_layout->addLayout(buttons);
  footer_layout->addWidget(progress_label_);
  footer_layout->addWidget(progress_);
  outer->addWidget(footer);
}

WorkflowPanel::StepRow WorkflowPanel::make_step(int number, const QString &title,
                                                const QString &hint, QWidget *host) {
  auto *frame = new QFrame(host);
  frame->setObjectName(QStringLiteral("workflowCard"));
  auto *layout = new QVBoxLayout(frame);
  layout->setContentsMargins(12, 10, 12, 10);
  layout->setSpacing(7);
  auto *header = new QHBoxLayout();
  StepRow row;
  auto *number_label = new QLabel(QString::number(number), frame);
  number_label->setObjectName(QStringLiteral("stepNumber"));
  number_label->setFixedSize(26, 26);
  number_label->setAlignment(Qt::AlignCenter);
  header->addWidget(number_label);
  row.title = new QLabel(title, frame);
  row.title->setObjectName(QStringLiteral("stepTitle"));
  row.badge = new QLabel(frame);
  row.badge->setAlignment(Qt::AlignCenter);
  row.badge->setMinimumWidth(56);
  header->addWidget(row.title, 1);
  header->addWidget(row.badge);
  layout->addLayout(header);
  row.detail = new QLabel(hint, frame);
  row.detail->setWordWrap(true);
  row.detail->setProperty("role", "muted");
  row.detail->setTextFormat(Qt::PlainText);
  layout->addWidget(row.detail);
  auto *actions = new QHBoxLayout();
  row.run = new QPushButton(QStringLiteral("Run"), frame);
  row.open = new QPushButton(QStringLiteral("Open output"), frame);
  row.open->setEnabled(false);
  actions->addWidget(row.run);
  actions->addWidget(row.open);
  actions->addStretch(1);
  layout->addLayout(actions);
  switch (number) {
    case 1: connect(row.run, &QPushButton::clicked, this, &WorkflowPanel::refine_requested); break;
    case 2: connect(row.run, &QPushButton::clicked, this, &WorkflowPanel::relocalization_requested); break;
    case 3: connect(row.run, &QPushButton::clicked, this, &WorkflowPanel::navigation_requested); break;
    case 4: connect(row.run, &QPushButton::clicked, this, &WorkflowPanel::patch_requested); break;
    default: connect(row.run, &QPushButton::clicked, this, &WorkflowPanel::publish_requested); break;
  }
  return row;
}

void WorkflowPanel::paint_badge(QLabel *badge, StageState state, bool applicable) {
  QString text;
  QString foreground;
  QString background;
  if (!applicable) {
    text = QStringLiteral("Skip"); foreground = QStringLiteral("#748197"); background = QStringLiteral("#f0f3f8");
  } else {
    switch (state) {
      case StageState::Fresh:
        text = QStringLiteral("Ready"); foreground = QStringLiteral("#238060"); background = QStringLiteral("#e8f6ef"); break;
      case StageState::Stale:
        text = QStringLiteral("Stale"); foreground = QStringLiteral("#ac711c"); background = QStringLiteral("#fff3df"); break;
      default:
        text = QStringLiteral("Pending"); foreground = QStringLiteral("#748197"); background = QStringLiteral("#f0f3f8"); break;
    }
  }
  badge->setText(text);
  badge->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border-radius: 5px; "
                                      "padding: 4px 6px; font-size: 11px; font-weight: 600; }")
                          .arg(background, foreground));
}

void WorkflowPanel::refresh(const WorkflowSession &session, bool tool_running) {
  const bool registered = session.algorithm_available();
  converter_box_->setEnabled(!registered && !tool_running);
  converter_box_->setVisible(!registered);
  converter_box_->setTitle(QStringLiteral("Converter parameters (pcd_to_nav_map)"));
  steps_[2].detail->setText(registered
      ? QStringLiteral("Use the registered navigation algorithm. Configure it when running.")
      : QStringLiteral("Registered algorithms, or pcd_to_nav_map + validate_nav_map."));
  if (session.empty()) {
    source_label_->setText(QStringLiteral("Open a map package, point cloud or 2D map to begin."));
    source_label_->setToolTip(QString());
  } else {
    source_label_->setText(QStringLiteral("%1\n%2")
        .arg(QFileInfo(session.source_pcd()).fileName(),
             session.source_is_mapping_package() ? QStringLiteral("Map package") : QStringLiteral("Point cloud")));
    source_label_->setToolTip(QStringLiteral("Source: %1\nWork directory: %2")
        .arg(session.source_pcd(), session.work_dir()));
  }
  const bool applicable[5] = {session.has_3d_edits(), true, true, session.has_2d_edits(), true};
  const WorkflowSession::Stage stages[5] = {
      WorkflowSession::Refine, WorkflowSession::Relocalization, WorkflowSession::Navigation,
      WorkflowSession::Patch, WorkflowSession::Publish};
  for (int i = 0; i < 5; ++i) {
    const auto &record = session.record(stages[i]);
    const StageState state = session.state(stages[i]);
    paint_badge(steps_[i].badge, state, applicable[i]);
    steps_[i].run->setEnabled(!tool_running && !session.empty() && applicable[i]);
    steps_[i].open->setEnabled(!record.path.isEmpty());
    steps_[i].open->disconnect();
    if (!record.path.isEmpty()) {
      const QString path = record.path;
      connect(steps_[i].open, &QPushButton::clicked, this,
              [this, path]() { emit open_output_requested(path); });
      steps_[i].detail->setToolTip(QStringLiteral("%1\n%2").arg(record.path, record.updated_at));
    }
  }
  if (!session.has_3d_edits()) {
    steps_[0].detail->setText(QStringLiteral("No point deletions. Using the source cloud."));
  } else {
    steps_[0].detail->setText(QStringLiteral("Point cloud edits are ready to apply."));
  }
  if (!session.has_2d_edits()) {
    steps_[3].detail->setText(QStringLiteral("No map edits. Using the generated layers."));
  } else {
    steps_[3].detail->setText(QStringLiteral("Navigation map edits are ready to apply."));
  }
  const QStringList reasons = session.blocking_reasons_for_publish();
  steps_[4].detail->setText(reasons.isEmpty()
                                ? QStringLiteral("Ready to publish.")
                                : QStringLiteral("Blocked: %1").arg(reasons.join(QStringLiteral("; "))));
  steps_[4].run->setEnabled(!tool_running && reasons.isEmpty());
  const bool reloc_available = ExternalToolRunner::ros2_executable_available(
      QStringLiteral("agt_global_relocalization_native"), QStringLiteral("build_relocalization_assets"));
  const bool convert_available = registered ||
      (ExternalToolRunner::ros2_executable_available(QStringLiteral("agt_map_converter"), QStringLiteral("pcd_to_nav_map")) &&
       ExternalToolRunner::ros2_executable_available(QStringLiteral("agt_map_converter"), QStringLiteral("validate_nav_map")));
  const bool patch_available = registered || session.navigation_uses_algorithm() ||
      ExternalToolRunner::ros2_executable_available(QStringLiteral("agt_map_converter"), QStringLiteral("patch_nav_map"));
  const bool publish_available = ExternalToolRunner::ros2_executable_available(
      QStringLiteral("agt_map_manager"), QStringLiteral("create_map_package"));
  for (const auto &item : {std::pair<int,bool>{1,reloc_available}, {2,convert_available},
                           {3,patch_available}, {4,publish_available}}) {
    if (item.second) continue;
    steps_[item.first].run->setEnabled(false);
    steps_[item.first].detail->setText(QStringLiteral("Required external tool is not installed."));
  }
  run_all_->setEnabled(!tool_running && !session.empty());
  if (!reloc_available && session.state(WorkflowSession::Relocalization) != StageState::Fresh) {
    run_all_->setEnabled(false);
    run_all_->setToolTip(QStringLiteral("Relocalization tool is not installed. Run available steps individually."));
  } else run_all_->setToolTip(QString());
  cancel_->setEnabled(tool_running);
}

void WorkflowPanel::append_log(const QString &text) {
  log_->moveCursor(QTextCursor::End);
  log_->insertPlainText(text);
  log_->moveCursor(QTextCursor::End);
}

void WorkflowPanel::clear_log() { log_->clear(); }

void WorkflowPanel::set_progress(const QString &label, bool busy) {
  progress_label_->setText(label);
  progress_->setRange(0, busy ? 0 : 1);
  progress_->setValue(busy ? 0 : 1);
}

void WorkflowPanel::read_converter(ConverterParameters *parameters) const {
  parameters->resolution = resolution_->value();
  parameters->margin = margin_->value();
  parameters->min_points = min_points_->value();
  parameters->max_step = max_step_->value();
  parameters->max_slope_deg = max_slope_->value();
  parameters->use_trajectory = use_trajectory_->isChecked();
}

void WorkflowPanel::set_converter(const ConverterParameters &parameters) {
  // Block every child: otherwise setValue emits parameters_changed while only
  // some fields have been restored, overwriting the caller's session settings.
  const QSignalBlocker b1(resolution_), b2(margin_), b3(min_points_),
      b4(max_step_), b5(max_slope_), b6(use_trajectory_);
  resolution_->setValue(parameters.resolution);
  margin_->setValue(parameters.margin);
  min_points_->setValue(parameters.min_points);
  max_step_->setValue(parameters.max_step);
  max_slope_->setValue(parameters.max_slope_deg);
  use_trajectory_->setChecked(parameters.use_trajectory);
}

void WorkflowPanel::read_publish_target(PublishTarget *target) const {
  target->map_root = map_root_->text().trimmed();
  target->map_id = map_id_->text().trimmed();
  target->map_version = map_version_->text().trimmed();
  target->activate = activate_->isChecked();
}

void WorkflowPanel::set_publish_target(const PublishTarget &target) {
  const QSignalBlocker b1(map_root_), b2(map_id_), b3(map_version_), b4(activate_);
  map_root_->setText(target.map_root);
  map_id_->setText(target.map_id);
  map_version_->setText(target.map_version);
  activate_->setChecked(target.activate);
}

}  // namespace agt_map_studio
