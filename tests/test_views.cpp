#include "doctest.h"

#include "imgui.h"

// ImPlotShadedFlags is used in common_charts.h but we don't need the full
// implot
using ImPlotShadedFlags = int;

#include "sources/proc_parsers.h"
#include "sources/sync.h"
#include "sources/username.h"
#include "state.h"
#include "test_helpers.h"
#include "views/brief_table.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/table_item.h"

#include <pwd.h>
#include <unistd.h>

// ============================================================================
// binary_search_pid Tests
// ============================================================================

TEST_CASE("binary_search_pid") {
  BumpArena arena = BumpArena::create();

  SUBCASE("empty array returns UINT32_MAX") {
    Array<ProcessStat> stats = {};
    stats.size = 0;
    stats.data = nullptr;

    CHECK(binary_search_pid(stats, 1) == UINT32_MAX);
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

    CHECK(binary_search_pid(snapshot.stats, 50) == UINT32_MAX);
    CHECK(binary_search_pid(snapshot.stats, 150) == UINT32_MAX);
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

    CHECK(binary_search_pid(snapshot.stats, 5) == UINT32_MAX);
    CHECK(binary_search_pid(snapshot.stats, 15) == UINT32_MAX);
    CHECK(binary_search_pid(snapshot.stats, 25) == UINT32_MAX);
    CHECK(binary_search_pid(snapshot.stats, 35) == UINT32_MAX);
  }

  arena.destroy();
}

struct TestChartData {
  Pid pid;
  char label[64];
};

// ============================================================================
// common_charts_sort_added Tests
// ============================================================================

