#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "base/base.h"
#include "base/channel.h"
#include "base/const_string.h"

// ============================================================================
// BumpArena Tests
// ============================================================================

TEST_CASE("BumpArena basic allocation") {
  BumpArena arena = BumpArena::create();

  SUBCASE("allocates multiple objects continuously") {
    int32_t *a = arena.alloc<int32_t>();
    int32_t *b = arena.alloc<int32_t>();
    int32_t *c = arena.alloc<int32_t>();

    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    CHECK(b - a == 1);
    CHECK(c - a == 2);
  }

  SUBCASE("respects alignment") {
    char *a = arena.alloc<char>();
    double *b = arena.alloc<double>();

    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    // double should be aligned to 8 bytes
    CHECK(reinterpret_cast<uintptr_t>(b) % alignof(double) == 0);
  }

  arena.destroy();
}

TEST_CASE("BumpArena large allocation") {
  BumpArena arena = BumpArena::create();

  // Allocate larger than default slab
  size_t large_size = SLAB_SIZE * 2;
  void *p = arena.alloc_raw(large_size, 1);
  REQUIRE(p != nullptr);
  REQUIRE(arena.cur_slab != nullptr);

  CHECK(arena.cur_slab->left_size == 0);

  arena.destroy();
}

TEST_CASE("BumpArena destroy and reuse") {
  BumpArena arena = BumpArena::create();

  int *p1 = arena.alloc<int>();
  REQUIRE(p1 != nullptr);
  CHECK(arena.cur_slab != nullptr);

  arena.destroy();
  CHECK(arena.cur_slab == nullptr);

  // Should be able to allocate again after destroy
  int *p2 = arena.alloc<int>();
  REQUIRE(p2 != nullptr);
  CHECK(arena.cur_slab != nullptr);

  arena.destroy();
}

// ============================================================================
// Array Tests
// ============================================================================

TEST_CASE("Array creation and access") {
  BumpArena arena = BumpArena::create();

  SUBCASE("creates array with correct size") {
    Array<int> arr = Array<int>::create(arena, 10);
    CHECK(arr.size == 10);
    CHECK(arr.data != nullptr);
  }

  SUBCASE("elements are zero-initialized") {
    Array<int> arr = Array<int>::create(arena, 5);
    for (size_t i = 0; i < arr.size; ++i) {
      CHECK(arr.data[i] == 0);
    }
  }

  SUBCASE("can read and write elements") {
    Array<int> arr = Array<int>::create(arena, 3);
    arr.data[0] = 10;
    arr.data[1] = 20;
    arr.data[2] = 30;

    CHECK(arr.data[0] == 10);
    CHECK(arr.data[1] == 20);
    CHECK(arr.data[2] == 30);
  }

  SUBCASE("zero-size array") {
    Array<int> arr = Array<int>::create(arena, 0);
    CHECK(arr.size == 0);
  }

  arena.destroy();
}

// ============================================================================
// GrowingArray Tests
// ============================================================================

TEST_CASE("GrowingArray basic operations") {
  BumpArena arena = BumpArena::create();

  SUBCASE("emplace_back adds elements") {
    GrowingArray<int> arr = {};
    uint32_t wasted = 0;

    *arr.emplace_back(arena, wasted) = 1;
    *arr.emplace_back(arena, wasted) = 2;
    *arr.emplace_back(arena, wasted) = 3;

    CHECK(arr.size() == 3);
    CHECK(arr.data()[0] == 1);
    CHECK(arr.data()[1] == 2);
    CHECK(arr.data()[2] == 3);
  }

  SUBCASE("grows automatically") {
    GrowingArray<int> arr = {};
    uint32_t wasted = 0;

    // Add many elements to force growth
    for (int i = 0; i < 100; ++i) {
      *arr.emplace_back(arena, wasted) = i;
    }

    CHECK(arr.size() == 100);
    for (int i = 0; i < 100; ++i) {
      CHECK(arr.data()[i] == i);
    }
  }

  SUBCASE("tracks wasted bytes on realloc") {
    GrowingArray<int> arr = {};
    uint32_t wasted = 0;

    // Force multiple reallocations
    for (int i = 0; i < 20; ++i) {
      *arr.emplace_back(arena, wasted) = i;
    }

    // Should have wasted some bytes due to reallocations
    // Initial capacity is 4, then 8, then 16, then 32
    // Wasted: 4*4 + 8*4 + 16*4 = 112 bytes
    CHECK(wasted > 0);
  }

  SUBCASE("shrink_to reduces size") {
    GrowingArray<int> arr = {};
    uint32_t wasted = 0;

    for (int i = 0; i < 10; ++i) {
      *arr.emplace_back(arena, wasted) = i;
    }

    arr.shrink_to(5);
    CHECK(arr.size() == 5);

    // First 5 elements should be unchanged
    for (int i = 0; i < 5; ++i) {
      CHECK(arr.data()[i] == i);
    }
  }

  SUBCASE("shrink_to with larger size does nothing") {
    GrowingArray<int> arr = {};
    uint32_t wasted = 0;

    for (int i = 0; i < 5; ++i) {
      *arr.emplace_back(arena, wasted) = i;
    }

    arr.shrink_to(10);
    CHECK(arr.size() == 5);
  }

  arena.destroy();
}

