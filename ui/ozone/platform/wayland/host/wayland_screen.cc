// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/wayland_screen.h"

#include <set>
#include <vector>

#include "base/stl_util.h"
#include "ui/display/display.h"
#include "ui/display/display_finder.h"
#include "ui/display/display_list.h"
#include "ui/display/display_observer.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"
#include "ui/ozone/platform/wayland/host/wayland_cursor_position.h"
#include "ui/ozone/platform/wayland/host/wayland_window.h"

namespace ui {

WaylandScreen::WaylandScreen(WaylandConnection* connection)
    : connection_(connection), weak_factory_(this) {
  DCHECK(connection_);
}

WaylandScreen::~WaylandScreen() = default;

void WaylandScreen::OnOutputAddedOrUpdated(uint32_t output_id,
                                           const gfx::Rect& bounds,
                                           int32_t scale) {
  AddOrUpdateDisplay(output_id, bounds, scale);
}

void WaylandScreen::OnOutputRemoved(uint32_t output_id) {
  if (output_id == GetPrimaryDisplay().id()) {
    // First, set a new primary display as required by the |display_list_|. It's
    // safe to set any of the displays to be a primary one. Once the output is
    // completely removed, Wayland updates geometry of other displays. And a
    // display, which became the one to be nearest to the origin will become a
    // primary one.
    for (const auto& display : display_list_.displays()) {
      if (display.id() != output_id) {
        display_list_.AddOrUpdateDisplay(display,
                                         display::DisplayList::Type::PRIMARY);
        break;
      }
    }
  }
  display_list_.RemoveDisplay(output_id);
}

void WaylandScreen::AddOrUpdateDisplay(uint32_t output_id,
                                       const gfx::Rect& new_bounds,
                                       int32_t scale_factor) {
  display::Display changed_display(output_id);
  if (!display::Display::HasForceDeviceScaleFactor())
    changed_display.set_device_scale_factor(scale_factor);
  changed_display.set_bounds(new_bounds);
  changed_display.set_work_area(new_bounds);

  // There are 2 cases where |changed_display| must be set as primary:
  // 1. When it is the first one being added to the |display_list_|. Or
  // 2. If it is nearest the origin than the previous primary or has the same
  // origin as it. When an user, for example, swaps two side-by-side displays,
  // at some point, as the notification come in, both will have the same
  // origin.
  auto type = display::DisplayList::Type::NOT_PRIMARY;
  if (display_list_.displays().empty()) {
    type = display::DisplayList::Type::PRIMARY;
  } else {
    auto nearest_origin = GetDisplayNearestPoint({0, 0}).bounds().origin();
    auto changed_origin = changed_display.bounds().origin();
    if (changed_origin < nearest_origin || changed_origin == nearest_origin)
      type = display::DisplayList::Type::PRIMARY;
  }

  display_list_.AddOrUpdateDisplay(changed_display, type);

  auto* wayland_window_manager = connection_->wayland_window_manager();
  for (auto* window : wayland_window_manager->GetWindowsOnOutput(output_id))
    window->UpdateBufferScale(true);
}

base::WeakPtr<WaylandScreen> WaylandScreen::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

const std::vector<display::Display>& WaylandScreen::GetAllDisplays() const {
  return display_list_.displays();
}

display::Display WaylandScreen::GetPrimaryDisplay() const {
  auto iter = display_list_.GetPrimaryDisplayIterator();
  DCHECK(iter != display_list_.displays().end());
  return *iter;
}

display::Display WaylandScreen::GetDisplayForAcceleratedWidget(
    gfx::AcceleratedWidget widget) const {
  auto* window = connection_->wayland_window_manager()->GetWindow(widget);
  // A window might be destroyed by this time on shutting down the browser.
  if (!window)
    return GetPrimaryDisplay();

  const auto* parent_window = window->parent_window();
  const auto entered_outputs_ids = window->entered_outputs_ids();
  // Although spec says a surface receives enter/leave surface events on
  // create/move/resize actions, this might be called right after a window is
  // created, but it has not been configured by a Wayland compositor and it has
  // not received enter surface events yet. Another case is when a user switches
  // between displays in a single output mode - Wayland may not send enter
  // events immediately, which can result in empty container of entered ids
  // (check comments in WaylandWindow::RemoveEnteredOutputId). In this case,
  // it's also safe to return the primary display.
  // A child window will most probably enter the same display than its parent
  // so we return the parent's display if there is a parent.
  if (entered_outputs_ids.empty()) {
    if (parent_window)
      return GetDisplayForAcceleratedWidget(parent_window->GetWidget());
    return GetPrimaryDisplay();
  }

  DCHECK(!display_list_.displays().empty());

  // A widget can be located on two or more displays. It would be better if the
  // most in DIP occupied display was returned, but it's impossible to do so in
  // Wayland. Thus, return the one that was used the earliest.
  for (const auto& display : display_list_.displays()) {
    if (display.id() == *entered_outputs_ids.begin())
      return display;
  }

  NOTREACHED();
  return GetPrimaryDisplay();
}

gfx::Point WaylandScreen::GetCursorScreenPoint() const {
  // Wayland does not provide either location of surfaces in global space
  // coordinate system or location of a pointer. Instead, only locations of
  // mouse/touch events are known. Given that Chromium assumes top-level windows
  // are located at origin, always provide a cursor point in regards to
  // surfaces' location.
  //
  // If a pointer is located in any of the existing wayland windows, return the
  // last known cursor position. Otherwise, return such a point, which is not
  // contained by any of the windows.
  auto* cursor_position = connection_->wayland_cursor_position();
  if (connection_->wayland_window_manager()->GetCurrentFocusedWindow() &&
      cursor_position)
    return cursor_position->GetCursorSurfacePoint();

  auto* window =
      connection_->wayland_window_manager()->GetWindowWithLargestBounds();
  DCHECK(window);
  const gfx::Rect bounds = window->GetBounds();
  return gfx::Point(bounds.width() + 10, bounds.height() + 10);
}

gfx::AcceleratedWidget WaylandScreen::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) const {
  // It is safe to check only for focused windows and test if they contain the
  // point or not.
  auto* window =
      connection_->wayland_window_manager()->GetCurrentFocusedWindow();
  if (window && window->GetBounds().Contains(point))
    return window->GetWidget();
  return gfx::kNullAcceleratedWidget;
}

gfx::AcceleratedWidget WaylandScreen::GetLocalProcessWidgetAtPoint(
    const gfx::Point& point,
    const std::set<gfx::AcceleratedWidget>& ignore) const {
  auto widget = GetAcceleratedWidgetAtScreenPoint(point);
  return !widget || base::Contains(ignore, widget) ? gfx::kNullAcceleratedWidget
                                                   : widget;
}

display::Display WaylandScreen::GetDisplayNearestPoint(
    const gfx::Point& point) const {
  return *FindDisplayNearestPoint(display_list_.displays(), point);
}

display::Display WaylandScreen::GetDisplayMatching(
    const gfx::Rect& match_rect) const {
  if (match_rect.IsEmpty())
    return GetDisplayNearestPoint(match_rect.origin());

  const display::Display* display_matching =
      display::FindDisplayWithBiggestIntersection(display_list_.displays(),
                                                  match_rect);
  return display_matching ? *display_matching : GetPrimaryDisplay();
}

void WaylandScreen::AddObserver(display::DisplayObserver* observer) {
  display_list_.AddObserver(observer);
}

void WaylandScreen::RemoveObserver(display::DisplayObserver* observer) {
  display_list_.RemoveObserver(observer);
}

}  // namespace ui