TEST_CASE("common_charts_sort_added") {
  BumpArena arena = BumpArena::create();

  SUBCASE("sorts by pid ascending") {
    GrowingArray<TestChartData> charts = {};
    uint32_t wasted = 0;
    charts.emplace_back(arena, wasted)->pid = 50;
    charts.emplace_back(arena, wasted)->pid = 10;
    charts.emplace_back(arena, wasted)->pid = 30;
    charts.emplace_back(arena, wasted)->pid = 20;
    charts.emplace_back(arena, wasted)->pid = 40;

    common_views_sort_added(charts);

    CHECK(charts.data()[0].pid == 10);
    CHECK(charts.data()[1].pid == 20);
    CHECK(charts.data()[2].pid == 30);
    CHECK(charts.data()[3].pid == 40);
    CHECK(charts.data()[4].pid == 50);
  }

  SUBCASE("already sorted stays sorted") {
    GrowingArray<TestChartData> charts = {};
    uint32_t wasted = 0;
    charts.emplace_back(arena, wasted)->pid = 1;
    charts.emplace_back(arena, wasted)->pid = 2;
    charts.emplace_back(arena, wasted)->pid = 3;

    common_views_sort_added(charts);

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
  InternTable interner = InternTable::create(&arena);

  SUBCASE("empty old lines - all processes become new lines") {
    // Set up state with new snapshot
    State state = {};
    state.snapshot_arena = BumpArena::create();

    SnapshotBuilder builder(arena);
    builder.add(10, 0, "proc_a");
    builder.add(20, 0, "proc_b");
    builder.add(30, 0, "proc_c");
    state.snapshot = builder.build();

    BriefTableState my_state = {};
    my_state.tree_mode = false;
    my_state.sorted_by = eBriefTableColumnId_Pid;
    my_state.sorted_order = ImGuiSortDirection_Ascending;

    brief_table_update(my_state, interner, state);

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

    BriefTableState my_state = {};
    my_state.tree_mode = false;
    my_state.sorted_by = eBriefTableColumnId_Pid;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 2);
    my_state.lines.data[0] =
        make_brief_table_line(nullptr, 30, 0); // was at index 0, will be at 2
    my_state.lines.data[1] =
        make_brief_table_line(nullptr, 10, 1); // was at index 1, will be at 0

    brief_table_update(my_state, interner, state);

    // After update and sort: all 4 processes, sorted by PID ascending
    REQUIRE(my_state.lines.size == 4);
    CHECK(my_state.lines.data[0].pid == 10);
    CHECK(my_state.lines.data[1].pid == 20);
    CHECK(my_state.lines.data[2].pid == 30);
    CHECK(my_state.lines.data[3].pid == 40);

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
    my_state.tree_mode = false;
    my_state.sorted_by = eBriefTableColumnId_Name;
    my_state.sorted_order = ImGuiSortDirection_Descending;

    brief_table_update(my_state, interner, state);

    // Sorted by name descending: zzz (20), mmm (30), aaa (10)
    REQUIRE(my_state.lines.size == 3);
    CHECK(my_state.lines.data[0].pid == 20); // zzz
    CHECK(my_state.lines.data[1].pid == 30); // mmm
    CHECK(my_state.lines.data[2].pid == 10); // aaa

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
    old_state.system.ticks_in_second = 100; // 100 ticks per second
    old_state.system.mem_page_size = 4096;

    // Create old snapshot
    ProcessStat old_proc = {};
    old_proc.pid = 100;
    old_proc.utime = 1000;
    old_proc.stime = 500;

    ProcessDerivedStat old_derived = {};

    // /proc/stat: [0]=aggregate, [1]=single core. Per-process CPU% is
    // normalized by the aggregate jiffy delta divided by the core count, so a
    // 100-jiffy delta over 1 core gives a per-core budget of 100 ticks.
    CpuCoreStat old_cpu[2] = {};
    CpuCoreStat new_cpu[2] = {};
    new_cpu[0].idle = 100; // aggregate gains 100 jiffies over the interval

    old_state.snapshot.stats.data = &old_proc;
    old_state.snapshot.stats.size = 1;
    old_state.snapshot.derived_stats.data = &old_derived;
    old_state.snapshot.derived_stats.size = 1;
    old_state.snapshot.cpu_stats.data = old_cpu;
    old_state.snapshot.cpu_stats.size = 2;
    old_state.snapshot.at = SteadyTimePoint{};

    // New snapshot: 1100 user ticks, 550 kernel ticks after 1 second
    UpdateSnapshot update = {};
    ProcessStat new_proc = {};
    new_proc.pid = 100;
    new_proc.utime = 1100;          // +100 ticks
    new_proc.stime = 550;           // +50 ticks
    new_proc.statm_resident = 1000; // 1000 pages

    update.stats.data = &new_proc;
    update.stats.size = 1;
    update.cpu_stats.data = new_cpu;
    update.cpu_stats.size = 2;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    REQUIRE(result.derived_stats.size == 1);
    // 100 process ticks against a 100-tick per-core budget = 100% user CPU
    CHECK(result.derived_stats.data[0].cpu_user_perc == doctest::Approx(100.0));
    // 50 ticks = 50% kernel CPU
    CHECK(result.derived_stats.data[0].cpu_kernel_perc ==
          doctest::Approx(50.0));
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
    new_proc.statm_resident = 256; // 256 pages

    update.stats.data = &new_proc;
    update.stats.size = 1;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    REQUIRE(result.derived_stats.size == 1);
    // 256 pages * 4096 bytes = 1048576 bytes
    CHECK(result.derived_stats.data[0].mem_resident_bytes ==
          doctest::Approx(256 * 4096));
  }

  SUBCASE("I/O rate calculation") {
    State old_state = {};
    old_state.system.ticks_in_second = 100;
    old_state.system.mem_page_size = 4096;

    ProcessStat old_proc = {};
    old_proc.pid = 100;
    old_proc.io_read_bytes = 1024 * 1024; // 1 MB
    old_proc.io_write_bytes = 512 * 1024; // 512 KB

    ProcessDerivedStat old_derived = {};

    old_state.snapshot.stats.data = &old_proc;
    old_state.snapshot.stats.size = 1;
    old_state.snapshot.derived_stats.data = &old_derived;
    old_state.snapshot.derived_stats.size = 1;
    old_state.snapshot.at = SteadyTimePoint{};

    UpdateSnapshot update = {};
    ProcessStat new_proc = {};
    new_proc.pid = 100;
    new_proc.io_read_bytes = 1024 * 1024 + 102400; // +100 KB
    new_proc.io_write_bytes = 512 * 1024 + 51200;  // +50 KB

    update.stats.data = &new_proc;
    update.stats.size = 1;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    REQUIRE(result.derived_stats.size == 1);
    // 102400 bytes in 1 second = 100 KB/s
    CHECK(result.derived_stats.data[0].io_read_kb_per_sec ==
          doctest::Approx(100.0));
    // 51200 bytes in 1 second = 50 KB/s
    CHECK(result.derived_stats.data[0].io_write_kb_per_sec ==
          doctest::Approx(50.0));
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
    // ...but memory is instantaneous, so it's still reported on first sight.
    CHECK(result.derived_stats.data[0].mem_resident_bytes ==
          doctest::Approx(100 * 4096));
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

  SUBCASE("old process absent from new snapshot is skipped during alignment") {
    // Old snapshot has PIDs 50 and 100 (sorted); PID 50 has since died, so the
    // new snapshot only has PID 100. Aligning to PID 100 must skip past the
    // stale PID 50 entry and still match utime/stime correctly.
    State old_state = {};
    old_state.system.ticks_in_second = 100;
    old_state.system.mem_page_size = 4096;
    old_state.snapshot.at = SteadyTimePoint{};

    ProcessStat old_procs[2] = {};
    old_procs[0].pid = 50; // dead by the new snapshot
    old_procs[0].utime = 999;
    old_procs[1].pid = 100;
    old_procs[1].utime = 1000;
    old_procs[1].stime = 500;
    ProcessDerivedStat old_derived[2] = {};

    CpuCoreStat old_cpu[2] = {};
    CpuCoreStat new_cpu[2] = {};
    new_cpu[0].idle = 100; // 100-jiffy aggregate delta over 1 core

    old_state.snapshot.stats.data = old_procs;
    old_state.snapshot.stats.size = 2;
    old_state.snapshot.derived_stats.data = old_derived;
    old_state.snapshot.derived_stats.size = 2;
    old_state.snapshot.cpu_stats.data = old_cpu;
    old_state.snapshot.cpu_stats.size = 2;

    UpdateSnapshot update = {};
    ProcessStat new_proc = {};
    new_proc.pid = 100;
    new_proc.utime = 1100; // +100 ticks
    new_proc.stime = 550;  // +50 ticks

    update.stats.data = &new_proc;
    update.stats.size = 1;
    update.cpu_stats.data = new_cpu;
    update.cpu_stats.size = 2;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    REQUIRE(result.derived_stats.size == 1);
    // Matched against PID 100 (not the skipped PID 50): 100% user, 50% kernel.
    CHECK(result.derived_stats.data[0].cpu_user_perc == doctest::Approx(100.0));
    CHECK(result.derived_stats.data[0].cpu_kernel_perc ==
          doctest::Approx(50.0));
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
    CHECK(result.disk_io_rate.read_mb_per_sec ==
          doctest::Approx(expected_read));
    CHECK(result.disk_io_rate.write_mb_per_sec ==
          doctest::Approx(expected_write));
  }

  arena.destroy();
}

// ============================================================================
// sort_brief_table_tree Tests
// ============================================================================

TEST_CASE("sort_brief_table_tree") {
  BumpArena arena = BumpArena::create();

  SUBCASE("single root process") {
    BriefTableState my_state = {};
    my_state.lines = Array<BriefTableLine>::create(arena, 1);
    my_state.lines.data[0] = make_brief_table_line("init", 1, 0);

    sort_brief_table_tree(my_state, arena);

    REQUIRE(my_state.lines.size == 1);
    CHECK(my_state.lines.data[0].pid == 1);
    CHECK(my_state.lines.data[0].tree_depth == 0);
  }

  SUBCASE("parent with children - DFS order") {
    BriefTableState my_state = {};
    my_state.lines = Array<BriefTableLine>::create(arena, 4);
    my_state.lines.data[0] = make_brief_table_line("init", 1, 0);
    my_state.lines.data[1] = make_brief_table_line("child_a", 10, 1);
    my_state.lines.data[2] = make_brief_table_line("child_b", 20, 1);
    my_state.lines.data[3] = make_brief_table_line("grandchild", 30, 10);

    sort_brief_table_tree(my_state, arena);

    REQUIRE(my_state.lines.size == 4);
    // DFS: init(1) -> child_a(10) -> grandchild(30) -> child_b(20)
    CHECK(my_state.lines.data[0].pid == 1);
    CHECK(my_state.lines.data[0].tree_depth == 0);
    CHECK(my_state.lines.data[1].pid == 10);
    CHECK(my_state.lines.data[1].tree_depth == 1);
    CHECK(my_state.lines.data[2].pid == 30);
    CHECK(my_state.lines.data[2].tree_depth == 2);
    CHECK(my_state.lines.data[3].pid == 20);
    CHECK(my_state.lines.data[3].tree_depth == 1);
  }

  SUBCASE("multiple root processes") {
    BriefTableState my_state = {};
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    // All have ppid=0 or ppid not in list
    my_state.lines.data[0] = make_brief_table_line("orphan", 5, 999);
    my_state.lines.data[1] = make_brief_table_line("init", 1, 0);
    my_state.lines.data[2] = make_brief_table_line("kthread", 2, 0);

    sort_brief_table_tree(my_state, arena);

    REQUIRE(my_state.lines.size == 3);
    // All roots at depth 0, sorted by PID
    CHECK(my_state.lines.data[0].pid == 1);
    CHECK(my_state.lines.data[0].tree_depth == 0);
    CHECK(my_state.lines.data[1].pid == 2);
    CHECK(my_state.lines.data[1].tree_depth == 0);
    CHECK(my_state.lines.data[2].pid == 5);
    CHECK(my_state.lines.data[2].tree_depth == 0);
  }

  SUBCASE("empty lines") {
    BriefTableState my_state = {};
    my_state.lines = {};
    my_state.lines.size = 0;

    sort_brief_table_tree(my_state, arena);
    CHECK(my_state.lines.size == 0);
  }

  SUBCASE("deep hierarchy") {
    BriefTableState my_state = {};
    my_state.lines = Array<BriefTableLine>::create(arena, 4);
    my_state.lines.data[0] = make_brief_table_line("root", 1, 0);
    my_state.lines.data[1] = make_brief_table_line("level1", 2, 1);
    my_state.lines.data[2] = make_brief_table_line("level2", 3, 2);
    my_state.lines.data[3] = make_brief_table_line("level3", 4, 3);

    sort_brief_table_tree(my_state, arena);

    REQUIRE(my_state.lines.size == 4);
    CHECK(my_state.lines.data[0].tree_depth == 0);
    CHECK(my_state.lines.data[1].tree_depth == 1);
    CHECK(my_state.lines.data[2].tree_depth == 2);
    CHECK(my_state.lines.data[3].tree_depth == 3);
  }

  arena.destroy();
}

// ============================================================================
// brief_table_update dead process handling Tests
// ============================================================================

TEST_CASE("brief_table_update dead process handling") {
  BumpArena arena = BumpArena::create();
  InternTable interner = InternTable::create(&arena);

  SUBCASE("dead process retained within 2s window") {
    State state = {};
    state.snapshot_arena = BumpArena::create();

    // Snapshot at t=3s with only PID 20 alive (PID 10 died)
    SnapshotBuilder builder(arena);
    builder.add(20, 0, "proc_b");
    state.snapshot = builder.build();
    state.snapshot.at = SteadyTimePoint{} + std::chrono::seconds(3);

    // Old lines had PID 10 and PID 20
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_Pid;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 2);
    my_state.lines.data[0] = make_brief_table_line("proc_a", 10, 0);
    my_state.lines.data[1] = make_brief_table_line("proc_b", 20, 0);

    brief_table_update(my_state, interner, state);

    // Both should still be present (PID 10 just died)
    REQUIRE(my_state.lines.size == 2);
    // PID 10 should have death_time_ns set
    bool found_dead = false;
    for (size_t i = 0; i < my_state.lines.size; ++i) {
      if (my_state.lines.data[i].pid == 10) {
        CHECK(my_state.lines.data[i].death_time_ns != 0);
        found_dead = true;
      }
    }
    CHECK(found_dead);

    state.snapshot_arena.destroy();
  }

  SUBCASE("dead process removed after 2s expiry") {
    State state = {};
    state.snapshot_arena = BumpArena::create();

    // Snapshot at t=10s with only PID 20
    SnapshotBuilder builder(arena);
    builder.add(20, 0, "proc_b");
    state.snapshot = builder.build();
    state.snapshot.at = SteadyTimePoint{} + std::chrono::seconds(10);

    // Old lines: PID 10 died at t=5s (5 seconds ago, > 2s threshold)
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_Pid;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 2);
    my_state.lines.data[0] = make_brief_table_line("proc_a", 10, 0);
    my_state.lines.data[0].death_time_ns =
        (SteadyTimePoint{} + std::chrono::seconds(5))
            .time_since_epoch()
            .count();
    my_state.lines.data[1] = make_brief_table_line("proc_b", 20, 0);

    brief_table_update(my_state, interner, state);

    // Only PID 20 should remain
    REQUIRE(my_state.lines.size == 1);
    CHECK(my_state.lines.data[0].pid == 20);

    state.snapshot_arena.destroy();
  }

  SUBCASE("first_seen_ns is 0 on initial update") {
    State state = {};
    state.snapshot_arena = BumpArena::create();

    SnapshotBuilder builder(arena);
    builder.add(10, 0, "proc_a");
    state.snapshot = builder.build();
    state.snapshot.at = SteadyTimePoint{} + std::chrono::seconds(1);

    // Empty old lines (first update)
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_Pid;
    my_state.sorted_order = ImGuiSortDirection_Ascending;

    brief_table_update(my_state, interner, state);

    REQUIRE(my_state.lines.size == 1);
    // On first update, first_seen_ns should be 0 (avoids "new" highlight)
    CHECK(my_state.lines.data[0].first_seen_ns == 0);

    state.snapshot_arena.destroy();
  }

  SUBCASE("new processes get current timestamp as first_seen_ns") {
    State state = {};
    state.snapshot_arena = BumpArena::create();

    SnapshotBuilder builder(arena);
    builder.add(10, 0, "proc_a");
    builder.add(20, 0, "proc_b");
    state.snapshot = builder.build();
    state.snapshot.at = SteadyTimePoint{} + std::chrono::seconds(5);

    // Old lines had only PID 10
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_Pid;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 1);
    my_state.lines.data[0] = make_brief_table_line("proc_a", 10, 0);
    my_state.lines.data[0].first_seen_ns = 1000000000; // was seen at 1s

    brief_table_update(my_state, interner, state);

    REQUIRE(my_state.lines.size == 2);
    // PID 10 should keep old first_seen_ns
    // PID 20 should get now_ns as first_seen_ns
    const int64_t now_ns = (SteadyTimePoint{} + std::chrono::seconds(5))
                               .time_since_epoch()
                               .count();
    for (size_t i = 0; i < my_state.lines.size; ++i) {
      if (my_state.lines.data[i].pid == 10) {
        CHECK(my_state.lines.data[i].first_seen_ns == 1000000000);
      } else if (my_state.lines.data[i].pid == 20) {
        CHECK(my_state.lines.data[i].first_seen_ns == now_ns);
      }
    }

    state.snapshot_arena.destroy();
  }

  arena.destroy();
}