// ============================================================================
// LinkedList Tests
// ============================================================================

TEST_CASE("LinkedList basic operations") {
  BumpArena arena = BumpArena::create();

  SUBCASE("starts empty") {
    LinkedList<int> list = {};
    CHECK(list.head == nullptr);
    CHECK(list.size == 0);
  }

  SUBCASE("emplace_front adds to head") {
    LinkedList<int> list = {};

    *list.emplace_front(arena) = 1;
    CHECK(list.size == 1);
    CHECK(list.head->value == 1);

    *list.emplace_front(arena) = 2;
    CHECK(list.size == 2);
    CHECK(list.head->value == 2);
    CHECK(list.head->next->value == 1);
  }

  SUBCASE("can iterate through list") {
    LinkedList<int> list = {};

    *list.emplace_front(arena) = 3;
    *list.emplace_front(arena) = 2;
    *list.emplace_front(arena) = 1;

    int expected = 1;
    LinkedNode<int> *node = list.head;
    while (node) {
      CHECK(node->value == expected);
      ++expected;
      node = node->next;
    }
  }

  arena.destroy();
}

// ============================================================================
// Channel Tests
// ============================================================================

TEST_CASE("Channel basic operations") {
  Channel<int, 8> rb = {};

  SUBCASE("starts empty") {
    int out;
    CHECK_FALSE(rb.pop(out));
  }

  SUBCASE("push and pop single element") {
    CHECK(rb.push(42));

    int out;
    CHECK(rb.pop(out));
    CHECK(out == 42);
  }

  SUBCASE("push and pop multiple elements") {
    CHECK(rb.push(1));
    CHECK(rb.push(2));
    CHECK(rb.push(3));

    int out;
    CHECK(rb.pop(out));
    CHECK(out == 1);
    CHECK(rb.pop(out));
    CHECK(out == 2);
    CHECK(rb.pop(out));
    CHECK(out == 3);
  }

  SUBCASE("FIFO ordering") {
    for (int i = 0; i < 5; ++i) {
      CHECK(rb.push(i * 10));
    }

    for (int i = 0; i < 5; ++i) {
      int out;
      CHECK(rb.pop(out));
      CHECK(out == i * 10);
    }
  }

  SUBCASE("full buffer rejects push") {
    // Buffer size is 8, but one slot is always empty (SPSC design)
    // So we can only store 7 elements
    for (int i = 0; i < 7; ++i) {
      CHECK(rb.push(i));
    }
    CHECK_FALSE(rb.push(999)); // Should fail
  }

  SUBCASE("empty after draining") {
    rb.push(1);
    rb.push(2);

    int out;
    rb.pop(out);
    rb.pop(out);

    CHECK_FALSE(rb.pop(out)); // Empty now
  }

  SUBCASE("wrap around works") {
    // Fill partially, drain, refill to test wrap-around
    for (int i = 0; i < 5; ++i) {
      rb.push(i);
    }

    int out;
    for (int i = 0; i < 5; ++i) {
      rb.pop(out);
    }

    // Now push more elements (will wrap around)
    for (int i = 100; i < 107; ++i) {
      CHECK(rb.push(i));
    }

    for (int i = 100; i < 107; ++i) {
      CHECK(rb.pop(out));
      CHECK(out == i);
    }
  }
}

