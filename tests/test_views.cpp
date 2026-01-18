#include "doctest.h"

#include "imgui.h"

// ImPlotShadedFlags is used in common_charts.h but we don't need the full implot
using ImPlotShadedFlags = int;

#include "state.h"
#include "views/brief_table.h"
#include "views/common_charts.h"
#include "test_helpers.h"

// ============================================================================
// binary_search_pid Tests
// ============================================================================

TEST_CASE("binary_search_pid") {
  BumpArena arena = BumpArena::create();

  SUBCASE("empty array returns SIZE_MAX") {
    Array<ProcessStat> stats = {};
    stats.size = 0;
    stats.data = nullptr;

    CHECK(binary_search_pid(stats, 1) == SIZE_MAX);
  }

  SUBCASE("single element - found") {
    SnapshotBuilder builder(arena);
    builder.add(100, 0, "test");
    StateSnapshot snapshot = builder.build();

    CHECK(binary_search_pid(snapshot.stats, 100) == 0);
  }

  SUBCASE("single element - not found") {
    SnapshotBuilder builder(arena);
    builder.add(100, 0, "test");
    StateSnapshot snapshot = builder.build();

    CHECK(binary_search_pid(snapshot.stats, 50) == SIZE_MAX);
    CHECK(binary_search_pid(snapshot.stats, 150) == SIZE_MAX);
  }

  SUBCASE("multiple elements - found at various positions") {
    SnapshotBuilder builder(arena);
    builder.add(10, 0, "first");
    builder.add(20, 0, "second");
    builder.add(30, 0, "third");
    builder.add(40, 0, "fourth");
    builder.add(50, 0, "fifth");
    StateSnapshot snapshot = builder.build();

    CHECK(binary_search_pid(snapshot.stats, 10) == 0);
    CHECK(binary_search_pid(snapshot.stats, 30) == 2);
    CHECK(binary_search_pid(snapshot.stats, 50) == 4);
    CHECK(binary_search_pid(snapshot.stats, 20) == 1);
    CHECK(binary_search_pid(snapshot.stats, 40) == 3);
  }

  SUBCASE("multiple elements - not found") {
    SnapshotBuilder builder(arena);
    builder.add(10, 0, "first");
    builder.add(20, 0, "second");
    builder.add(30, 0, "third");
    StateSnapshot snapshot = builder.build();

    CHECK(binary_search_pid(snapshot.stats, 5) == SIZE_MAX);
    CHECK(binary_search_pid(snapshot.stats, 15) == SIZE_MAX);
    CHECK(binary_search_pid(snapshot.stats, 25) == SIZE_MAX);
    CHECK(binary_search_pid(snapshot.stats, 35) == SIZE_MAX);
  }

  arena.destroy();
}

// ============================================================================
// build_process_tree Tests
// ============================================================================