// ============================================================================
// sort_brief_table_lines by different columns Tests
// ============================================================================

TEST_CASE("sort_brief_table_lines by columns") {
  BumpArena arena = BumpArena::create();

  SUBCASE("sort by CPU total ascending") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_CpuTotalPerc;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    my_state.lines.data[0] = make_brief_table_line("high", 1, 0);
    my_state.lines.data[0].derived_stat.cpu_user_perc = 80.0;
    my_state.lines.data[0].derived_stat.cpu_kernel_perc = 20.0;
    my_state.lines.data[1] = make_brief_table_line("low", 2, 0);
    my_state.lines.data[1].derived_stat.cpu_user_perc = 5.0;
    my_state.lines.data[1].derived_stat.cpu_kernel_perc = 1.0;
    my_state.lines.data[2] = make_brief_table_line("mid", 3, 0);
    my_state.lines.data[2].derived_stat.cpu_user_perc = 30.0;
    my_state.lines.data[2].derived_stat.cpu_kernel_perc = 10.0;

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2); // 6% total
    CHECK(my_state.lines.data[1].pid == 3); // 40% total
    CHECK(my_state.lines.data[2].pid == 1); // 100% total
  }

  SUBCASE("sort by CPU total descending") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_CpuTotalPerc;
    my_state.sorted_order = ImGuiSortDirection_Descending;
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    my_state.lines.data[0] = make_brief_table_line("low", 1, 0);
    my_state.lines.data[0].derived_stat.cpu_user_perc = 5.0;
    my_state.lines.data[0].derived_stat.cpu_kernel_perc = 1.0;
    my_state.lines.data[1] = make_brief_table_line("high", 2, 0);
    my_state.lines.data[1].derived_stat.cpu_user_perc = 80.0;
    my_state.lines.data[1].derived_stat.cpu_kernel_perc = 20.0;
    my_state.lines.data[2] = make_brief_table_line("mid", 3, 0);
    my_state.lines.data[2].derived_stat.cpu_user_perc = 30.0;
    my_state.lines.data[2].derived_stat.cpu_kernel_perc = 10.0;

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2); // 100%
    CHECK(my_state.lines.data[1].pid == 3); // 40%
    CHECK(my_state.lines.data[2].pid == 1); // 6%
  }

  SUBCASE("sort by memory RSS") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_MemRssBytes;
    my_state.sorted_order = ImGuiSortDirection_Descending;
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    my_state.lines.data[0] = make_brief_table_line("small", 1, 0);
    my_state.lines.data[0].derived_stat.mem_resident_bytes = 1024.0;
    my_state.lines.data[1] = make_brief_table_line("large", 2, 0);
    my_state.lines.data[1].derived_stat.mem_resident_bytes = 1048576.0;
    my_state.lines.data[2] = make_brief_table_line("medium", 3, 0);
    my_state.lines.data[2].derived_stat.mem_resident_bytes = 65536.0;

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2); // 1MB
    CHECK(my_state.lines.data[1].pid == 3); // 64KB
    CHECK(my_state.lines.data[2].pid == 1); // 1KB
  }

  SUBCASE("sort by I/O read") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_IoReadKbPerSec;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    my_state.lines.data[0] = make_brief_table_line("c", 1, 0);
    my_state.lines.data[0].derived_stat.io_read_kb_per_sec = 500.0;
    my_state.lines.data[1] = make_brief_table_line("a", 2, 0);
    my_state.lines.data[1].derived_stat.io_read_kb_per_sec = 10.0;
    my_state.lines.data[2] = make_brief_table_line("b", 3, 0);
    my_state.lines.data[2].derived_stat.io_read_kb_per_sec = 100.0;

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2); // 10 KB/s
    CHECK(my_state.lines.data[1].pid == 3); // 100 KB/s
    CHECK(my_state.lines.data[2].pid == 1); // 500 KB/s
  }

  SUBCASE("sort by I/O write descending") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_IoWriteKbPerSec;
    my_state.sorted_order = ImGuiSortDirection_Descending;
    my_state.lines = Array<BriefTableLine>::create(arena, 2);
    my_state.lines.data[0] = make_brief_table_line("low", 1, 0);
    my_state.lines.data[0].derived_stat.io_write_kb_per_sec = 5.0;
    my_state.lines.data[1] = make_brief_table_line("high", 2, 0);
    my_state.lines.data[1].derived_stat.io_write_kb_per_sec = 200.0;

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2); // 200 KB/s
    CHECK(my_state.lines.data[1].pid == 1); // 5 KB/s
  }

  SUBCASE("sort by state") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_State;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    my_state.lines.data[0] = make_brief_table_line("z", 1, 0, 'Z');
    my_state.lines.data[1] = make_brief_table_line("r", 2, 0, 'R');
    my_state.lines.data[2] = make_brief_table_line("s", 3, 0, 'S');

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2); // 'R'
    CHECK(my_state.lines.data[1].pid == 3); // 'S'
    CHECK(my_state.lines.data[2].pid == 1); // 'Z'
  }

  SUBCASE("sort by thread count descending") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_Threads;
    my_state.sorted_order = ImGuiSortDirection_Descending;
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    my_state.lines.data[0] = make_brief_table_line("a", 1, 0);
    my_state.lines.data[0].num_threads = 4;
    my_state.lines.data[1] = make_brief_table_line("b", 2, 0);
    my_state.lines.data[1].num_threads = 16;
    my_state.lines.data[2] = make_brief_table_line("c", 3, 0);
    my_state.lines.data[2].num_threads = 1;

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2); // 16
    CHECK(my_state.lines.data[1].pid == 1); // 4
    CHECK(my_state.lines.data[2].pid == 3); // 1
  }

  SUBCASE("sort by CPU user perc") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_CpuUserPerc;
    my_state.sorted_order = ImGuiSortDirection_Descending;
    my_state.lines = Array<BriefTableLine>::create(arena, 2);
    my_state.lines.data[0] = make_brief_table_line("low", 1, 0);
    my_state.lines.data[0].derived_stat.cpu_user_perc = 10.0;
    my_state.lines.data[0].derived_stat.cpu_kernel_perc = 90.0;
    my_state.lines.data[1] = make_brief_table_line("high", 2, 0);
    my_state.lines.data[1].derived_stat.cpu_user_perc = 70.0;
    my_state.lines.data[1].derived_stat.cpu_kernel_perc = 1.0;

    sort_brief_table_lines(my_state);

    // Ranks by user% only, ignoring kernel%.
    CHECK(my_state.lines.data[0].pid == 2); // 70% user
    CHECK(my_state.lines.data[1].pid == 1); // 10% user
  }

  SUBCASE("sort by CPU kernel perc") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_CpuKernelPerc;
    my_state.sorted_order = ImGuiSortDirection_Descending;
    my_state.lines = Array<BriefTableLine>::create(arena, 2);
    my_state.lines.data[0] = make_brief_table_line("low", 1, 0);
    my_state.lines.data[0].derived_stat.cpu_user_perc = 90.0;
    my_state.lines.data[0].derived_stat.cpu_kernel_perc = 10.0;
    my_state.lines.data[1] = make_brief_table_line("high", 2, 0);
    my_state.lines.data[1].derived_stat.cpu_user_perc = 1.0;
    my_state.lines.data[1].derived_stat.cpu_kernel_perc = 70.0;

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2); // 70% kernel
    CHECK(my_state.lines.data[1].pid == 1); // 10% kernel
  }

  SUBCASE("sort by virtual memory") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_MemVirtBytes;
    my_state.sorted_order = ImGuiSortDirection_Descending;
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    my_state.lines.data[0] = make_brief_table_line("small", 1, 0);
    my_state.lines.data[0].derived_stat.mem_virtual_bytes = 1024.0;
    my_state.lines.data[1] = make_brief_table_line("large", 2, 0);
    my_state.lines.data[1].derived_stat.mem_virtual_bytes = 1048576.0;
    my_state.lines.data[2] = make_brief_table_line("medium", 3, 0);
    my_state.lines.data[2].derived_stat.mem_virtual_bytes = 65536.0;

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2);
    CHECK(my_state.lines.data[1].pid == 3);
    CHECK(my_state.lines.data[2].pid == 1);
  }

  SUBCASE("sort by command line") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_CmdLine;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    my_state.lines.data[0] = make_brief_table_line("c", 1, 0);
    my_state.lines.data[0].cmdline = "/usr/bin/zsh";
    my_state.lines.data[1] = make_brief_table_line("a", 2, 0);
    my_state.lines.data[1].cmdline = "/usr/bin/bash";
    my_state.lines.data[2] = make_brief_table_line("b", 3, 0);
    my_state.lines.data[2].cmdline = "/usr/bin/fish";

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2); // bash
    CHECK(my_state.lines.data[1].pid == 3); // fish
    CHECK(my_state.lines.data[2].pid == 1); // zsh
  }

  SUBCASE("sort by username") {
    BriefTableState my_state = {};
    my_state.sorted_by = eBriefTableColumnId_Username;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    my_state.lines = Array<BriefTableLine>::create(arena, 3);
    my_state.lines.data[0] = make_brief_table_line("c", 1, 0);
    my_state.lines.data[0].username = PersistentString{"root"};
    my_state.lines.data[1] = make_brief_table_line("a", 2, 0);
    my_state.lines.data[1].username = PersistentString{"daemon"};
    my_state.lines.data[2] = make_brief_table_line("b", 3, 0);
    my_state.lines.data[2].username = PersistentString{"matrohin"};

    sort_brief_table_lines(my_state);

    CHECK(my_state.lines.data[0].pid == 2); // daemon
    CHECK(my_state.lines.data[1].pid == 3); // matrohin
    CHECK(my_state.lines.data[2].pid == 1); // root
  }

  arena.destroy();
}

