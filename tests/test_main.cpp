#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "base.h"
#include "ring_buffer.h"

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
    size_t wasted = 0;

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
    size_t wasted = 0;

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
    size_t wasted = 0;

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
    size_t wasted = 0;

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
    size_t wasted = 0;

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
// RingBuffer Tests
// ============================================================================

TEST_CASE("RingBuffer basic operations") {
  RingBuffer<int, 8> rb = {};

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
    CHECK_FALSE(rb.push(999));  // Should fail
  }

  SUBCASE("empty after draining") {
    rb.push(1);
    rb.push(2);

    int out;
    rb.pop(out);
    rb.pop(out);

    CHECK_FALSE(rb.pop(out));  // Empty now
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

TEST_CASE("RingBuffer with struct type") {
  struct TestData {
    int x;
    int y;
  };

  RingBuffer<TestData, 4> rb = {};

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
