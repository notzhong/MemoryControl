#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <cassert>
#include "memorycontrol.hpp"

using namespace std;

/* ── helper: custom type with non-trivial destructor ────────────────────── */
struct Point
{
    int x, y;
    Point() : x(0), y(0) { cout << "  Point()\n"; }
    ~Point() { cout << "  ~Point(" << x << "," << y << ")\n"; }
};

/* ── helper: multi-threaded allocator ────────────────────────────────────── */
static void worker_thread(int id)
{
    for (int i = 0; i < 50; ++i)
    {
        int *p = NEW_SINGLE(int);
        *p = id * 1000 + i;
        DELETE_MEMORY(p);
    }
}

int main()
{
    auto *mc = memorycontrol::getInstance();

    cout << "══════════════════════════════════════════════\n"
         << "  MemoryControl v2 — Large Project Debugger\n"
         << "══════════════════════════════════════════════\n\n";

    // ─────────────────────────────────────────────────────────────────────
    // 1. Manual macros — all allocation styles
    // ─────────────────────────────────────────────────────────────────────
    cout << "── 1. Manual macros ───────────────────────────\n";

    // 1a. NEW_SINGLE with manual destructor call
    {
        Point *p = NEW_SINGLE(Point);
        p->x = 10; p->y = 20;
        cout << "    Point: (" << p->x << ", " << p->y << ")\n";
        p->~Point();            // manual dtor for non-trivial type
        DELETE_MEMORY(p);
        assert(p == nullptr);   // macro sets to nullptr
    }

    // 1b. NEW_MEMORY (zero-init, no ctor)
    {
        int *p = NEW_MEMORY(int);
        *p = 42;
        assert(*p == 42);
        DELETE_MEMORY(p);
    }

    // 1c. CALLOC_ARRAY (zero-init array)
    {
        int *arr = CALLOC_ARRAY(int, 4);
        arr[0] = 1; arr[1] = 2; arr[2] = 3; arr[3] = 4;
        for (int i = 0; i < 4; ++i) assert(arr[i] == i + 1);
        DELETE_MEMORY(arr);
    }

    // 1d. RAII MemoryControlPtr
    {
        MemoryControlPtr<double> ptr(NEW_MEMORY(double));
        *ptr = 2.718;
        cout << "    RAII *ptr = " << *ptr << " (auto-freed)\n";
    }

    cout << endl;

    // ─────────────────────────────────────────────────────────────────────
    // 2. Runtime query API
    // ─────────────────────────────────────────────────────────────────────
    cout << "── 2. Runtime stats (mid-demo) ─────────────────\n";
    mc->print_stats();
    cout << endl;

    // ─────────────────────────────────────────────────────────────────────
    // 3. Multi-threaded allocation
    // ─────────────────────────────────────────────────────────────────────
    cout << "── 3. Multi-threaded (4 workers × 50 allocs each) ─\n";
    {
        thread t1(worker_thread, 1);
        thread t2(worker_thread, 2);
        thread t3(worker_thread, 3);
        thread t4(worker_thread, 4);
        t1.join(); t2.join(); t3.join(); t4.join();
    }
    cout << "    (all 200 allocations succeeded)\n\n";

    // ─────────────────────────────────────────────────────────────────────
    // 4. Runtime stats after multi-threaded work
    // ─────────────────────────────────────────────────────────────────────
    cout << "── 4. Stats after multi-threaded work ──────────\n";
    mc->print_stats();
    cout << endl;

    // ─────────────────────────────────────────────────────────────────────
    // 5. Deliberate leak (detected at exit with backtrace)
    // ─────────────────────────────────────────────────────────────────────
    cout << "── 5. Deliberate leak (will appear in report) ──\n";
    {
        // Create 3 leaks of different types
        auto *leak1 = NEW_SINGLE(int);
        *leak1 = 100;
        // no DELETE_MEMORY

        auto *leak2 = CALLOC_ARRAY(double, 4);
        leak2[0] = 1.1; leak2[1] = 2.2;
        // no DELETE_MEMORY

        auto *leak3 = NEW_MEMORY(char);
        *leak3 = 'Z';
        // no DELETE_MEMORY

        cout << "    created 3 leaked allocations\n";
        (void)leak1; (void)leak2; (void)leak3;   // suppress unused warnings
    }

    // ─────────────────────────────────────────────────────────────────────
    // 6. Final stats before exit
    // ─────────────────────────────────────────────────────────────────────
    cout << "\n── 6. Final stats (3 leaks still live) ─────────\n";
    mc->print_stats();
    // Turn on poison for remaining demos (not needed here, just showing API)
    mc->set_enable_poison(true);
    // Disable abort on double-free for demo safety
    mc->set_abort_on_double_free(false);

    cout << "══════════════════════════════════════════════\n"
         << "  Exiting — leak report follows\n"
         << "══════════════════════════════════════════════\n";
    return 0;
}