// ============================================================================
// process_display_name Tests
// ============================================================================

TEST_CASE("process_display_name") {
  BumpArena arena = BumpArena::create();
  InternTable interner = InternTable::create(&arena);

  SUBCASE("null and empty cmdline fall back to comm") {
    CHECK(strcmp(process_display_name(nullptr, "kthreadd", interner).data,
                 "kthreadd") == 0);
    CHECK(strcmp(process_display_name("", "kthreadd", interner).data,
                 "kthreadd") == 0);
  }

  SUBCASE("strips leading directories") {
    ConstString name = process_display_name("/usr/bin/bash", "bash", interner);
    REQUIRE(name.data != nullptr);
    CHECK(strcmp(name.data, "bash") == 0);
  }

  SUBCASE("stops at first argument separator") {
    ConstString name =
        process_display_name("/usr/bin/python3 script.py -v", "foo", interner);
    REQUIRE(name.data != nullptr);
    CHECK(strcmp(name.data, "python3") == 0);
  }

  SUBCASE("plain basename with no path") {
    ConstString name = process_display_name("cat", "cat", interner);
    REQUIRE(name.data != nullptr);
    CHECK(strcmp(name.data, "cat") == 0);
  }

  SUBCASE("trailing slash leaves no basename, falls back to comm") {
    // base advances past the last '/' to end-of-token, so base == end.
    CHECK(strcmp(process_display_name("/usr/bin/", "comm", interner).data,
                 "comm") == 0);
  }

  arena.destroy();
}

