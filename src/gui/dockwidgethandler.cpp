/*****************************************************************************
* Copyright 2015-2020 Alexander Barthel alex@littlenavmap.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "gui/dockwidgethandler.h"

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QDebug>
#include <QEvent>
#include <QMouseEvent>
#include <QStatusBar>
#include <QMenuBar>
#include <QToolBar>

namespace atools {
namespace gui {

/* Saves the main window states and states of all attached widgets like the status bars and the menu bar. */
struct MainWindowState
{
  /* Copy state to main window and all related widgets */
  void toWindow(QMainWindow *mainWindow) const;

  /* Save state from main window and all related widgets */
  void fromWindow(const QMainWindow *mainWindow);

  /* Create an initial fullscreen configuration without docks and toobars depending on configuration */
  void initFullscreen(atools::gui::DockFlags flags);

  /* Clear all and set valid to false */
  void clear();

  /* false if default constructed or cleared */
  bool isValid() const
  {
    return valid;
  }

  QByteArray mainWindowState; // State from main window including toolbars and dock widgets
  QSize mainWindowSize;
  QPoint mainWindowPosition;
  Qt::WindowStates mainWindowStates = Qt::WindowNoState;

  bool statusBarVisible = true, menuVisible = true, // Not covered by saveState in main window
       valid = false, verbose = false;
};

QDebug operator<<(QDebug out, const MainWindowState& obj)
{
  QDebugStateSaver saver(out);
  out.noquote().nospace() << "MainWindowState["
                          << "size " << obj.mainWindowState.size()
                          << ", window size " << obj.mainWindowSize
                          << ", window position " << obj.mainWindowPosition
                          << ", window states " << obj.mainWindowStates
                          << ", statusbar " << obj.statusBarVisible
                          << ", menu " << obj.menuVisible
                          << ", valid " << obj.valid
                          << "]";
  return out;
}

void MainWindowState::toWindow(QMainWindow *mainWindow) const
{
  if(verbose)
    qDebug() << Q_FUNC_INFO << *this;

  if(!valid)
    qWarning() << Q_FUNC_INFO << "Calling on invalid state";

  mainWindow->setWindowState(mainWindowStates);
  if(!mainWindowStates.testFlag(Qt::WindowMaximized) && !mainWindowStates.testFlag(Qt::WindowFullScreen))
  {
    // Change size and position only if main window is not maximized or full screen
    if(mainWindowSize.isValid())
      mainWindow->resize(mainWindowSize);
    mainWindow->move(mainWindowPosition);
  }
  mainWindow->statusBar()->setVisible(statusBarVisible);
  mainWindow->menuWidget()->setVisible(menuVisible);

  // Restores the state of this mainwindow's toolbars and dockwidgets. Also restores the corner settings too.
  // Has to be called after setting size to avoid unwanted widget resizing
  if(!mainWindowState.isEmpty())
    mainWindow->restoreState(mainWindowState);
}

void MainWindowState::fromWindow(const QMainWindow *mainWindow)
{
  clear();
  mainWindowState = mainWindow->saveState();
  mainWindowSize = mainWindow->size();
  mainWindowPosition = mainWindow->pos();
  mainWindowStates = mainWindow->windowState();
  statusBarVisible = mainWindow->statusBar()->isVisible();
  menuVisible = mainWindow->menuWidget()->isVisible();
  valid = true;

  if(verbose)
    qDebug() << Q_FUNC_INFO << *this;
}

void MainWindowState::initFullscreen(atools::gui::DockFlags flags)
{
  clear();

  mainWindowStates = flags.testFlag(MAXIMIZE) ? Qt::WindowMaximized : Qt::WindowFullScreen;
  statusBarVisible = !flags.testFlag(HIDE_STATUSBAR);
  menuVisible = !flags.testFlag(HIDE_MENUBAR);
  valid = true;

  if(verbose)
    qDebug() << Q_FUNC_INFO << *this;
}

void MainWindowState::clear()
{
  mainWindowState.clear();
  mainWindowSize = QSize();
  mainWindowPosition = QPoint();
  mainWindowStates = Qt::WindowNoState;
  statusBarVisible = true,
  menuVisible = true;
  valid = false;
}

