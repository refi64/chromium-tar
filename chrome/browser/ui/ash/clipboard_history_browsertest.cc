// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>
#include <memory>

#include "ash/clipboard/clipboard_history.h"
#include "ash/clipboard/clipboard_history_controller.h"
#include "ash/shell.h"
#include "base/test/bind_test_util.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/chromeos/login/login_manager_test.h"
#include "chrome/browser/chromeos/login/test/login_manager_mixin.h"
#include "chrome/browser/chromeos/login/ui/user_adding_screen.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chromeos/constants/chromeos_features.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/controls/menu/menu_config.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/widget/widget.h"

namespace {

std::unique_ptr<views::Widget> CreateTestWidget() {
  auto widget = std::make_unique<views::Widget>();

  views::Widget::InitParams params;
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.type = views::Widget::InitParams::TYPE_WINDOW_FRAMELESS;
  widget->Init(std::move(params));

  return widget;
}

void FlushMessageLoop() {
  base::RunLoop run_loop;
  base::SequencedTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                   run_loop.QuitClosure());
  run_loop.Run();
}

void SetClipboardText(const std::string& text) {
  ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
      .WriteText(base::UTF8ToUTF16(text));

  // ClipboardHistory will post a task to process clipboard data in order to
  // debounce multiple clipboard writes occurring in sequence. Here we give
  // ClipboardHistory the chance to run its posted tasks before proceeding.
  FlushMessageLoop();
}

void SetClipboardTextAndHtml(const std::string& text, const std::string& html) {
  {
    ui::ScopedClipboardWriter scw(ui::ClipboardBuffer::kCopyPaste);
    scw.WriteText(base::UTF8ToUTF16(text));
    scw.WriteHTML(base::UTF8ToUTF16(html), /*source_url=*/"");
  }

  // ClipboardHistory will post a task to process clipboard data in order to
  // debounce multiple clipboard writes occurring in sequence. Here we give
  // ClipboardHistory the chance to run its posted tasks before proceeding.
  FlushMessageLoop();
}

ash::ClipboardHistoryController* GetClipboardHistoryController() {
  return ash::Shell::Get()->clipboard_history_controller();
}

const std::list<ui::ClipboardData>& GetClipboardData() {
  return GetClipboardHistoryController()->history()->GetItems();
}

gfx::Rect GetClipboardHistoryMenuBoundsInScreen() {
  return GetClipboardHistoryController()->GetMenuBoundsInScreenForTest();
}

}  // namespace

// Verify clipboard history's features in the multiprofile environment.
class ClipboardHistoryWithMultiProfileBrowserTest
    : public chromeos::LoginManagerTest {
 public:
  ClipboardHistoryWithMultiProfileBrowserTest() : LoginManagerTest() {
    login_mixin_.AppendRegularUsers(2);
    account_id1_ = login_mixin_.users()[0].account_id;
    account_id2_ = login_mixin_.users()[1].account_id;

    feature_list_.InitAndEnableFeature(chromeos::features::kClipboardHistory);
  }

  ~ClipboardHistoryWithMultiProfileBrowserTest() override = default;

  ui::test::EventGenerator* GetEventGenerator() {
    return event_generator_.get();
  }

 protected:
  void Press(ui::KeyboardCode key, int modifiers = ui::EF_NONE) {
    event_generator_->PressKey(key, modifiers);
  }

  void Release(ui::KeyboardCode key, int modifiers = ui::EF_NONE) {
    event_generator_->ReleaseKey(key, modifiers);
  }

  void PressAndRelease(ui::KeyboardCode key, int modifiers = ui::EF_NONE) {
    Press(key, modifiers);
    Release(key, modifiers);
  }

  void ShowContextMenuViaAccelerator() {
    PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  }

  // chromeos::LoginManagerTest:
  void SetUpOnMainThread() override {
    chromeos::LoginManagerTest::SetUpOnMainThread();
    event_generator_ = std::make_unique<ui::test::EventGenerator>(
        ash::Shell::GetPrimaryRootWindow());
  }

  AccountId account_id1_;
  AccountId account_id2_;
  chromeos::LoginManagerMixin login_mixin_{&mixin_host_};

  std::unique_ptr<ui::test::EventGenerator> event_generator_;

  base::test::ScopedFeatureList feature_list_;
};