TEST_CASE("build_process_tree") {
  BumpArena arena = BumpArena::create();

  SUBCASE("empty lines returns nullptr") {
    SnapshotBuilder builder(arena);
    StateSnapshot snapshot = builder.build();
    Array<BriefTableLine> lines = {};

    BriefTreeNode *roots = build_process_tree(arena, lines, snapshot,
        eBriefTableColumnId_Pid, ImGuiSortDirection_Ascending);

    CHECK(roots == nullptr);
  }

  SUBCASE("single process becomes single root") {
    SnapshotBuilder builder(arena);
    builder.add(100, 0, "init");
    StateSnapshot snapshot = builder.build();

    Array<BriefTableLine> lines = Array<BriefTableLine>::create(arena, 1);
    lines.data[0] = {100, 0};

    BriefTreeNode *roots = build_process_tree(arena, lines, snapshot,
        eBriefTableColumnId_Pid, ImGuiSortDirection_Ascending);

    REQUIRE(roots != nullptr);
    CHECK(roots->pid == 100);
    CHECK(roots->first_child == nullptr);
    CHECK(roots->next_sibling == nullptr);
  }

  SUBCASE("flat processes (no parent-child) all become roots") {
    SnapshotBuilder builder(arena);
    builder.add(1, 0, "init");
    builder.add(2, 0, "kthreadd");
    builder.add(3, 0, "rcu");
    StateSnapshot snapshot = builder.build();

    Array<BriefTableLine> lines = Array<BriefTableLine>::create(arena, 3);
    lines.data[0] = {1, 0};
    lines.data[1] = {2, 1};
    lines.data[2] = {3, 2};

    BriefTreeNode *roots = build_process_tree(arena, lines, snapshot,
        eBriefTableColumnId_Pid, ImGuiSortDirection_Ascending);

    REQUIRE(roots != nullptr);
    // Count roots
    size_t root_count = 0;
    for (BriefTreeNode *n = roots; n; n = n->next_sibling) root_count++;
    CHECK(root_count == 3);
  }

  SUBCASE("parent-child relationship") {
    SnapshotBuilder builder(arena);
    builder.add(1, 0, "init");      // root
    builder.add(100, 1, "child1");  // child of 1
    builder.add(200, 1, "child2");  // child of 1
    StateSnapshot snapshot = builder.build();

    Array<BriefTableLine> lines = Array<BriefTableLine>::create(arena, 3);
    lines.data[0] = {1, 0};
    lines.data[1] = {100, 1};
    lines.data[2] = {200, 2};

    BriefTreeNode *roots = build_process_tree(arena, lines, snapshot,
        eBriefTableColumnId_Pid, ImGuiSortDirection_Ascending);

    REQUIRE(roots != nullptr);
    CHECK(roots->pid == 1);
    CHECK(roots->next_sibling == nullptr);  // only one root

    // Check children
    REQUIRE(roots->first_child != nullptr);
    // Children should be sorted by PID ascending: 100, 200
    CHECK(roots->first_child->pid == 100);
    CHECK(roots->first_child->next_sibling != nullptr);
    CHECK(roots->first_child->next_sibling->pid == 200);
  }

  SUBCASE("orphan process (parent not in list) becomes root") {
    SnapshotBuilder builder(arena);
    builder.add(100, 999, "orphan");  // parent 999 not in list
    builder.add(200, 999, "orphan2"); // parent 999 not in list
    StateSnapshot snapshot = builder.build();

    Array<BriefTableLine> lines = Array<BriefTableLine>::create(arena, 2);
    lines.data[0] = {100, 0};
    lines.data[1] = {200, 1};

    BriefTreeNode *roots = build_process_tree(arena, lines, snapshot,
        eBriefTableColumnId_Pid, ImGuiSortDirection_Ascending);

    REQUIRE(roots != nullptr);
    // Both should be roots
    size_t root_count = 0;
    for (BriefTreeNode *n = roots; n; n = n->next_sibling) root_count++;
    CHECK(root_count == 2);
  }

  SUBCASE("deep hierarchy") {
    SnapshotBuilder builder(arena);
    builder.add(1, 0, "init");
    builder.add(10, 1, "level1");
    builder.add(100, 10, "level2");
    builder.add(1000, 100, "level3");
    StateSnapshot snapshot = builder.build();

    Array<BriefTableLine> lines = Array<BriefTableLine>::create(arena, 4);
    lines.data[0] = {1, 0};
    lines.data[1] = {10, 1};
    lines.data[2] = {100, 2};
    lines.data[3] = {1000, 3};

    BriefTreeNode *roots = build_process_tree(arena, lines, snapshot,
        eBriefTableColumnId_Pid, ImGuiSortDirection_Ascending);

    REQUIRE(roots != nullptr);
    CHECK(roots->pid == 1);
    REQUIRE(roots->first_child != nullptr);
    CHECK(roots->first_child->pid == 10);
    REQUIRE(roots->first_child->first_child != nullptr);
    CHECK(roots->first_child->first_child->pid == 100);
    REQUIRE(roots->first_child->first_child->first_child != nullptr);
    CHECK(roots->first_child->first_child->first_child->pid == 1000);
  }

  SUBCASE("sorting children by name descending") {
    SnapshotBuilder builder(arena);
    builder.add(1, 0, "init");
    builder.add(100, 1, "aaa");
    builder.add(150, 1, "mmm");
    builder.add(200, 1, "zzz");
    StateSnapshot snapshot = builder.build();
    // After sorting by PID: init@0, aaa@1, mmm@2, zzz@3

    Array<BriefTableLine> lines = Array<BriefTableLine>::create(arena, 4);
    lines.data[0] = {1, 0};    // init at index 0
    lines.data[1] = {100, 1};  // aaa at index 1
    lines.data[2] = {150, 2};  // mmm at index 2
    lines.data[3] = {200, 3};  // zzz at index 3

    BriefTreeNode *roots = build_process_tree(arena, lines, snapshot,
        eBriefTableColumnId_Name, ImGuiSortDirection_Descending);

    REQUIRE(roots != nullptr);
    REQUIRE(roots->first_child != nullptr);
    // Descending by name: zzz, mmm, aaa
    CHECK(roots->first_child->pid == 200);  // zzz
    REQUIRE(roots->first_child->next_sibling != nullptr);
    CHECK(roots->first_child->next_sibling->pid == 150);  // mmm
    REQUIRE(roots->first_child->next_sibling->next_sibling != nullptr);
    CHECK(roots->first_child->next_sibling->next_sibling->pid == 100);  // aaa
  }

  arena.destroy();
}

