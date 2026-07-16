#include <iostream>
#include <string>
#include "memorycontrol.hpp"

using namespace std;

// ── helper: a non-trivial type to demonstrate NEW_SINGLE ───────────────────
struct Point
{
    int x, y;
    Point() : x(0), y(0) { cout << "  Point() constructed\n"; }
    ~Point() { cout << "  Point(" << x << "," << y << ") destroyed\n"; }
};

int main()
{
    cout << "=== MemoryControl Demo ===\n\n";

    // ── 1. NEW_SINGLE — calls constructor ─────────────────────────────
    cout << "-- 1. NEW_SINGLE (constructor called, but NOT destructor) --\n";
    cout << "     (DELETE_MEMORY uses ::operator delete, which does not\n";
    cout << "      call ~T(). For non-trivial types, manually call\n";
    cout << "      p->~T() before DELETE_MEMORY(p).)\n";
    {
        Point *p = NEW_SINGLE(Point);
        p->x = 10;
        p->y = 20;
        cout << "  Point: (" << p->x << ", " << p->y << ")\n";
        p->~Point();            // manually call destructor for non-trivial type
        DELETE_MEMORY(p);       // then free raw memory
        cout << "  pointer after delete: " << (p ? "non-null" : "nullptr") << "\n";
    }
    cout << endl;

    // ── 2. NEW_MEMORY — raw memory, zero-initialised ──────────────────
    cout << "-- 2. NEW_MEMORY (raw memory, zero-initialised) --\n";
    {
        int *p_int = NEW_MEMORY(int);
        *p_int = 42;
        cout << "  int value: " << *p_int << "\n";
        DELETE_MEMORY(p_int);
    }
    cout << endl;

    // ── 3. CALLOC_ARRAY — array of 2 ints ─────────────────────────────
    cout << "-- 3. CALLOC_ARRAY (array of ints via calloc) --\n";
    {
        int *p_arr = CALLOC_ARRAY(int, 2);
        p_arr[0] = 10;
        p_arr[1] = 20;
        cout << "  array: " << p_arr[0] << ", " << p_arr[1] << "\n";
        DELETE_MEMORY(p_arr);
    }
    cout << endl;

    // ── 4. MemoryControlPtr RAII wrapper ──────────────────────────────
    cout << "-- 4. MemoryControlPtr (RAII, auto-delete on scope exit) --\n";
    {
        MemoryControlPtr<double> ptr(NEW_MEMORY(double));
        *ptr = 3.14;
        cout << "  *ptr = " << *ptr << "\n";
        cout << "  (leaving scope — ptr destructor will free)\n";
    }
    cout << endl;

    // ── 5. MemoryControlPtr move semantics ────────────────────────────
    cout << "-- 5. MemoryControlPtr move semantics --\n";
    {
        MemoryControlPtr<int> a(NEW_SINGLE(int));
        *a = 100;

        MemoryControlPtr<int> b = std::move(a);
        cout << "  a is " << (a ? "non-null" : "null") << "\n";
        cout << "  b is " << (b ? "non-null" : "null") << ", *b = " << *b << "\n";
        cout << "  (leaving scope — b destructor frees)\n";
    }
    cout << endl;

    // ── 6. deliberate leak (detected at exit with summary) ────────────
    cout << "-- 6. Deliberate leak (will be reported at exit) --\n";
    {
        auto *p = CALLOC_MEMORY(double);
        *p = 1.234;
        cout << "  leaked value: " << *p << "\n";
        // NOTE: DELETE_MEMORY(p) intentionally omitted
    }
    cout << endl;

    cout << "=== Done (check leak report below) ===" << endl;
    return 0;
}