// ============================================================================
// format_memory_bytes Tests
// ============================================================================

TEST_CASE("format_memory_bytes") {
  char buf[32];

  SUBCASE("bytes range") {
    format_memory_bytes(0.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "0 B") == 0);

    format_memory_bytes(512.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "512 B") == 0);

    format_memory_bytes(1023.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "1023 B") == 0);
  }

  SUBCASE("kilobytes range") {
    format_memory_bytes(1024.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "1 KB") == 0);

    format_memory_bytes(500.0 * 1024.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "500 KB") == 0);
  }

  SUBCASE("megabytes range") {
    format_memory_bytes(1024.0 * 1024.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "1.0 MB") == 0);

    format_memory_bytes(512.0 * 1024.0 * 1024.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "512.0 MB") == 0);
  }

  SUBCASE("gigabytes range") {
    format_memory_bytes(1024.0 * 1024.0 * 1024.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "1.0 GB") == 0);

    format_memory_bytes(2.5 * 1024.0 * 1024.0 * 1024.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "2.5 GB") == 0);
  }
}

// ============================================================================
// format_io_rate_kb Tests
// ============================================================================

TEST_CASE("format_io_rate_kb") {
  char buf[32];

  SUBCASE("bytes per second range") {
    format_io_rate_kb(0.5, buf, sizeof(buf), nullptr);
    CHECK(strcmp(buf, "512 B/s") == 0);

    format_io_rate_kb(0.0, buf, sizeof(buf), nullptr);
    CHECK(strcmp(buf, "0 B/s") == 0);
  }

  SUBCASE("KB/s range") {
    format_io_rate_kb(1.0, buf, sizeof(buf), nullptr);
    CHECK(strcmp(buf, "1.0 KB/s") == 0);

    format_io_rate_kb(500.0, buf, sizeof(buf), nullptr);
    CHECK(strcmp(buf, "500.0 KB/s") == 0);
  }

  SUBCASE("MB/s range") {
    format_io_rate_kb(1024.0, buf, sizeof(buf), nullptr);
    CHECK(strcmp(buf, "1.0 MB/s") == 0);

    format_io_rate_kb(2048.0, buf, sizeof(buf), nullptr);
    CHECK(strcmp(buf, "2.0 MB/s") == 0);
  }

  SUBCASE("GB/s range") {
    format_io_rate_kb(1024.0 * 1024.0, buf, sizeof(buf), nullptr);
    CHECK(strcmp(buf, "1.0 GB/s") == 0);
  }
}