QDataStream& operator<<(QDataStream& out, const atools::gui::MainWindowState& state)
{
  out << state.valid << state.mainWindowState << state.mainWindowSize << state.mainWindowPosition
      << state.mainWindowStates << state.statusBarVisible << state.menuVisible;
  return out;
}

QDataStream& operator>>(QDataStream& in, atools::gui::MainWindowState& state)
{
  in >> state.valid >> state.mainWindowState >> state.mainWindowSize >> state.mainWindowPosition
  >> state.mainWindowStates >> state.statusBarVisible >> state.menuVisible;
  return in;
}

// ===================================================================================
class DockEventFilter :
  public QObject
{
public:
  DockEventFilter()
  {

  }

  bool autoRaiseDockWindow = false, autoRaiseMainWindow = false;

private:
  virtual bool eventFilter(QObject *object, QEvent *event) override;

};

bool DockEventFilter::eventFilter(QObject *object, QEvent *event)
{
  if(event->type() == QEvent::Enter)
  {
    if(autoRaiseDockWindow)
    {
      QDockWidget *widget = dynamic_cast<QDockWidget *>(object);
      if(widget != nullptr)
      {
        qDebug() << Q_FUNC_INFO << event->type() << widget->objectName();
        if(widget->isFloating())
        {
          widget->activateWindow();
          widget->raise();
        }
      }
    }

    if(autoRaiseMainWindow)
    {
      QMainWindow *mainWindow = dynamic_cast<QMainWindow *>(object);
      if(mainWindow != nullptr)
      {
        mainWindow->activateWindow();
        mainWindow->raise();
      }
    }
  }

  return QObject::eventFilter(object, event);
}

// ===================================================================================
DockWidgetHandler::DockWidgetHandler(QMainWindow *parentMainWindow, const QList<QDockWidget *>& dockWidgetsParam,
                                     const QList<QToolBar *>& toolBarsParam, bool verboseLog)
  : QObject(parentMainWindow), mainWindow(parentMainWindow), dockWidgets(dockWidgetsParam), toolBars(toolBarsParam),
  verbose(verboseLog)
{
  dockEventFilter = new DockEventFilter();
  normalState = new MainWindowState;
  normalState->verbose = verbose;
  fullscreenState = new MainWindowState;
  fullscreenState->verbose = verbose;
}

DockWidgetHandler::~DockWidgetHandler()
{
  delete dockEventFilter;
  delete normalState;
  delete fullscreenState;
}

void DockWidgetHandler::dockTopLevelChanged(bool topLevel)
{
  if(verbose)
    qDebug() << Q_FUNC_INFO;

  Q_UNUSED(topLevel)
  updateDockTabStatus();
}

void DockWidgetHandler::dockLocationChanged(Qt::DockWidgetArea area)
{
  if(verbose)
    qDebug() << Q_FUNC_INFO;

  Q_UNUSED(area)
  updateDockTabStatus();
}

void DockWidgetHandler::connectDockWindow(QDockWidget *dockWidget)
{
  updateDockTabStatus();
  connect(dockWidget->toggleViewAction(), &QAction::toggled, this, &DockWidgetHandler::dockViewToggled);
  connect(dockWidget, &QDockWidget::dockLocationChanged, this, &DockWidgetHandler::dockLocationChanged);
  connect(dockWidget, &QDockWidget::topLevelChanged, this, &DockWidgetHandler::dockTopLevelChanged);
  dockWidget->installEventFilter(dockEventFilter);
}

void DockWidgetHandler::toggledDockWindow(QDockWidget *dockWidget, bool checked)
{
  bool handle = handleDockViews;

  // Do not remember stacks triggered by signals
  handleDockViews = false;

  if(checked)
  {
    // Find a stack that contains the widget ==================
    auto it = std::find_if(dockStackList.begin(), dockStackList.end(),
                           [dockWidget](QList<QDockWidget *>& list)
        {
          return list.contains(dockWidget);
        });

    if(it != dockStackList.end())
    {
      // Found a stack now show all stack member widgets
      for(QDockWidget *dock : *it)
      {
        if(dock != dockWidget)
          dock->show();
      }
    }

    // Show the widget whose action fired
    dockWidget->show();
    dockWidget->activateWindow();
    dockWidget->raise();
  }
  else
  {
    // Even floating widgets can have tabified buddies - ignore floating
    if(!dockWidget->isFloating())
    {
      for(QDockWidget *dock : mainWindow->tabifiedDockWidgets(dockWidget))
      {
        if(!dock->isFloating())
          dock->close();
      }
    }
  }
  handleDockViews = handle;
}

