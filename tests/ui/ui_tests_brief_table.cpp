#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "views/brief_table_logic.h"

constexpr const char *TREE_CHECKBOX = "//ProcessTable/Header/Tree";
constexpr const char *TABLE = "//ProcessTable/Processes";
constexpr const char *FIRST_PROCESS = "//ProcessTable/Processes/1";
constexpr const char *PID_COLUMN = "//ProcessTable/Processes/$$0/PID";
constexpr const char *RSS_COLUMN = "//ProcessTable/Processes/$$9/RSS";

#define CHECK_ITEM_INFO_HAS(info, expect_true)                                 \
  IM_CHECK(((info).StatusFlags & (expect_true)) == (expect_true))

#define CHECK_ITEM_INFO_NOT_HAVE(info, expect_false)                           \
  IM_CHECK_EQ(((info).StatusFlags & (expect_false)), 0)

static void select_row(ImGuiTestContext *context, const char *row_id) {
  context->ItemClick(row_id, ImGuiMouseButton_Left,
                     ImGuiTestOpFlags_MoveToEdgeL);
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
}
