#include <iostream>
#include "memorycontrol.hpp"

using namespace std;

int main(int args, char **argv)
{
    cout << "=== MemoryControl Demo ===" << endl;

    // ── 1. allocate via new char[] ──────────────────────────────────────
    int *p_int = NEW_MEMORY(int);
    *p_int = 42;
    cout << "int value: " << *p_int << endl;
    DELETE_MEMORY(p_int);   // safely freed with delete[]

    // ── 2. allocate via calloc ──────────────────────────────────────────
    int *p_arr = CALLOC_MEMORY(int);
    p_arr[0] = 10;
    p_arr[1] = 20;
    cout << "array: " << p_arr[0] << ", " << p_arr[1] << endl;
    DELETE_MEMORY(p_arr);   // safely freed with free()

    // ── 3. deliberate leak (detected at exit) ───────────────────────────
    auto *p_leak = NEW_MEMORY(double);
    *p_leak = 3.14;
    cout << "leaked value: " << *p_leak << " (will be reported at exit)" << endl;
    // NOTE: DELETE_MEMORY(p_leak) intentionally omitted to demonstrate leak detection

    cout << "=== Done ===" << endl;
    return 0;
}