// ============================================================================
// scale_cpu_perc Tests
// ============================================================================

TEST_CASE("scale_cpu_perc") {
  SUBCASE("per_core mode returns raw value") {
    CHECK(scale_cpu_perc(200.0, 4, true) == doctest::Approx(200.0));
    CHECK(scale_cpu_perc(400.0, 8, true) == doctest::Approx(400.0));
    CHECK(scale_cpu_perc(50.0, 1, true) == doctest::Approx(50.0));
  }

  SUBCASE("normalized mode divides by num_cores") {
    CHECK(scale_cpu_perc(400.0, 4, false) == doctest::Approx(100.0));
    CHECK(scale_cpu_perc(200.0, 4, false) == doctest::Approx(50.0));
    CHECK(scale_cpu_perc(800.0, 8, false) == doctest::Approx(100.0));
  }

  SUBCASE("num_cores <= 0 returns raw value") {
    CHECK(scale_cpu_perc(200.0, 0, false) == doctest::Approx(200.0));
    CHECK(scale_cpu_perc(200.0, -1, false) == doctest::Approx(200.0));
  }

  SUBCASE("single core normalized is same as raw") {
    CHECK(scale_cpu_perc(75.0, 1, false) == doctest::Approx(75.0));
  }
}

// ============================================================================
// CpuCoreStat method Tests
// ============================================================================

TEST_CASE("CpuCoreStat methods") {
  CpuCoreStat cpu = {};
  cpu.user = 100;
  cpu.nice = 10;
  cpu.system = 50;
  cpu.idle = 800;
  cpu.iowait = 20;
  cpu.irq = 5;
  cpu.softirq = 15;

  SUBCASE("total() sums all fields") {
    CHECK(cpu.total() == 100 + 10 + 50 + 800 + 20 + 5 + 15);
    CHECK(cpu.total() == 1000);
  }

  SUBCASE("busy() excludes idle and iowait") {
    // busy = user + nice + system + irq + softirq
    CHECK(cpu.busy() == 100 + 10 + 50 + 5 + 15);
    CHECK(cpu.busy() == 180);
  }

  SUBCASE("kernel() = system + irq + softirq") {
    CHECK(cpu.kernel() == 50 + 5 + 15);
    CHECK(cpu.kernel() == 70);
  }

  SUBCASE("interrupts() = irq + softirq") {
    CHECK(cpu.interrupts() == 5 + 15);
    CHECK(cpu.interrupts() == 20);
  }

  SUBCASE("all zeros") {
    CpuCoreStat zero = {};
    CHECK(zero.total() == 0);
    CHECK(zero.busy() == 0);
    CHECK(zero.kernel() == 0);
    CHECK(zero.interrupts() == 0);
  }
}

// ============================================================================
// find_top_process Tests
// ============================================================================