// ============================================================================
// GrowingArray::last_or Tests
// ============================================================================

TEST_CASE("GrowingArray::last_or") {
  BumpArena arena = BumpArena::create();

  SUBCASE("returns default when empty") {
    GrowingArray<int> arr = {};
    CHECK(arr.last_or(42) == 42);
    CHECK(arr.last_or(-1) == -1);
  }

  SUBCASE("returns last element with single element") {
    GrowingArray<int> arr = {};
    uint32_t wasted = 0;
    *arr.emplace_back(arena, wasted) = 99;

    CHECK(arr.last_or(0) == 99);
  }

  SUBCASE("returns last element with multiple elements") {
    GrowingArray<int> arr = {};
    uint32_t wasted = 0;
    *arr.emplace_back(arena, wasted) = 10;
    *arr.emplace_back(arena, wasted) = 20;
    *arr.emplace_back(arena, wasted) = 30;

    CHECK(arr.last_or(0) == 30);
  }

  SUBCASE("returns last after shrink") {
    GrowingArray<int> arr = {};
    uint32_t wasted = 0;
    *arr.emplace_back(arena, wasted) = 10;
    *arr.emplace_back(arena, wasted) = 20;
    *arr.emplace_back(arena, wasted) = 30;

    arr.shrink_to(2);
    CHECK(arr.last_or(0) == 20);
  }

  arena.destroy();
}

// ============================================================================
// BumpArena::alloc_string_copy Tests
// ============================================================================

TEST_CASE("BumpArena::alloc_string_copy") {
  BumpArena arena = BumpArena::create();

  SUBCASE("copies string with explicit length") {
    const char *result = arena.alloc_string_copy("hello", 5);
    CHECK(strcmp(result, "hello") == 0);
  }

  SUBCASE("copies string with auto length") {
    const char *result = arena.alloc_string_copy("world");
    CHECK(strcmp(result, "world") == 0);
  }

  SUBCASE("copies empty string") {
    const char *result = arena.alloc_string_copy("", 0);
    CHECK(result[0] == '\0');
    CHECK(strlen(result) == 0);
  }

  SUBCASE("result is independent of source") {
    char src[] = "original";
    const char *result = arena.alloc_string_copy(src);
    src[0] = 'X';
    CHECK(strcmp(result, "original") == 0);
  }

  SUBCASE("copies partial string with explicit length") {
    const char *result = arena.alloc_string_copy("hello world", 5);
    CHECK(strcmp(result, "hello") == 0);
    CHECK(strlen(result) == 5);
  }

  arena.destroy();
}

// ============================================================================
// lower_bound / bin_search_exact Tests
// ============================================================================

TEST_CASE("lower_bound") {
  SUBCASE("single element - found") {
    int arr[] = {10};
    auto get = [&](size_t i) { return arr[i]; };
    CHECK(lower_bound<int>(1, get, 10) == 0);
  }

  SUBCASE("multiple elements - finds exact value") {
    int arr[] = {10, 20, 30, 40, 50};
    auto get = [&](size_t i) { return arr[i]; };
    CHECK(lower_bound<int>(5, get, 10) == 0);
    CHECK(lower_bound<int>(5, get, 30) == 2);
    CHECK(lower_bound<int>(5, get, 50) == 4);
  }

  SUBCASE("returns index of largest element <= value") {
    int arr[] = {10, 20, 30, 40, 50};
    auto get = [&](size_t i) { return arr[i]; };
    // 25 is between 20 and 30, lower_bound should return index of 20
    CHECK(lower_bound<int>(5, get, 25) == 1);
    CHECK(lower_bound<int>(5, get, 45) == 3);
  }
}