// ============================================================================
// common_charts_contains_pid Tests
// ============================================================================

struct TestChartData {
  int pid;
  char label[64];
};

TEST_CASE("common_charts_contains_pid") {
  BumpArena arena = BumpArena::create();

  SUBCASE("empty array returns false") {
    GrowingArray<TestChartData> charts = {};
    CHECK_FALSE(common_charts_contains_pid(charts, 100));
  }

  SUBCASE("single element - found") {
    GrowingArray<TestChartData> charts = {};
    size_t wasted = 0;
    charts.emplace_back(arena, wasted)->pid = 100;

    CHECK(common_charts_contains_pid(charts, 100));
  }

  SUBCASE("single element - not found") {
    GrowingArray<TestChartData> charts = {};
    size_t wasted = 0;
    charts.emplace_back(arena, wasted)->pid = 100;

    CHECK_FALSE(common_charts_contains_pid(charts, 50));
    CHECK_FALSE(common_charts_contains_pid(charts, 150));
  }

  SUBCASE("multiple elements - sorted by pid") {
    GrowingArray<TestChartData> charts = {};
    size_t wasted = 0;
    charts.emplace_back(arena, wasted)->pid = 10;
    charts.emplace_back(arena, wasted)->pid = 20;
    charts.emplace_back(arena, wasted)->pid = 30;
    charts.emplace_back(arena, wasted)->pid = 40;
    charts.emplace_back(arena, wasted)->pid = 50;

    CHECK(common_charts_contains_pid(charts, 10));
    CHECK(common_charts_contains_pid(charts, 30));
    CHECK(common_charts_contains_pid(charts, 50));
    CHECK_FALSE(common_charts_contains_pid(charts, 5));
    CHECK_FALSE(common_charts_contains_pid(charts, 25));
    CHECK_FALSE(common_charts_contains_pid(charts, 55));
  }

  arena.destroy();
}

// ============================================================================
// common_charts_sort_added Tests
// ============================================================================

TEST_CASE("common_charts_sort_added") {
  BumpArena arena = BumpArena::create();

  SUBCASE("sorts by pid ascending") {
    GrowingArray<TestChartData> charts = {};
    size_t wasted = 0;
    charts.emplace_back(arena, wasted)->pid = 50;
    charts.emplace_back(arena, wasted)->pid = 10;
    charts.emplace_back(arena, wasted)->pid = 30;
    charts.emplace_back(arena, wasted)->pid = 20;
    charts.emplace_back(arena, wasted)->pid = 40;

    common_charts_sort_added(charts);

    CHECK(charts.data()[0].pid == 10);
    CHECK(charts.data()[1].pid == 20);
    CHECK(charts.data()[2].pid == 30);
    CHECK(charts.data()[3].pid == 40);
    CHECK(charts.data()[4].pid == 50);
  }

  SUBCASE("already sorted stays sorted") {
    GrowingArray<TestChartData> charts = {};
    size_t wasted = 0;
    charts.emplace_back(arena, wasted)->pid = 1;
    charts.emplace_back(arena, wasted)->pid = 2;
    charts.emplace_back(arena, wasted)->pid = 3;

    common_charts_sort_added(charts);

    CHECK(charts.data()[0].pid == 1);
    CHECK(charts.data()[1].pid == 2);
    CHECK(charts.data()[2].pid == 3);
  }

  arena.destroy();
}
