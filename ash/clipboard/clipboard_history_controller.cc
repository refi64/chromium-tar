// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/clipboard/clipboard_history_controller.h"

#include "ash/accelerators/accelerator_controller_impl.h"
#include "ash/clipboard/clipboard_history.h"
#include "ash/clipboard/clipboard_history_helper.h"
#include "ash/clipboard/clipboard_history_menu_model_adapter.h"
#include "ash/public/cpp/window_tree_host_lookup.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/shell.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/clipboard/clipboard_constants.h"
#include "ui/base/clipboard/clipboard_data.h"
#include "ui/base/clipboard/clipboard_non_backed.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/models/image_model.h"
#include "ui/base/models/menu_separator_types.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes_posix.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/strings/grit/ui_strings.h"

namespace ash {

namespace {

// TODO(dmblack): Move to clipboard_history_helper.
ui::ImageModel GetImageModelForClipboardData(const ui::ClipboardData& item) {
  if (item.format() & static_cast<int>(ui::ClipboardInternalFormat::kBitmap)) {
    // TODO(newcomer): Show a smaller version of the bitmap.
    return ui::ImageModel();
  }
  if (item.format() & static_cast<int>(ui::ClipboardInternalFormat::kWeb))
    return ui::ImageModel::FromVectorIcon(ash::kWebSmartPasteIcon);
  if (item.format() &
      static_cast<int>(ui::ClipboardInternalFormat::kBookmark)) {
    return ui::ImageModel::FromVectorIcon(ash::kWebBookmarkIcon);
  }
  if (item.format() & static_cast<int>(ui::ClipboardInternalFormat::kHtml))
    return ui::ImageModel::FromVectorIcon(ash::kHtmlIcon);
  if (item.format() & static_cast<int>(ui::ClipboardInternalFormat::kRtf))
    return ui::ImageModel::FromVectorIcon(ash::kRtfIcon);
  if (item.format() & static_cast<int>(ui::ClipboardInternalFormat::kText))
    return ui::ImageModel::FromVectorIcon(ash::kTextIcon);
  if (item.format() & static_cast<int>(ui::ClipboardInternalFormat::kCustom)) {
    // TODO(crbug/1108901): Handle file manager case.
    // TODO(crbug/1108902): Handle fallback case.
    return ui::ImageModel();
  }
  return ui::ImageModel();
}

ui::ClipboardNonBacked* GetClipboard() {
  auto* clipboard = ui::ClipboardNonBacked::GetForCurrentThread();
  DCHECK(clipboard);
  return clipboard;
}

}  // namespace

// ClipboardHistoryController::AcceleratorTarget -------------------------------

class ClipboardHistoryController::AcceleratorTarget
    : public ui::AcceleratorTarget {
 public:
  explicit AcceleratorTarget(ClipboardHistoryController* controller)
      : controller_(controller) {}
  AcceleratorTarget(const AcceleratorTarget&) = delete;
  AcceleratorTarget& operator=(const AcceleratorTarget&) = delete;
  ~AcceleratorTarget() override = default;

  void Init() {
    ui::Accelerator show_menu_combo(ui::VKEY_V, ui::EF_COMMAND_DOWN);
    show_menu_combo.set_key_state(ui::Accelerator::KeyState::PRESSED);
    // Register, but no need to unregister because this outlives
    // AcceleratorController.
    Shell::Get()->accelerator_controller()->Register(
        {show_menu_combo}, /*accelerator_target=*/this);
  }

 private:
  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    if (controller_->IsMenuShowing())
      controller_->ExecuteSelectedMenuItem(accelerator.modifiers());
    else
      controller_->ShowMenu();
    return true;
  }

  bool CanHandleAccelerators() const override {
    return controller_->IsMenuShowing() || controller_->CanShowMenu();
  }

  // The controller responsible for showing the Clipboard History menu.
  ClipboardHistoryController* const controller_;
};

// ClipboardHistoryController::MenuDelegate ------------------------------------

class ClipboardHistoryController::MenuDelegate
    : public ui::SimpleMenuModel::Delegate {
 public:
  explicit MenuDelegate(ClipboardHistoryController* controller)
      : controller_(controller) {}
  MenuDelegate(const MenuDelegate&) = delete;
  MenuDelegate& operator=(const MenuDelegate&) = delete;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    controller_->MenuOptionSelected(/*index=*/command_id, event_flags);
  }

 private:
  // The controller responsible for showing the Clipboard History menu.
  ClipboardHistoryController* const controller_;
};

// ClipboardHistoryController --------------------------------------------------

ClipboardHistoryController::ClipboardHistoryController()
    : clipboard_history_(std::make_unique<ClipboardHistory>()),
      accelerator_target_(std::make_unique<AcceleratorTarget>(this)),
      menu_delegate_(std::make_unique<MenuDelegate>(this)) {}

ClipboardHistoryController::~ClipboardHistoryController() = default;

void ClipboardHistoryController::Init() {
  accelerator_target_->Init();
}

bool ClipboardHistoryController::IsMenuShowing() const {
  return context_menu_ && context_menu_->IsRunning();
}

gfx::Rect ClipboardHistoryController::GetMenuBoundsInScreenForTest() const {
  return context_menu_->GetMenuBoundsInScreenForTest();
}

bool ClipboardHistoryController::CanShowMenu() const {
  return !clipboard_history_->IsEmpty();
}

void ClipboardHistoryController::ExecuteSelectedMenuItem(int event_flags) {
  DCHECK(IsMenuShowing());
  auto command = context_menu_->GetSelectedMenuItemCommand();

  // Force close the context menu. Failure to do so before dispatching our
  // synthetic key event will result in the context menu consuming the event.
  context_menu_->Cancel();

  // If no menu item is currently selected, we'll fallback to the first item.
  menu_delegate_->ExecuteCommand(command.value_or(0), event_flags);
}