TEST_CASE("bin_search_exact") {
  SUBCASE("finds exact match") {
    int arr[] = {10, 20, 30, 40, 50};
    auto get = [&](size_t i) { return arr[i]; };
    CHECK(bin_search_exact<int>(5, get, 10) == 0);
    CHECK(bin_search_exact<int>(5, get, 30) == 2);
    CHECK(bin_search_exact<int>(5, get, 50) == 4);
  }

  SUBCASE("returns UINT32_MAX for missing values") {
    int arr[] = {10, 20, 30, 40, 50};
    auto get = [&](size_t i) { return arr[i]; };
    CHECK(bin_search_exact<int>(5, get, 5) == UINT32_MAX);
    CHECK(bin_search_exact<int>(5, get, 25) == UINT32_MAX);
    CHECK(bin_search_exact<int>(5, get, 55) == UINT32_MAX);
  }

  SUBCASE("empty array returns UINT32_MAX") {
    auto get = [](size_t) { return 0; };
    CHECK(bin_search_exact<int>(0, get, 10) == UINT32_MAX);
  }

  SUBCASE("single element - not found") {
    int arr[] = {10};
    auto get = [&](size_t i) { return arr[i]; };
    CHECK(bin_search_exact<int>(1, get, 5) == UINT32_MAX);
    CHECK(bin_search_exact<int>(1, get, 15) == UINT32_MAX);
  }
}

// ============================================================================
// Channel Tests (continued)
// ============================================================================

TEST_CASE("Channel with struct type") {
  struct TestData {
    int x;
    int y;
  };

  Channel<TestData, 4> rb = {};

  TestData d1 = {1, 2};
  TestData d2 = {3, 4};

  CHECK(rb.push(d1));
  CHECK(rb.push(d2));

  TestData out;
  CHECK(rb.pop(out));
  CHECK(out.x == 1);
  CHECK(out.y == 2);

  CHECK(rb.pop(out));
  CHECK(out.x == 3);
  CHECK(out.y == 4);
}

// ============================================================================
// InternTable Tests
// ============================================================================

TEST_CASE("InternTable interning") {
  BumpArena arena = BumpArena::create();
  InternTable t = InternTable::create(&arena);

  SUBCASE("same content returns identical pointer") {
    ConstString a = t.intern("hello");
    ConstString b = t.intern("hello");
    CHECK(a.data == b.data);
    CHECK(a == b);
  }

  SUBCASE("content-addressed, not pointer-addressed") {
    char buf1[] = "process";
    char buf2[] = "process";
    REQUIRE(buf1 != buf2); // distinct source buffers
    ConstString a = t.intern(buf1);
    ConstString b = t.intern(buf2);
    CHECK(a.data == b.data);
  }

  SUBCASE("distinct content returns distinct pointers") {
    ConstString a = t.intern("foo");
    ConstString b = t.intern("bar");
    CHECK(a.data != b.data);
    CHECK(a != b);
  }

  SUBCASE("same length, different bytes are distinct") {
    ConstString a = t.intern("abc");
    ConstString b = t.intern("abd");
    CHECK(a.data != b.data);
  }

  SUBCASE("stored value is correct and null-terminated") {
    ConstString a = t.intern("monitor");
    CHECK(a.len == 7);
    CHECK(strcmp(a.data, "monitor") == 0);
    CHECK(a.data[a.len] == '\0');
  }

  SUBCASE("length-aware: interns a substring") {
    ConstString a = t.intern("hello world", 5);
    CHECK(a.len == 5);
    CHECK(strcmp(a.data, "hello") == 0);
    // The strlen overload of the same prefix dedups to the substring.
    ConstString b = t.intern("hello");
    CHECK(a.data == b.data);
  }

  SUBCASE("empty string round-trips") {
    ConstString a = t.intern("");
    ConstString b = t.intern("");
    CHECK(a.len == 0);
    CHECK(a.data[0] == '\0');
    CHECK(a.data == b.data);
  }

  t.destroy();
  arena.destroy();
}

TEST_CASE("InternTable growth preserves identity") {
  BumpArena arena = BumpArena::create();
  InternTable t = InternTable::create(&arena);

  constexpr int N = 1000; // forces several grows past the 256 initial cap
  const char *pointers[N];

  // First pass: intern N unique strings, remember their canonical pointers.
  for (int i = 0; i < N; ++i) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "str_%d", i);
    pointers[i] = t.intern(buf, static_cast<uint32_t>(len)).data;
  }

  CHECK(t.count == N);

  // Second pass: re-interning each must return the exact same pointer, proving
  // rehash during growth preserved every entry.
  for (int i = 0; i < N; ++i) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "str_%d", i);
    ConstString again = t.intern(buf, static_cast<uint32_t>(len));
    CHECK(again.data == pointers[i]);
    CHECK(strcmp(again.data, buf) == 0);
  }

  // No new entries were added on the second pass.
  CHECK(t.count == N);

  t.destroy();
  arena.destroy();
}