void DockWidgetHandler::updateDockTabStatus()
{
  if(handleDockViews)
  {
    dockStackList.clear();
    for(QDockWidget *dock : dockWidgets)
      updateDockTabStatus(dock);
  }
}

void DockWidgetHandler::updateDockTabStatus(QDockWidget *dockWidget)
{
  if(dockWidget->isFloating())
    return;

  QList<QDockWidget *> tabified = mainWindow->tabifiedDockWidgets(dockWidget);
  if(!tabified.isEmpty())
  {
    auto it = std::find_if(dockStackList.begin(), dockStackList.end(), [dockWidget](QList<QDockWidget *>& list) -> bool
        {
          return list.contains(dockWidget);
        });

    if(it == dockStackList.end())
    {
      auto rmIt = std::remove_if(tabified.begin(), tabified.end(), [](QDockWidget *dock) -> bool
          {
            return dock->isFloating();
          });
      if(rmIt != tabified.end())
        tabified.erase(rmIt, tabified.end());

      if(!tabified.isEmpty())
      {
        tabified.append(dockWidget);
        dockStackList.append(tabified);
      }
    }
  }
}

void DockWidgetHandler::dockViewToggled()
{
  if(verbose)
    qDebug() << Q_FUNC_INFO;

  if(handleDockViews)
  {
    QAction *action = dynamic_cast<QAction *>(sender());
    if(action != nullptr)
    {
      bool checked = action->isChecked();
      for(QDockWidget *dock : dockWidgets)
      {
        if(action == dock->toggleViewAction())
          toggledDockWindow(dock, checked);
      }
    }
  }
}

void DockWidgetHandler::activateWindow(QDockWidget *dockWidget)
{
  qDebug() << Q_FUNC_INFO;
  dockWidget->show();
  dockWidget->activateWindow();
  dockWidget->raise();
}

void DockWidgetHandler::setHandleDockViews(bool value)
{
  handleDockViews = value;
  updateDockTabStatus();
}

bool DockWidgetHandler::isAutoRaiseDockWindows() const
{
  return dockEventFilter->autoRaiseDockWindow;
}

void DockWidgetHandler::setAutoRaiseDockWindows(bool value)
{
  dockEventFilter->autoRaiseDockWindow = value;
}

bool DockWidgetHandler::isAutoRaiseMainWindow() const
{
  return dockEventFilter->autoRaiseMainWindow;
}

void DockWidgetHandler::setAutoRaiseMainWindow(bool value)
{
  dockEventFilter->autoRaiseMainWindow = value;
}

void DockWidgetHandler::setDockingAllowed(bool value)
{
  if(allowedAreas.isEmpty())
  {
    // Create backup
    for(QDockWidget *dock : dockWidgets)
      allowedAreas.append(dock->allowedAreas());
  }

  if(value)
  {
    // Restore backup
    for(int i = 0; i < dockWidgets.size(); i++)
      dockWidgets[i]->setAllowedAreas(allowedAreas.value(i, Qt::AllDockWidgetAreas));
  }
  else
  {
    // Forbid docking for all widgets
    for(QDockWidget *dock : dockWidgets)
      dock->setAllowedAreas(value ? Qt::AllDockWidgetAreas : Qt::NoDockWidgetArea);
  }
}

void DockWidgetHandler::raiseFloatingWindow(QDockWidget *dockWidget)
{
  qDebug() << Q_FUNC_INFO;
  if(dockWidget->isVisible() && dockWidget->isFloating())
    dockWidget->raise();
}

void DockWidgetHandler::connectDockWindows()
{
  for(QDockWidget *dock : dockWidgets)
    connectDockWindow(dock);
  mainWindow->installEventFilter(dockEventFilter);
}

void DockWidgetHandler::raiseFloatingWindows()
{
  for(QDockWidget *dock : dockWidgets)
    raiseFloatingWindow(dock);
}

// ==========================================================================
// Fullscreen methods

