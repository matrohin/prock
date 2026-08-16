#include "app.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "ui/ui_test_replay.h"
#include "views/brief_table_logic.h"

constexpr const char *TREE_CHECKBOX = "//ProcessTable/Header/Tree";
constexpr const char *FILTER_INPUT = "//ProcessTable/Header/##ProcessFilter";
constexpr const char *TABLE = "//ProcessTable/Processes";
constexpr const char *FIRST_PROCESS = "//ProcessTable/Processes/1";
constexpr const char *PID_COLUMN = "//ProcessTable/Processes/$$0/PID";
constexpr const char *RSS_COLUMN = "//ProcessTable/Processes/$$9/RSS";

// Rows are identified by PID, so these spell out the scenario's PIDs.
constexpr const char *BORN_ROW = "//ProcessTable/Processes/600";
constexpr const char *DYING_ROW = "//ProcessTable/Processes/400";
constexpr const char *SHELL_ROW = "//ProcessTable/Processes/300";
constexpr const char *BROWSER_ROW = "//ProcessTable/Processes/500";
static_assert(UI_TEST_PID_ROOT == 1);
static_assert(UI_TEST_PID_BORN == 600);
static_assert(UI_TEST_PID_DYING == 400);
static_assert(UI_TEST_PID_SHELL == 300);
static_assert(UI_TEST_PID_BROWSER == 500);

#define CHECK_ITEM_INFO_HAS(info, expect_true)                                 \
  IM_CHECK(((info).StatusFlags & (expect_true)) == (expect_true))

#define CHECK_ITEM_INFO_NOT_HAVE(info, expect_false)                           \
  IM_CHECK_EQ(((info).StatusFlags & (expect_false)), 0)

static void select_row(ImGuiTestContext *context, const char *row_id) {
  context->ItemClick(row_id, ImGuiMouseButton_Left,
                     ImGuiTestOpFlags_MoveToEdgeL);
}

static void set_filter(ImGuiTestContext *context, const char *text) {
  context->ItemInputValue(FILTER_INPUT, text);
  // Let the rows that just went away age out of the item cache.
  context->Yield(UI_TEST_SETTLE_FRAMES);
}

static void set_tree_mode(ImGuiTestContext *context, const bool enabled) {
  if (enabled) {
    context->ItemCheck(TREE_CHECKBOX);
  } else {
    context->ItemUncheck(TREE_CHECKBOX);
  }
  context->Yield(UI_TEST_SETTLE_FRAMES);
}

static void check_col_sorted(ImGuiTestContext *context,
                             BriefTableColumnId col_id,
                             ImGuiSortDirection direction) {
  const ImGuiTable *table = ImGui::TableFindByID(context->GetID(TABLE));
  IM_CHECK(table);
  const ImGuiTableColumn &col = table->Columns[col_id];
  IM_CHECK_EQ(col.SortOrder, 0);
  IM_CHECK_EQ(col.SortDirection, direction);
}

