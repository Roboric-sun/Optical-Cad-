#pragma once
#include <QIcon>
#include <QTreeWidget>
#include <QWidget>
#include <functional>

class QHBoxLayout;
class QVBoxLayout;
class QToolButton;
class QLabel;
class QMainWindow;

// Code-native vector artwork, drawn in the visual language of the supplied ribbon.
QIcon officeIcon(const QString& name);
QString officeStyle();

class NavigatorTree : public QTreeWidget {
  protected:
    void resizeEvent(QResizeEvent*) override;
};

class RibbonGroup : public QWidget {
  public:
    explicit RibbonGroup(const QString& title, QWidget* parent = nullptr);
    QToolButton* large(const QString& icon, const QString& text, std::function<void()> action,
                       const QString& id = {});
    QVBoxLayout* column();
    QToolButton* small(QVBoxLayout*, const QString& icon, const QString& text,
                       std::function<void()> action, const QString& id = {});

  private:
    QHBoxLayout* commands_;
};

class OfficeTitleBar : public QWidget {
  public:
    explicit OfficeTitleBar(QMainWindow* window);
    void setTitle(const QString& text);

  protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

  private:
    QMainWindow* window_;
    QLabel* title_;
};
