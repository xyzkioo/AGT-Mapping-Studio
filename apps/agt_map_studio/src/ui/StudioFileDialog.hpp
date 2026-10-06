#pragma once

#include <QFileDialog>
#include <QAbstractItemView>
#include <QTimer>
#include <QTreeView>

namespace agt_map_studio {

// Use Qt's own file list rather than the platform/desktop portal dialog. Keep
// input focus in the list on first show, where arrows and wheel can navigate.
class StudioFileDialog : public QFileDialog {
public:
  StudioFileDialog(QWidget *parent, const QString &caption, const QString &directory,
                   const QString &filter = QString()) : QFileDialog(parent) {
    setOption(QFileDialog::DontUseNativeDialog, true);
    setOption(QFileDialog::DontUseCustomDirectoryIcons, true);
    setWindowTitle(caption);
    if (!directory.isEmpty()) setDirectory(directory);
    if (!filter.isEmpty()) setNameFilters(filter.split(QStringLiteral(";;")));
    setViewMode(QFileDialog::Detail);
    resize(900, 560);
    if (auto *view = findChild<QTreeView*>(QStringLiteral("treeView"))) {
      view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
      view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
      view->setFocusPolicy(Qt::StrongFocus);
    }
  }

  static QString getOpenFileName(QWidget *parent, const QString &caption,
                                 const QString &directory = QString(), const QString &filter = QString()) {
    StudioFileDialog dialog(parent, caption, directory, filter);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
  }

  static QString getSaveFileName(QWidget *parent, const QString &caption,
                                 const QString &path = QString(), const QString &filter = QString()) {
    StudioFileDialog dialog(parent, caption, QString(), filter);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    if (!path.isEmpty()) dialog.selectFile(path);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
  }

  static QString getExistingDirectory(QWidget *parent, const QString &caption,
                                      const QString &directory = QString()) {
    StudioFileDialog dialog(parent, caption, directory);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
  }

protected:
  void showEvent(QShowEvent *event) override {
    QFileDialog::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
      auto *view = findChild<QTreeView*>(QStringLiteral("treeView"));
      if (!view || !view->isVisible()) return;
      view->setFocus(Qt::OtherFocusReason);
      if (!view->currentIndex().isValid()) {
        const auto first = view->model()->index(0, 0, view->rootIndex());
        if (first.isValid()) view->setCurrentIndex(first);
      }
    });
  }
};

}  // namespace agt_map_studio