TEST_CASE("find_top_process") {
  BumpArena arena = BumpArena::create();
  InternTable interner = InternTable::create(&arena);

  SUBCASE("finds process with highest CPU") {
    SnapshotBuilder builder(arena);
    builder.add(10, 0, "low_cpu", 'S', 5.0, 1.0);
    builder.add(20, 0, "high_cpu", 'R', 80.0, 20.0);
    builder.add(30, 0, "mid_cpu", 'S', 30.0, 10.0);
    StateSnapshot snapshot = builder.build();

    TopProcess top =
        find_top_process(snapshot, interner, [](const ProcessDerivedStat &d) {
          return d.cpu_user_perc + d.cpu_kernel_perc;
        });

    CHECK(top.pid == 20);
    CHECK(top.value == doctest::Approx(100.0));
    CHECK(strcmp(top.name.data, "high_cpu") == 0);
  }

  SUBCASE("finds process with highest memory") {
    SnapshotBuilder builder(arena);
    builder.add(10, 0, "small", 'S', 0.0, 0.0, 1024.0);
    builder.add(20, 0, "large", 'S', 0.0, 0.0, 1048576.0);
    builder.add(30, 0, "medium", 'S', 0.0, 0.0, 65536.0);
    StateSnapshot snapshot = builder.build();

    TopProcess top =
        find_top_process(snapshot, interner, [](const ProcessDerivedStat &d) {
          return d.mem_resident_bytes;
        });

    CHECK(top.pid == 20);
    CHECK(top.value == doctest::Approx(1048576.0));
  }

  SUBCASE("empty snapshot returns zero") {
    StateSnapshot snapshot = {};
    snapshot.stats.size = 0;
    snapshot.derived_stats.size = 0;

    TopProcess top =
        find_top_process(snapshot, interner, [](const ProcessDerivedStat &d) {
          return d.cpu_user_perc;
        });

    CHECK(top.pid == 0);
    CHECK(top.value == doctest::Approx(0.0));
  }

  SUBCASE("single process") {
    SnapshotBuilder builder(arena);
    builder.add(42, 0, "only_one", 'S', 50.0, 10.0);
    StateSnapshot snapshot = builder.build();

    TopProcess top =
        find_top_process(snapshot, interner, [](const ProcessDerivedStat &d) {
          return d.cpu_user_perc;
        });

    CHECK(top.pid == 42);
    CHECK(top.value == doctest::Approx(50.0));
    CHECK(strcmp(top.name.data, "only_one") == 0);
  }

  arena.destroy();
}

// ============================================================================
// state_snapshot_update network I/O Tests
// ============================================================================

TEST_CASE("state_snapshot_update network I/O") {
  BumpArena arena = BumpArena::create();

  SUBCASE("system network I/O rate calculation") {
    State old_state = {};
    old_state.system.ticks_in_second = 100;
    old_state.system.mem_page_size = 4096;
    old_state.snapshot.at = SteadyTimePoint{};

    // Old network stats
    old_state.snapshot.net_io_stats.bytes_received = 10'000'000;
    old_state.snapshot.net_io_stats.bytes_transmitted = 5'000'000;

    // New network stats after 1 second
    // Delta: 1,048,576 bytes received (1 MB), 524,288 bytes transmitted (0.5
    // MB)
    UpdateSnapshot update = {};
    update.net_io_stats.bytes_received = 10'000'000 + 1048576;
    update.net_io_stats.bytes_transmitted = 5'000'000 + 524288;
    update.at = old_state.snapshot.at + std::chrono::seconds(1);

    StateSnapshot result = state_snapshot_update(arena, old_state, update);

    // 1048576 bytes / (1024*1024) / 1 sec = 1.0 MB/s
    CHECK(result.net_io_rate.recv_mb_per_sec == doctest::Approx(1.0));
    // 524288 bytes / (1024*1024) / 1 sec = 0.5 MB/s
    CHECK(result.net_io_rate.send_mb_per_sec == doctest::Approx(0.5));
  }

  arena.destroy();
}

// ============================================================================
// parse_proc_stat_bufs Tests
// ============================================================================

// Helper: build a /proc/[pid]/stat line from components.
// Fields after comm: state ppid pgrp session tty_nr tpgid flags
//   minflt cminflt majflt cmajflt utime stime cutime cstime priority nice
//   num_threads itrealvalue starttime vsize
static const char *make_stat_line(char *buf, size_t buf_size, int pid,
                                  const char *comm, char state, int ppid,
                                  ulong utime, ulong stime, long num_threads,
                                  ulong vsize) {
  snprintf(buf, buf_size,
           "%d (%s) %c %d 0 0 0 -1 0 0 0 0 0 %lu %lu 0 0 20 0 %ld 0 0 %lu", pid,
           comm, state, ppid, utime, stime, num_threads, vsize);
  return buf;
}

TEST_CASE("parse_proc_stat_bufs") {
  ProcessStat stat = {};

  SUBCASE("basic process") {
    char stat_buf[256];
    make_stat_line(stat_buf, sizeof(stat_buf), 1234, "bash", 'S', 100, 5, 2, 1,
                   102400);
    const char *statm_buf = "25600 2500 1000 100 0 3000 0";

    CHECK(parse_proc_stat_bufs(stat_buf, statm_buf, &stat));
    CHECK(stat.state == 'S');
    CHECK(stat.ppid == 100);
    CHECK(stat.utime == 5);
    CHECK(stat.stime == 2);
    CHECK(stat.num_threads == 1);
    CHECK(stat.vsize == 102400);
    CHECK(stat.statm_resident == 2500);
  }

  SUBCASE("process name with parentheses") {
    // strrchr must find the last ')' to handle names like "(kworker/0:1)"
    char stat_buf[256];
    make_stat_line(stat_buf, sizeof(stat_buf), 456, "my (weird) proc", 'R', 1,
                   10, 3, 2, 65536);
    const char *statm_buf = "1000 500 0 0 0 0 0";

    CHECK(parse_proc_stat_bufs(stat_buf, statm_buf, &stat));
    CHECK(stat.state == 'R');
    CHECK(stat.ppid == 1);
    CHECK(stat.utime == 10);
    CHECK(stat.stime == 3);
    CHECK(stat.num_threads == 2);
    CHECK(stat.statm_resident == 500);
  }

  SUBCASE("zombie process") {
    char stat_buf[256];
    make_stat_line(stat_buf, sizeof(stat_buf), 789, "zombie", 'Z', 1, 0, 0, 1,
                   0);
    const char *statm_buf = "0 0 0 0 0 0 0";

    CHECK(parse_proc_stat_bufs(stat_buf, statm_buf, &stat));
    CHECK(stat.state == 'Z');
    CHECK(stat.vsize == 0);
    CHECK(stat.statm_resident == 0);
  }

  SUBCASE("malformed stat_buf - no closing paren") {
    const char *stat_buf = "999 (truncated";
    const char *statm_buf = "0 0";

    CHECK_FALSE(parse_proc_stat_bufs(stat_buf, statm_buf, &stat));
  }

  SUBCASE("kernel thread - zero utime and stime") {
    char stat_buf[256];
    make_stat_line(stat_buf, sizeof(stat_buf), 2, "kthreadd", 'S', 0, 0, 0, 1,
                   0);
    const char *statm_buf = "0 0 0 0 0 0 0";

    CHECK(parse_proc_stat_bufs(stat_buf, statm_buf, &stat));
    CHECK(stat.utime == 0);
    CHECK(stat.stime == 0);
    CHECK(stat.ppid == 0);
  }

  SUBCASE("nice and starttime") {
    // Fields: state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt
    //   cmajflt utime stime cutime cstime priority nice num_threads itrealvalue
    //   starttime vsize
    const char *stat_buf =
        "1234 (bash) S 100 0 0 0 -1 0 111 50 222 7 5 2 0 0 20 -5 3 0 99 102400";
    const char *statm_buf = "25600 2500 1000 100 0 3000 0";

    CHECK(parse_proc_stat_bufs(stat_buf, statm_buf, &stat));
    CHECK(stat.nice == -5);
    CHECK(stat.utime == 5);
    CHECK(stat.stime == 2);
    CHECK(stat.num_threads == 3);
    CHECK(stat.starttime == 99);
    CHECK(stat.vsize == 102400);
  }
}