void DockWidgetHandler::setFullScreenOn(atools::gui::DockFlags flags)
{
  if(!fullscreen)
  {
    if(verbose)
      qDebug() << Q_FUNC_INFO;

    // Copy window layout to state
    normalState->fromWindow(mainWindow);

    if(!fullscreenState->isValid())
    {
      // No saved fullscreen configuration yet - create a new one
      fullscreenState->initFullscreen(flags);

      if(flags.testFlag(HIDE_TOOLBARS))
      {
        for(QToolBar *toolBar: toolBars)
          toolBar->setVisible(false);
      }

      if(flags.testFlag(HIDE_DOCKS))
      {
        for(QDockWidget *dockWidget : dockWidgets)
          dockWidget->setVisible(false);
      }
    }

    // Main window to fullscreen
    fullscreenState->toWindow(mainWindow);

    fullscreen = true;
    delayedFullscreen = false;
  }
  else
    qWarning() << Q_FUNC_INFO << "Already fullscreen";
}

void DockWidgetHandler::setFullScreenOff()
{
  if(fullscreen)
  {
    if(verbose)
      qDebug() << Q_FUNC_INFO;

    // Save full screen layout
    fullscreenState->fromWindow(mainWindow);

    // Assign normal state to window
    normalState->toWindow(mainWindow);

    fullscreen = false;
    delayedFullscreen = false;
  }
  else
    qWarning() << Q_FUNC_INFO << "Already no fullscreen";
}

QByteArray DockWidgetHandler::saveState()
{
  // Save current state - other state was saved when switching fs/normal
  if(fullscreen)
    fullscreenState->fromWindow(mainWindow);
  else
    normalState->fromWindow(mainWindow);

  qDebug() << Q_FUNC_INFO << "normalState" << *normalState;
  qDebug() << Q_FUNC_INFO << "fullscreenState" << *fullscreenState;

  // Save states for each mode and also fullscreen status
  QByteArray data;
  QDataStream stream(&data, QIODevice::WriteOnly);
  stream << fullscreen << *normalState << *fullscreenState;
  return data;
}

void DockWidgetHandler::restoreState(QByteArray data)
{
  QDataStream stream(&data, QIODevice::ReadOnly);
  stream >> fullscreen >> *normalState >> *fullscreenState;
  delayedFullscreen = false;

  qDebug() << Q_FUNC_INFO << "normalState" << *normalState;
  qDebug() << Q_FUNC_INFO << "fullscreenState" << *fullscreenState;
}

void DockWidgetHandler::currentStateToWindow()
{
  if(verbose)
    qDebug() << Q_FUNC_INFO;

  if(fullscreen)
    fullscreenState->toWindow(mainWindow);
  else
    normalState->toWindow(mainWindow);
}

void DockWidgetHandler::normalStateToWindow()
{
  normalState->toWindow(mainWindow);
  delayedFullscreen = fullscreen; // Set flag to allow switch to fullscreen later after showing windows
  fullscreen = false;
}

void DockWidgetHandler::fullscreenStateToWindow()
{
  fullscreenState->toWindow(mainWindow);
  fullscreen = true;
  delayedFullscreen = false;
}

void DockWidgetHandler::resetWindowState(const QSize& size, const QString& resetWindowStateFileName)
{
  QFile file(resetWindowStateFileName);
  if(file.open(QIODevice::ReadOnly))
  {
    QByteArray bytes = file.readAll();

    if(!bytes.isEmpty())
    {
      qDebug() << Q_FUNC_INFO;

      // Reset also ends fullscreen mode
      fullscreen = false;

      // End maximized and fullscreen state
      mainWindow->setWindowState(Qt::WindowActive);

      // Move to origin and apply size
      mainWindow->move(0, 0);
      mainWindow->resize(size);

      // Reload state now. This has to be done after resizing the window.
      mainWindow->restoreState(bytes);

      normalState->fromWindow(mainWindow);
      fullscreenState->clear();
    }
    else
      qWarning() << Q_FUNC_INFO << "cannot read file" << resetWindowStateFileName << file.errorString();

    file.close();
  }
  else
    qWarning() << Q_FUNC_INFO << "cannot open file" << resetWindowStateFileName << file.errorString();
}

void DockWidgetHandler::registerMetaTypes()
{
  qRegisterMetaTypeStreamOperators<atools::gui::MainWindowState>();
}

} // namespace gui
} // namespace atools

Q_DECLARE_METATYPE(atools::gui::MainWindowState);
