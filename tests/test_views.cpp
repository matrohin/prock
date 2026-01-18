#include "doctest.h"

#include "imgui.h"

// ImPlotShadedFlags is used in common_charts.h but we don't need the full implot
using ImPlotShadedFlags = int;

#include "state.h"
#include "sync.h"
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

// ============================================================================
// brief_table_update Tests
// ============================================================================

TEST_CASE("brief_table_update") {
  BumpArena arena = BumpArena::create();

  SUBCASE("empty old lines - all processes become new lines") {
    // Set up state with new snapshot
    State state = {};
    state.snapshot_arena = BumpArena::create();

    SnapshotBuilder builder(arena);
    builder.add(10, 0, "proc_a");
    builder.add(20, 0, "proc_b");
    builder.add(30, 0, "proc_c");
    state.snapshot = builder.build();

    // Empty old lines
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_Pid;
    my_state.sorted_order = ImGuiSortDirection_Ascending;

    brief_table_update(my_state, state);

    // All 3 processes should be in lines, sorted by PID
    REQUIRE(my_state.lines.size == 3);
    CHECK(my_state.lines.data[0].pid == 10);
    CHECK(my_state.lines.data[1].pid == 20);
    CHECK(my_state.lines.data[2].pid == 30);

    state.snapshot_arena.destroy();
  }

  SUBCASE("existing processes preserve order, new ones appended") {
    State state = {};
    state.snapshot_arena = BumpArena::create();

    SnapshotBuilder builder(arena);
    builder.add(10, 0, "proc_a");
    builder.add(20, 0, "proc_b");
    builder.add(30, 0, "proc_c");
    builder.add(40, 0, "proc_d");
    state.snapshot = builder.build();

    // Old lines had 30, 10 (in that order) - note different order than PID
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_Pid;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 2);
    my_state.lines.data[0] = {30, 0};  // was at index 0, will be at 2
    my_state.lines.data[1] = {10, 1};  // was at index 1, will be at 0

    brief_table_update(my_state, state);

    // After update and sort: all 4 processes, sorted by PID ascending
    REQUIRE(my_state.lines.size == 4);
    CHECK(my_state.lines.data[0].pid == 10);
    CHECK(my_state.lines.data[1].pid == 20);
    CHECK(my_state.lines.data[2].pid == 30);
    CHECK(my_state.lines.data[3].pid == 40);

    state.snapshot_arena.destroy();
  }

  SUBCASE("removed processes are filtered out") {
    State state = {};
    state.snapshot_arena = BumpArena::create();

    // New snapshot only has PID 20
    SnapshotBuilder builder(arena);
    builder.add(20, 0, "proc_b");
    state.snapshot = builder.build();

    // Old lines had 10, 20, 30
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_Pid;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    my_state.lines.data[0] = {10, 0};
    my_state.lines.data[1] = {20, 1};
    my_state.lines.data[2] = {30, 2};

    brief_table_update(my_state, state);

    // Only PID 20 should remain
    REQUIRE(my_state.lines.size == 1);
    CHECK(my_state.lines.data[0].pid == 20);
    CHECK(my_state.lines.data[0].state_index == 0);

    state.snapshot_arena.destroy();
  }

  SUBCASE("sorting by name descending") {
    State state = {};
    state.snapshot_arena = BumpArena::create();

    SnapshotBuilder builder(arena);
    builder.add(10, 0, "aaa");
    builder.add(20, 0, "zzz");
    builder.add(30, 0, "mmm");
    state.snapshot = builder.build();

    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_Name;
    my_state.sorted_order = ImGuiSortDirection_Descending;

    brief_table_update(my_state, state);

    // Sorted by name descending: zzz (20), mmm (30), aaa (10)
    REQUIRE(my_state.lines.size == 3);
    CHECK(my_state.lines.data[0].pid == 20);  // zzz
    CHECK(my_state.lines.data[1].pid == 30);  // mmm
    CHECK(my_state.lines.data[2].pid == 10);  // aaa

    state.snapshot_arena.destroy();
  }

  arena.destroy();
}

// ============================================================================
// state_snapshot_update Tests (stat derivation)
// ============================================================================