void ui_tests_brief_table_register(ImGuiTestEngine *engine) {
  ImGuiTest *t = nullptr;

  t = IM_REGISTER_TEST(
      engine, "brief_table",
      "Tree checkbox switches the table mode with persisted state");
  t->TestFunc = [](ImGuiTestContext *context) {
    ImGuiTestItemInfo info;

    // Tree Mode; Opened sub-tree
    IM_CHECK(context->ItemIsChecked(TREE_CHECKBOX));
    info = context->ItemInfo(FIRST_PROCESS);
    CHECK_ITEM_INFO_HAS(info, ImGuiItemStatusFlags_Openable);
    CHECK_ITEM_INFO_HAS(info, ImGuiItemStatusFlags_Opened);

    // Tree Mode; Closed sub-tree
    select_row(context, FIRST_PROCESS);
    context->KeyPress(ImGuiKey_LeftArrow);
    info = context->ItemInfo(FIRST_PROCESS);
    CHECK_ITEM_INFO_HAS(info, ImGuiItemStatusFlags_Openable);
    CHECK_ITEM_INFO_NOT_HAVE(info, ImGuiItemStatusFlags_Opened);

    // List Mode
    context->ItemClick(TREE_CHECKBOX);
    IM_CHECK(!context->ItemIsChecked(TREE_CHECKBOX));
    info = context->ItemInfo(FIRST_PROCESS);
    CHECK_ITEM_INFO_NOT_HAVE(info, ImGuiItemStatusFlags_Openable);
    CHECK_ITEM_INFO_NOT_HAVE(info, ImGuiItemStatusFlags_Opened);

    // Tree Mode; Closed sub-tree
    context->ItemClick(TREE_CHECKBOX);
    info = context->ItemInfo(FIRST_PROCESS);
    CHECK_ITEM_INFO_HAS(info, ImGuiItemStatusFlags_Openable);
    CHECK_ITEM_INFO_NOT_HAVE(info, ImGuiItemStatusFlags_Opened);

    // Tree Mode; Opened sub-tree
    select_row(context, FIRST_PROCESS);
    context->KeyPress(ImGuiKey_RightArrow);
    info = context->ItemInfo(FIRST_PROCESS);
    CHECK_ITEM_INFO_HAS(info, ImGuiItemStatusFlags_Openable);
    CHECK_ITEM_INFO_HAS(info, ImGuiItemStatusFlags_Opened);
  };

  t = IM_REGISTER_TEST(engine, "brief_table", "Sorting resets the tree mode");
  t->TestFunc = [](ImGuiTestContext *context) {
    ImGuiTestItemInfo info;

    IM_CHECK(context->ItemIsChecked(TREE_CHECKBOX));
    check_col_sorted(context, eBriefTableColumnId_Pid,
                     ImGuiSortDirection_Ascending);

    // Click on PID -> List mode; PID desc sort
    context->ItemClick(PID_COLUMN);
    check_col_sorted(context, eBriefTableColumnId_Pid,
                     ImGuiSortDirection_Descending);
    IM_CHECK(!context->ItemIsChecked(TREE_CHECKBOX));

    // Back to original state
    context->ItemClick(TREE_CHECKBOX);
    IM_CHECK(context->ItemIsChecked(TREE_CHECKBOX));
    check_col_sorted(context, eBriefTableColumnId_Pid,
                     ImGuiSortDirection_Ascending);

    // Click on RSS -> List mode; RSS desc sort
    context->ItemClick(RSS_COLUMN);
    check_col_sorted(context, eBriefTableColumnId_MemRssBytes,
                     ImGuiSortDirection_Descending);
    IM_CHECK(!context->ItemIsChecked(TREE_CHECKBOX));

    // Back to original state
    context->ItemClick(TREE_CHECKBOX);
    IM_CHECK(context->ItemIsChecked(TREE_CHECKBOX));
    check_col_sorted(context, eBriefTableColumnId_Pid,
                     ImGuiSortDirection_Ascending);
  };

  t = IM_REGISTER_TEST(engine, "brief_table",
                       "Stepping the replay adds and removes rows");
  t->TestFunc = [](ImGuiTestContext *context) {
    ui_test_replay_seek(context, UI_TEST_RECORD_LIFECYCLE - 1);
    IM_CHECK(context->ItemExists(DYING_ROW));
    IM_CHECK(!context->ItemExists(BORN_ROW));

    // The record that starts one process and ends another. The dead row is
    // kept on screen for a while, so both are visible here.
    ui_test_replay_step(context);
    IM_CHECK(context->ItemExists(BORN_ROW));
    IM_CHECK(context->ItemExists(DYING_ROW));

    // Far enough past the death for the row to be dropped.
    ui_test_replay_seek(context, UI_TEST_RECORD_SETTLED);
    IM_CHECK(context->ItemExists(BORN_ROW));
    IM_CHECK(!context->ItemExists(DYING_ROW));
  };

  t = IM_REGISTER_TEST(engine, "brief_table",
                       "Stepping a paused replay still updates the table");
  t->TestFunc = [](ImGuiTestContext *context) {
    ui_test_replay_seek(context, UI_TEST_RECORD_LIFECYCLE - 1);

    // A paused replay leaves auto-follow off, which is what freezes the table
    // for live gathering.
    IM_CHECK(!g_ui_test_app->view_state.preferences_state.auto_follow);
    IM_CHECK(!context->ItemExists(BORN_ROW));

    ui_test_replay_step(context);
    IM_CHECK(context->ItemExists(BORN_ROW));
  };

  t = IM_REGISTER_TEST(engine, "brief_table",
                       "Filtering keeps matches, ancestors and '+' subtrees");
  t->TestFunc = [](ImGuiTestContext *context) {
    // The first record is the one where UI_TEST_PID_DYING is still alive.
    ui_test_replay_seek(context, 0);
    IM_CHECK(context->ItemIsChecked(TREE_CHECKBOX));

    // Name match in tree mode: the match and its ancestors stay, the rest is
    // hidden.
    set_filter(context, UI_TEST_NAME_DYING);
    IM_CHECK(context->ItemExists(DYING_ROW));
    IM_CHECK(context->ItemExists(SHELL_ROW));
    IM_CHECK(context->ItemExists(FIRST_PROCESS));
    IM_CHECK(!context->ItemExists(BROWSER_ROW));

    // List mode has no tree to keep intact, so the ancestors go too.
    set_tree_mode(context, false);
    IM_CHECK(context->ItemExists(DYING_ROW));
    IM_CHECK(!context->ItemExists(SHELL_ROW));
    IM_CHECK(!context->ItemExists(FIRST_PROCESS));

    // PID match, without the children of the matched process.
    set_filter(context, "300");
    IM_CHECK(context->ItemExists(SHELL_ROW));
    IM_CHECK(!context->ItemExists(DYING_ROW));
    IM_CHECK(!context->ItemExists(BROWSER_ROW));

    // Same match prefixed with '+' also keeps the subtree below it, which is
    // what list mode does not drop.
    set_filter(context, "+300");
    IM_CHECK(context->ItemExists(SHELL_ROW));
    IM_CHECK(context->ItemExists(DYING_ROW));
    IM_CHECK(!context->ItemExists(FIRST_PROCESS));
    IM_CHECK(!context->ItemExists(BROWSER_ROW));

    // The same subtree, now with the ancestors back on top of it.
    set_tree_mode(context, true);
    IM_CHECK(context->ItemExists(SHELL_ROW));
    IM_CHECK(context->ItemExists(DYING_ROW));
    IM_CHECK(context->ItemExists(FIRST_PROCESS));
    IM_CHECK(!context->ItemExists(BROWSER_ROW));

    // Back to the original state
    set_filter(context, "");
    IM_CHECK(context->ItemExists(DYING_ROW));
    IM_CHECK(context->ItemExists(BROWSER_ROW));
  };
}