// Verify that the clipboard data history is recorded as expected in the
// Multiuser environment.
IN_PROC_BROWSER_TEST_F(ClipboardHistoryWithMultiProfileBrowserTest,
                       VerifyClipboardHistoryAcrossMultiUser) {
  LoginUser(account_id1_);
  EXPECT_TRUE(GetClipboardData().empty());

  // Store text when the user1 is active.
  const std::string copypaste_data1("user1_text1");
  SetClipboardText(copypaste_data1);

  {
    const std::list<ui::ClipboardData>& data = GetClipboardData();
    EXPECT_EQ(1u, data.size());
    EXPECT_EQ(copypaste_data1, data.front().text());
  }

  // Log in as the user2. The clipboard history should be non-empty.
  chromeos::UserAddingScreen::Get()->Start();
  AddUser(account_id2_);
  EXPECT_FALSE(GetClipboardData().empty());

  // Store text when the user2 is active.
  const std::string copypaste_data2("user2_text1");
  SetClipboardText(copypaste_data2);

  {
    const std::list<ui::ClipboardData>& data = GetClipboardData();
    EXPECT_EQ(2u, data.size());
    EXPECT_EQ(copypaste_data2, data.front().text());
  }

  // Switch to the user1.
  user_manager::UserManager::Get()->SwitchActiveUser(account_id1_);

  // Store text when the user1 is active.
  const std::string copypaste_data3("user1_text2");
  SetClipboardText(copypaste_data3);

  {
    const std::list<ui::ClipboardData>& data = GetClipboardData();
    EXPECT_EQ(3u, data.size());

    // Note that items in |data| follow the time ordering. The most recent item
    // is always the first one.
    auto it = data.begin();
    EXPECT_EQ(copypaste_data3, it->text());

    std::advance(it, 1u);
    EXPECT_EQ(copypaste_data2, it->text());

    std::advance(it, 1u);
    EXPECT_EQ(copypaste_data1, it->text());
  }
}

// Verifies that the history menu is anchored at the cursor's location when
// not having any textfield.
IN_PROC_BROWSER_TEST_F(ClipboardHistoryWithMultiProfileBrowserTest,
                       ShowHistoryMenuWhenNoTextfieldExists) {
  LoginUser(account_id1_);

  // Close the browser window to ensure that textfield does not exist.
  CloseAllBrowsers();

  // No clipboard data. So the clipboard history menu should not show.
  ASSERT_TRUE(GetClipboardData().empty());
  ShowContextMenuViaAccelerator();
  EXPECT_FALSE(GetClipboardHistoryController()->IsMenuShowing());

  SetClipboardText("test");

  const gfx::Point mouse_location =
      ash::Shell::Get()->GetPrimaryRootWindow()->bounds().CenterPoint();
  GetEventGenerator()->MoveMouseTo(mouse_location);
  ShowContextMenuViaAccelerator();

  // Verifies that the menu is anchored at the cursor's location.
  ASSERT_TRUE(GetClipboardHistoryController()->IsMenuShowing());
  const gfx::Point menu_origin =
      GetClipboardHistoryMenuBoundsInScreen().origin();
  EXPECT_EQ(mouse_location.x() +
                views::MenuConfig::instance().touchable_anchor_offset,
            menu_origin.x());
  EXPECT_EQ(mouse_location.y(), menu_origin.y());
}

