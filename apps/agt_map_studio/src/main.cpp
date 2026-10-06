#include "ui/MainWindow.hpp"

#include <ament_index_cpp/get_package_share_directory.hpp>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QMessageBox>

#include <exception>
#include <cmath>
#include <optional>

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  application.setApplicationName(QStringLiteral("agt_map_studio"));
  application.setApplicationVersion(QStringLiteral("0.3.0"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("AGT Map Studio: 3D point deletion, 2D navigation-map patching and map package publishing."));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption pcd_option(
      {QStringLiteral("p"), QStringLiteral("pcd")},
      QStringLiteral("Open a PCD file on startup."), QStringLiteral("path"));
  parser.addOption(pcd_option);
  const QCommandLineOption color_field_option(
      QStringLiteral("color-field"), QStringLiteral("Color points by a numeric PCD field."),
      QStringLiteral("name"));
  parser.addOption(color_field_option);
  const QCommandLineOption color_min_option(
      QStringLiteral("color-min"), QStringLiteral("Minimum value for scalar coloring."),
      QStringLiteral("value"));
  parser.addOption(color_min_option);
  const QCommandLineOption color_max_option(
      QStringLiteral("color-max"), QStringLiteral("Maximum value for scalar coloring."),
      QStringLiteral("value"));
  parser.addOption(color_max_option);
  const QCommandLineOption map_option(
      {QStringLiteral("m"), QStringLiteral("map")},
      QStringLiteral("Open a Nav2 map.yaml on startup."), QStringLiteral("path"));
  parser.addOption(map_option);
  const QCommandLineOption package_option(
      {QStringLiteral("k"), QStringLiteral("package")},
      QStringLiteral("Open a mapping package or a maps/<id>/<version> map package directory."),
      QStringLiteral("dir"));
  parser.addOption(package_option);
  const QCommandLineOption session_option(
      {QStringLiteral("s"), QStringLiteral("session")},
      QStringLiteral("Restore a studio_session.yaml."), QStringLiteral("path"));
  parser.addOption(session_option);
  const QCommandLineOption review_package_option(
      QStringLiteral("review-package"),
      QStringLiteral("Open a mapping package in lightweight 2D review mode (PCD is not rendered)."),
      QStringLiteral("dir"));
  parser.addOption(review_package_option);
  const QCommandLineOption review_map_option(
      QStringLiteral("review-map"), QStringLiteral("Base map.yaml for lightweight 2D review."),
      QStringLiteral("path"));
  parser.addOption(review_map_option);
  const QCommandLineOption review_output_option(
      QStringLiteral("review-output"), QStringLiteral("Fixed output directory for the confirmed map."),
      QStringLiteral("dir"));
  parser.addOption(review_output_option);
  parser.addPositionalArgument(QStringLiteral("pcd"),
                               QStringLiteral("Optional positional PCD path."));
  parser.process(application);

  QString config_path;
  try {
    const std::string share =
        ament_index_cpp::get_package_share_directory("agt_map_studio");
    config_path = QString::fromStdString(share + "/config/config.yaml");
  } catch (const std::exception &) {
    // Running directly from a build tree is supported; defaults remain valid.
  }

  agt_map_studio::MainWindow window(config_path);
  const QString review_package = parser.value(review_package_option);
  const QString review_map = parser.value(review_map_option);
  const QString review_output = parser.value(review_output_option);
  const bool review_requested =
      !review_package.isEmpty() || !review_map.isEmpty() || !review_output.isEmpty();
  if (review_requested) {
    if (review_package.isEmpty() || review_map.isEmpty() || review_output.isEmpty()) {
      window.show();
      QMessageBox::critical(
          &window, QStringLiteral("Invalid review arguments"),
          QStringLiteral("--review-package, --review-map and --review-output must be used together."));
    } else {
      QString error;
      if (!window.open_mapping_review(QFileInfo(review_package).absoluteFilePath(),
                                      QFileInfo(review_map).absoluteFilePath(),
                                      QFileInfo(review_output).absoluteFilePath(), &error)) {
        window.show();
        QMessageBox::critical(&window, QStringLiteral("Open review failed"), error);
      }
    }
  }
  QString pcd_path = parser.value(pcd_option);
  if (pcd_path.isEmpty() && !parser.positionalArguments().isEmpty()) {
    pcd_path = parser.positionalArguments().first();
  }
  if (!review_requested && !pcd_path.isEmpty()) {
    QString error;
    if (!window.open_pcd(QFileInfo(pcd_path).absoluteFilePath(), &error)) {
      window.show();
      QMessageBox::critical(&window, QStringLiteral("Open PCD failed"), error);
    } else {
      const QString field = parser.value(color_field_option);
      const QString minimum_text = parser.value(color_min_option);
      const QString maximum_text = parser.value(color_max_option);
      if (!field.isEmpty() || !minimum_text.isEmpty() || !maximum_text.isEmpty()) {
        std::optional<float> minimum;
        std::optional<float> maximum;
        bool minimum_ok = true;
        bool maximum_ok = true;
        if (!minimum_text.isEmpty()) minimum = minimum_text.toFloat(&minimum_ok);
        if (!maximum_text.isEmpty()) maximum = maximum_text.toFloat(&maximum_ok);
        if (field.isEmpty() || !minimum_ok || !maximum_ok ||
            (minimum && !std::isfinite(*minimum)) ||
            (maximum && !std::isfinite(*maximum))) {
          window.show();
          QMessageBox::critical(
              &window, QStringLiteral("Invalid scalar color options"),
              QStringLiteral("Use --color-field NAME and optional finite --color-min / --color-max values."));
        } else if (!window.set_scalar_color_field(field, minimum, maximum, &error)) {
          window.show();
          QMessageBox::critical(&window, QStringLiteral("Scalar field failed"), error);
        }
      }
    }
  }
  const QString package_path = parser.value(package_option);
  if (!review_requested && !package_path.isEmpty()) {
    QString error;
    if (!window.open_mapping_package(QFileInfo(package_path).absoluteFilePath(), &error)) {
      window.show();
      QMessageBox::critical(&window, QStringLiteral("Open package failed"), error);
    }
  }
  const QString session_path = parser.value(session_option);
  if (!review_requested && !session_path.isEmpty()) {
    QString error;
    if (!window.open_session(QFileInfo(session_path).absoluteFilePath(), &error)) {
      window.show();
      QMessageBox::critical(&window, QStringLiteral("Open session failed"), error);
    }
  }
  const QString map_path = parser.value(map_option);
  if (!review_requested && !map_path.isEmpty()) {
    QString error;
    if (!window.open_occupancy_map(QFileInfo(map_path).absoluteFilePath(), &error)) {
      window.show();
      QMessageBox::critical(&window, QStringLiteral("Open Occupancy Map failed"), error);
    }
  }
  window.show();
  return application.exec();
}