void ClipboardHistoryController::ShowMenu() {
  if (IsMenuShowing() || !CanShowMenu())
    return;

  clipboard_items_ =
      std::vector<ui::ClipboardData>(clipboard_history_->GetItems().begin(),
                                     clipboard_history_->GetItems().end());

  std::unique_ptr<ui::SimpleMenuModel> menu_model =
      std::make_unique<ui::SimpleMenuModel>(menu_delegate_.get());
  menu_model->AddTitle(
      ui::ResourceBundle::GetSharedInstance().GetLocalizedString(
          IDS_CLIPBOARD_MENU_CLIPBOARD));
  int index = 0;
  for (const auto& item : clipboard_items_) {
    menu_model->AddItemWithIcon(index++, clipboard::helper::GetLabel(item),
                                GetImageModelForClipboardData(item));
  }
  menu_model->AddSeparator(ui::MenuSeparatorType::NORMAL_SEPARATOR);
  menu_model->AddItemWithIcon(
      index++,
      ui::ResourceBundle::GetSharedInstance().GetLocalizedString(
          IDS_CLIPBOARD_MENU_DELETE_ALL),
      ui::ImageModel::FromVectorIcon(ash::kDeleteIcon));

  context_menu_ =
      std::make_unique<ClipboardHistoryMenuModelAdapter>(std::move(menu_model));
  context_menu_->Run(CalculateAnchorRect());
}

void ClipboardHistoryController::MenuOptionSelected(int index,
                                                    int event_flags) {
  auto it = clipboard_items_.begin();
  std::advance(it, index);

  if (it == clipboard_items_.end()) {
    // The last option in the menu is used to delete history.
    clipboard_history_->Clear();
    return;
  }

  auto* clipboard = GetClipboard();
  std::unique_ptr<ui::ClipboardData> original_data;

  // If necessary, replace the clipboard's |original_data| temporarily so that
  // we can paste the selected history item.
  const bool shift_key_pressed = event_flags & ui::EF_SHIFT_DOWN;
  if (shift_key_pressed || *it != *clipboard->GetClipboardData()) {
    std::unique_ptr<ui::ClipboardData> temp_data;
    if (shift_key_pressed) {
      // When the shift key is pressed, we only paste plain text.
      temp_data = std::make_unique<ui::ClipboardData>();
      temp_data->set_text(it->text());
    } else {
      temp_data = std::make_unique<ui::ClipboardData>(*it);
    }
    // Pause clipboard history when manipulating the clipboard for a paste.
    ClipboardHistory::ScopedPause scoped_pause(clipboard_history_.get());
    original_data = clipboard->WriteClipboardData(std::move(temp_data));
  }

  ui::KeyEvent synthetic_key_event(ui::ET_KEY_PRESSED, ui::VKEY_V,
                                   static_cast<ui::DomCode>(0),
                                   ui::EF_CONTROL_DOWN);
  auto* host = ash::GetWindowTreeHostForDisplay(
      display::Screen::GetScreen()->GetPrimaryDisplay().id());
  DCHECK(host);
  host->DeliverEventToSink(&synthetic_key_event);

  if (!original_data)
    return;

  // Replace the original item back on top of the clipboard. Some apps take a
  // long time to receive the paste event, also some apps will read from the
  // clipboard multiple times per paste. Wait a bit before replacing the item
  // back onto the clipboard.
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](const base::WeakPtr<ClipboardHistoryController>& weak_ptr,
             std::unique_ptr<ui::ClipboardData> original_data) {
            // When restoring the original item back on top of the clipboard we
            // need to pause clipboard history. Failure to do so will result in
            // the original item being re-recorded when this restoration step
            // should actually be opaque to the user.
            std::unique_ptr<ClipboardHistory::ScopedPause> scoped_pause;
            if (weak_ptr) {
              scoped_pause = std::make_unique<ClipboardHistory::ScopedPause>(
                  weak_ptr->clipboard_history_.get());
            }
            GetClipboard()->WriteClipboardData(std::move(original_data));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(original_data)),
      base::TimeDelta::FromMilliseconds(200));
}

gfx::Rect ClipboardHistoryController::CalculateAnchorRect() const {
  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  auto* host = ash::GetWindowTreeHostForDisplay(display.id());

  // Some web apps render the caret in an IFrame, and we will not get the
  // bounds in that case.
  // TODO(https://crbug.com/1099930): Show the menu in the middle of the
  // webview if the bounds are empty.
  ui::TextInputClient* text_input_client =
      host->GetInputMethod()->GetTextInputClient();

  // |text_input_client| may be null. For example, in clamshell mode and without
  // any window open.
  const gfx::Rect textfield_bounds =
      text_input_client ? text_input_client->GetCaretBounds() : gfx::Rect();

  // Note that the width of caret's bounds may be zero in some views (such as
  // the search bar of Google search web page). So we cannot use
  // gfx::Size::IsEmpty() here. In addition, the applications using IFrame may
  // provide unreliable |textfield_bounds| which are not fully contained by the
  // display bounds.
  // TODO(https://crbug.com/1110027).
  const bool textfield_bounds_are_valid =
      textfield_bounds.size() != gfx::Size() &&
      display.bounds().Contains(textfield_bounds);

  if (textfield_bounds_are_valid)
    return textfield_bounds;

  return gfx::Rect(display::Screen::GetScreen()->GetCursorScreenPoint(),
                   gfx::Size());
}

}  // namespace ash