IN_PROC_BROWSER_TEST_F(ClipboardHistoryWithMultiProfileBrowserTest,
                       ShouldPasteHistoryViaKeyboard) {
  LoginUser(account_id1_);
  CloseAllBrowsers();

  // Create a widget containing a single, focusable textfield.
  auto widget = CreateTestWidget();
  auto* textfield =
      widget->SetContentsView(std::make_unique<views::Textfield>());
  textfield->SetAccessibleName(base::UTF8ToUTF16("Textfield"));
  textfield->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);

  // Show the widget.
  widget->SetBounds(gfx::Rect(0, 0, 100, 100));
  widget->Show();
  EXPECT_TRUE(widget->IsActive());

  // Focus the textfield and confirm initial state.
  textfield->RequestFocus();
  EXPECT_TRUE(textfield->HasFocus());
  EXPECT_TRUE(textfield->GetText().empty());

  // Write some things to the clipboard.
  SetClipboardText("A");
  SetClipboardText("B");
  SetClipboardText("C");

  // Verify we can paste the first history item via the ENTER key.
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_TRUE(GetClipboardHistoryController()->IsMenuShowing());
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_RETURN);
  EXPECT_FALSE(GetClipboardHistoryController()->IsMenuShowing());
  EXPECT_EQ("C", base::UTF16ToUTF8(textfield->GetText()));

  textfield->SetText(base::string16());
  EXPECT_TRUE(textfield->GetText().empty());

  // Verify we can paste the first history item via the COMMAND+V shortcut.
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_TRUE(GetClipboardHistoryController()->IsMenuShowing());
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_FALSE(GetClipboardHistoryController()->IsMenuShowing());
  EXPECT_EQ("C", base::UTF16ToUTF8(textfield->GetText()));

  textfield->SetText(base::string16());
  EXPECT_TRUE(textfield->GetText().empty());

  // Verify we can paste the last history item via the ENTER key.
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_TRUE(GetClipboardHistoryController()->IsMenuShowing());
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_RETURN);
  EXPECT_FALSE(GetClipboardHistoryController()->IsMenuShowing());
  EXPECT_EQ("A", base::UTF16ToUTF8(textfield->GetText()));

  textfield->SetText(base::string16());
  EXPECT_TRUE(textfield->GetText().empty());

  // Verify we can paste the last history item via the COMMAND+V shortcut.
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_TRUE(GetClipboardHistoryController()->IsMenuShowing());
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_FALSE(GetClipboardHistoryController()->IsMenuShowing());
  EXPECT_EQ("A", base::UTF16ToUTF8(textfield->GetText()));
}

IN_PROC_BROWSER_TEST_F(ClipboardHistoryWithMultiProfileBrowserTest,
                       ShouldPasteHistoryWhileHoldingDownCommandKey) {
  LoginUser(account_id1_);
  CloseAllBrowsers();

  // Create a widget containing a single, focusable textfield.
  auto widget = CreateTestWidget();
  auto* textfield =
      widget->SetContentsView(std::make_unique<views::Textfield>());
  textfield->SetAccessibleName(base::UTF8ToUTF16("Textfield"));
  textfield->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);

  // Show the widget.
  widget->SetBounds(gfx::Rect(0, 0, 100, 100));
  widget->Show();
  EXPECT_TRUE(widget->IsActive());

  // Focus the textfield and confirm initial state.
  textfield->RequestFocus();
  EXPECT_TRUE(textfield->HasFocus());
  EXPECT_TRUE(textfield->GetText().empty());

  // Write some things to the clipboard.
  SetClipboardText("A");
  SetClipboardText("B");
  SetClipboardText("C");

  // Verify we can traverse clipboard history and paste the first history item
  // while holding down the COMMAND key.
  Press(ui::KeyboardCode::VKEY_COMMAND);
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_TRUE(GetClipboardHistoryController()->IsMenuShowing());
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN, ui::EF_COMMAND_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_FALSE(GetClipboardHistoryController()->IsMenuShowing());
  EXPECT_EQ("C", base::UTF16ToUTF8(textfield->GetText()));
  Release(ui::KeyboardCode::VKEY_COMMAND);

  textfield->SetText(base::string16());
  EXPECT_TRUE(textfield->GetText().empty());

  // Verify we can traverse clipboard history and paste the last history item
  // while holding down the COMMAND key.
  Press(ui::KeyboardCode::VKEY_COMMAND);
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_TRUE(GetClipboardHistoryController()->IsMenuShowing());
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN, ui::EF_COMMAND_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN, ui::EF_COMMAND_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN, ui::EF_COMMAND_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_FALSE(GetClipboardHistoryController()->IsMenuShowing());
  EXPECT_EQ("A", base::UTF16ToUTF8(textfield->GetText()));
  Release(ui::KeyboardCode::VKEY_COMMAND);
}