TEST_CASE("state_snapshot_update") {
  BumpArena arena = BumpArena::create();

  SUBCASE("CPU percentage calculation") {
    // Old state: process at 1000 user ticks, 500 kernel ticks
    State old_state = {};
    old_state.system.ticks_in_second = 100;  // 100 ticks per second
    old_state.system.mem_page_size = 4096;

    // Create old snapshot
    ProcessStat old_proc = {};
    old_proc.pid = 100;
    old_proc.utime = 1000;
    old_proc.stime = 500;

    ProcessDerivedStat old_derived = {};

    old_state.snapshot.stats.data = &old_proc;
    old_state.snapshot.stats.size = 1;
    old_state.snapshot.derived_stats.data = &old_derived;
    old_state.snapshot.derived_stats.size = 1;
    old_state.snapshot.at = SteadyTimePoint{};

    // New snapshot: 1100 user ticks, 550 kernel ticks after 1 second
    UpdateSnapshot update = {};
    ProcessStat new_proc = {};
    new_proc.pid = 100;
    new_proc.utime = 1100;  // +100 ticks
    new_proc.stime = 550;   // +50 ticks
    new_proc.statm_resident = 1000;  // 1000 pages

    update.stats.data = &new_proc;
    update.stats.size = 1;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    REQUIRE(result.derived_stats.size == 1);
    // 100 ticks in 1 second = 100% user CPU (100 ticks / 100 ticks_in_second)
    CHECK(result.derived_stats.data[0].cpu_user_perc == doctest::Approx(100.0));
    // 50 ticks in 1 second = 50% kernel CPU
    CHECK(result.derived_stats.data[0].cpu_kernel_perc == doctest::Approx(50.0));
  }

  SUBCASE("memory calculation") {
    State old_state = {};
    old_state.system.ticks_in_second = 100;
    old_state.system.mem_page_size = 4096;

    ProcessStat old_proc = {};
    old_proc.pid = 100;
    ProcessDerivedStat old_derived = {};

    old_state.snapshot.stats.data = &old_proc;
    old_state.snapshot.stats.size = 1;
    old_state.snapshot.derived_stats.data = &old_derived;
    old_state.snapshot.derived_stats.size = 1;
    old_state.snapshot.at = SteadyTimePoint{};

    UpdateSnapshot update = {};
    ProcessStat new_proc = {};
    new_proc.pid = 100;
    new_proc.statm_resident = 256;  // 256 pages

    update.stats.data = &new_proc;
    update.stats.size = 1;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    REQUIRE(result.derived_stats.size == 1);
    // 256 pages * 4096 bytes = 1048576 bytes
    CHECK(result.derived_stats.data[0].mem_resident_bytes == doctest::Approx(256 * 4096));
  }

  SUBCASE("I/O rate calculation") {
    State old_state = {};
    old_state.system.ticks_in_second = 100;
    old_state.system.mem_page_size = 4096;

    ProcessStat old_proc = {};
    old_proc.pid = 100;
    old_proc.io_read_bytes = 1024 * 1024;   // 1 MB
    old_proc.io_write_bytes = 512 * 1024;   // 512 KB

    ProcessDerivedStat old_derived = {};

    old_state.snapshot.stats.data = &old_proc;
    old_state.snapshot.stats.size = 1;
    old_state.snapshot.derived_stats.data = &old_derived;
    old_state.snapshot.derived_stats.size = 1;
    old_state.snapshot.at = SteadyTimePoint{};

    UpdateSnapshot update = {};
    ProcessStat new_proc = {};
    new_proc.pid = 100;
    new_proc.io_read_bytes = 1024 * 1024 + 102400;   // +100 KB
    new_proc.io_write_bytes = 512 * 1024 + 51200;    // +50 KB

    update.stats.data = &new_proc;
    update.stats.size = 1;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    REQUIRE(result.derived_stats.size == 1);
    // 102400 bytes in 1 second = 100 KB/s
    CHECK(result.derived_stats.data[0].io_read_kb_per_sec == doctest::Approx(100.0));
    // 51200 bytes in 1 second = 50 KB/s
    CHECK(result.derived_stats.data[0].io_write_kb_per_sec == doctest::Approx(50.0));
  }

  SUBCASE("new process (not in old snapshot) gets zero CPU") {
    State old_state = {};
    old_state.system.ticks_in_second = 100;
    old_state.system.mem_page_size = 4096;

    // Old snapshot is empty
    old_state.snapshot.stats.size = 0;
    old_state.snapshot.derived_stats.size = 0;
    old_state.snapshot.at = SteadyTimePoint{};

    UpdateSnapshot update = {};
    ProcessStat new_proc = {};
    new_proc.pid = 100;
    new_proc.utime = 1000;
    new_proc.stime = 500;
    new_proc.statm_resident = 100;

    update.stats.data = &new_proc;
    update.stats.size = 1;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    REQUIRE(result.derived_stats.size == 1);
    // New process - no old data to compare, so CPU should be 0
    CHECK(result.derived_stats.data[0].cpu_user_perc == doctest::Approx(0.0));
    CHECK(result.derived_stats.data[0].cpu_kernel_perc == doctest::Approx(0.0));
  }

  SUBCASE("system CPU percentage calculation") {
    State old_state = {};
    old_state.system.ticks_in_second = 100;
    old_state.system.mem_page_size = 4096;
    old_state.snapshot.at = SteadyTimePoint{};

    // Old CPU stats: 100 user, 50 system, 850 idle = 1000 total
    CpuCoreStat old_cpu = {};
    old_cpu.user = 100;
    old_cpu.nice = 0;
    old_cpu.system = 50;
    old_cpu.idle = 850;
    old_cpu.iowait = 0;
    old_cpu.irq = 0;
    old_cpu.softirq = 0;

    old_state.snapshot.cpu_stats.data = &old_cpu;
    old_state.snapshot.cpu_stats.size = 1;

    // New CPU stats: 200 user, 100 system, 900 idle = 1200 total
    // Delta: 100 user, 50 system, 50 idle = 200 total
    CpuCoreStat new_cpu = {};
    new_cpu.user = 200;
    new_cpu.nice = 0;
    new_cpu.system = 100;
    new_cpu.idle = 900;
    new_cpu.iowait = 0;
    new_cpu.irq = 0;
    new_cpu.softirq = 0;

    UpdateSnapshot update = {};
    update.cpu_stats.data = &new_cpu;
    update.cpu_stats.size = 1;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    REQUIRE(result.cpu_perc.total.size == 1);
    // busy delta = 100 + 50 = 150, total delta = 200
    // total CPU = 150/200 * 100 = 75%
    CHECK(result.cpu_perc.total.data[0] == doctest::Approx(75.0));
    // kernel delta = 50, total delta = 200
    // kernel CPU = 50/200 * 100 = 25%
    CHECK(result.cpu_perc.kernel.data[0] == doctest::Approx(25.0));
  }

  SUBCASE("disk I/O rate calculation") {
    State old_state = {};
    old_state.system.ticks_in_second = 100;
    old_state.system.mem_page_size = 4096;
    old_state.snapshot.at = SteadyTimePoint{};

    // Old disk stats: 1000 sectors read, 500 sectors written
    old_state.snapshot.disk_io_stats.sectors_read = 1000;
    old_state.snapshot.disk_io_stats.sectors_written = 500;

    // New disk stats: 3000 sectors read, 1500 sectors written after 1 second
    // Delta: 2000 sectors read, 1000 sectors written
    UpdateSnapshot update = {};
    update.disk_io_stats.sectors_read = 3000;
    update.disk_io_stats.sectors_written = 1500;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    // 2000 sectors * 512 bytes = 1024000 bytes = ~0.976 MB
    // In 1 second = ~0.976 MB/s
    double expected_read = (2000.0 * 512.0) / (1024.0 * 1024.0);
    double expected_write = (1000.0 * 512.0) / (1024.0 * 1024.0);
    CHECK(result.disk_io_rate.read_mb_per_sec == doctest::Approx(expected_read));
    CHECK(result.disk_io_rate.write_mb_per_sec == doctest::Approx(expected_write));
  }

  arena.destroy();
}