// ============================================================================
// parse_io_line Tests
// ============================================================================

TEST_CASE("parse_io_line") {
  ProcessStat stat = {};

  SUBCASE("read_bytes line") {
    parse_io_line("read_bytes: 1024\n", &stat);
    CHECK(stat.io_read_bytes == 1024);
    CHECK(stat.io_write_bytes == 0);
  }

  SUBCASE("write_bytes line") {
    parse_io_line("write_bytes: 512\n", &stat);
    CHECK(stat.io_read_bytes == 0);
    CHECK(stat.io_write_bytes == 512);
  }

  SUBCASE("unrelated key is ignored") {
    parse_io_line("rchar: 99999\n", &stat);
    CHECK(stat.io_read_bytes == 0);
    CHECK(stat.io_write_bytes == 0);
  }

  SUBCASE("large byte counts") {
    parse_io_line("read_bytes: 10737418240\n", &stat); // 10 GB
    CHECK(stat.io_read_bytes == 10737418240ULL);
  }

  SUBCASE("multiple lines accumulate correctly") {
    parse_io_line("rchar: 12345\n", &stat);
    parse_io_line("read_bytes: 1024\n", &stat);
    parse_io_line("write_bytes: 512\n", &stat);
    CHECK(stat.io_read_bytes == 1024);
    CHECK(stat.io_write_bytes == 512);
  }
}

// ============================================================================
// parse_proc_status_uid Tests
// ============================================================================

TEST_CASE("parse_proc_status_uid") {
  uid_t uid = 12345; // sentinel: must stay untouched on failure

  SUBCASE("reads the real uid from a status buffer") {
    const char *status = "Name:\tbash\nState:\tS\nPid:\t42\n"
                         "Uid:\t1000\t1000\t1000\t1000\n"
                         "Gid:\t1000\t1000\t1000\t1000\n";
    CHECK(parse_proc_status_uid(status, &uid));
    CHECK(uid == 1000);
  }

  SUBCASE("real uid differs from effective (setuid)") {
    CHECK(parse_proc_status_uid("Uid:\t1000\t0\t0\t0\n", &uid));
    CHECK(uid == 1000); // real uid, not the effective root
  }

  SUBCASE("missing Uid line leaves the output untouched") {
    CHECK_FALSE(parse_proc_status_uid("Name:\tbash\nState:\tS\n", &uid));
    CHECK(uid == 12345);
  }
}

// ============================================================================
// StringCache Tests
// ============================================================================

TEST_CASE("StringCache") {
  BumpArena arena = BumpArena::create();
  StringCache cache = StringCache::create(&arena);

  SUBCASE("missing key returns a null PersistentString") {
    CHECK(cache.get(42).data == nullptr);
  }

  SUBCASE("set copies the value and get returns it") {
    char src[] = "alice";
    const PersistentString stored = cache.set(7, src);
    src[0] = 'X'; // mutate the source to prove the cache copied
    CHECK(stored.data != src);
    CHECK(strcmp(stored.data, "alice") == 0);
    CHECK(cache.get(7).data == stored.data);
  }

  SUBCASE("repeated set returns the first stored string") {
    const PersistentString first = cache.set(1, "bob");
    const PersistentString second = cache.set(1, "carol");
    CHECK(first.data == second.data);
    CHECK(strcmp(cache.get(1).data, "bob") == 0);
  }

  SUBCASE("growth preserves all entries") {
    constexpr uint32_t N = 500; // forces several grows past INITIAL_CAP
    for (uint32_t i = 0; i < N; ++i) {
      char buf[16];
      snprintf(buf, sizeof(buf), "u%u", i);
      cache.set(i, buf);
    }
    for (uint32_t i = 0; i < N; ++i) {
      char buf[16];
      snprintf(buf, sizeof(buf), "u%u", i);
      const PersistentString got = cache.get(i);
      REQUIRE(got.data != nullptr);
      CHECK(strcmp(got.data, buf) == 0);
    }
  }

  cache.destroy();
  arena.destroy();
}

// ============================================================================
// UsernameResolver Tests
// ============================================================================

TEST_CASE("UsernameResolver") {
  BumpArena arena = BumpArena::create();
  UsernameResolver resolver = UsernameResolver::create(&arena);

  SUBCASE("resolves the current uid to its passwd name") {
    const uid_t uid = geteuid();
    const passwd *pw = getpwuid(uid);
    REQUIRE(pw != nullptr);
    CHECK(strcmp(resolver.resolve(uid).data, pw->pw_name) == 0);
  }

  SUBCASE("same uid hits the cache") {
    const PersistentString first = resolver.resolve(geteuid());
    const PersistentString second = resolver.resolve(geteuid());
    CHECK(first.data == second.data);
  }

  SUBCASE("unknown uid falls back to the numeric string") {
    const uint32_t unknown = 0xFFFFFFFEu;
    if (getpwuid(unknown) == nullptr) {
      CHECK(strcmp(resolver.resolve(unknown).data, "4294967294") == 0);
    }
  }

  resolver.cache.destroy();
  arena.destroy();
}