IN_PROC_BROWSER_TEST_F(ClipboardHistoryWithMultiProfileBrowserTest,
                       ShouldPasteHistoryAsPlainText) {
  LoginUser(account_id1_);

  // Create a browser and cache its active web contents.
  auto* browser = CreateBrowser(
      chromeos::ProfileHelper::Get()->GetProfileByAccountId(account_id1_));
  auto* web_contents = browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  // Load the web contents synchronously.
  // The contained script:
  //  - Listens for paste events and caches the last pasted data.
  //  - Notifies observers of paste events by changing document title.
  //  - Provides an API to expose the last pasted data.
  ASSERT_TRUE(content::NavigateToURL(web_contents, GURL(R"(data:text/html,
    <!DOCTYPE html>
    <html>
      <body>
        <script>

          let lastPaste = undefined;
          let lastPasteId = 1;

          window.addEventListener('paste', e => {
            e.stopPropagation();
            e.preventDefault();

            const clipboardData = e.clipboardData || window.clipboardData;
            lastPaste = clipboardData.types.map((type) => {
              return `${type}: ${clipboardData.getData(type)}`;
            });

            document.title = `Paste ${lastPasteId++}`;
          });

          window.getLastPaste = () => {
            return lastPaste || [];
          };

        </script>
      </body>
    </html>
  )")));

  // Cache a function to return the last paste.
  auto GetLastPaste = [&]() {
    auto result = content::EvalJs(
        web_contents, "(function() { return window.getLastPaste(); })();");
    EXPECT_EQ(result.error, "");
    return result.ExtractList();
  };

  // Confirm initial state.
  ASSERT_TRUE(GetLastPaste().GetList().empty());

  // Write some things to the clipboard.
  SetClipboardTextAndHtml("A", "<span>A</span>");
  SetClipboardTextAndHtml("B", "<span>B</span>");
  SetClipboardTextAndHtml("C", "<span>C</span>");

  // Open clipboard history and paste the last history item.
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_TRUE(GetClipboardHistoryController()->IsMenuShowing());
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_RETURN);
  EXPECT_FALSE(GetClipboardHistoryController()->IsMenuShowing());

  // Wait for the paste event to propagate to the web contents.
  // The web contents will notify us a paste occurred by updating page title.
  ignore_result(
      content::TitleWatcher(web_contents, base ::UTF8ToUTF16("Paste 1"))
          .WaitAndGetTitle());

  // Confirm the expected paste data.
  base::ListValue last_paste = GetLastPaste();
  ASSERT_EQ(last_paste.GetList().size(), 2u);
  EXPECT_EQ(last_paste.GetList()[0].GetString(), "text/plain: A");
  EXPECT_EQ(last_paste.GetList()[1].GetString(), "text/html: <span>A</span>");

  // Open clipboard history and paste the middle history item as plain text.
  PressAndRelease(ui::KeyboardCode::VKEY_V, ui::EF_COMMAND_DOWN);
  EXPECT_TRUE(GetClipboardHistoryController()->IsMenuShowing());
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_DOWN);
  PressAndRelease(ui::KeyboardCode::VKEY_RETURN, ui::EF_SHIFT_DOWN);
  EXPECT_FALSE(GetClipboardHistoryController()->IsMenuShowing());

  // Wait for the paste event to propagate to the web contents.
  // The web contents will notify us a paste occurred by updating page title.
  ignore_result(
      content::TitleWatcher(web_contents, base ::UTF8ToUTF16("Paste 2"))
          .WaitAndGetTitle());

  // Confirm the expected paste data.
  last_paste = GetLastPaste();
  ASSERT_EQ(last_paste.GetList().size(), 1u);
  EXPECT_EQ(last_paste.GetList()[0].GetString(), "text/plain: A");
}
