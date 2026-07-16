#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "memorycontrol.hpp"

using namespace std;

// ═══════════════════════════════════════════════════════════════════════════
//  Platform-specific backtrace support
// ═══════════════════════════════════════════════════════════════════════════

#if defined(_WIN32) || defined(_WIN64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

/** Capture up to max_len frames from the current call stack, skipping internal frames. */
int portable_backtrace(void **buffer, int max_len)
{
    typedef USHORT (WINAPI *RtlCaptureStackBackTrace_t)(ULONG, ULONG, PVOID *, PULONG);
    static RtlCaptureStackBackTrace_t pRtl =
        (RtlCaptureStackBackTrace_t)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "RtlCaptureStackBackTrace");
    if (!pRtl) return 0;
    return static_cast<int>(pRtl(0, static_cast<ULONG>(max_len), buffer, nullptr));
}

static char **portable_backtrace_symbols(void *const *buffer, int frames)
{
    char **syms = static_cast<char **>(std::malloc(sizeof(char *) * static_cast<size_t>(frames)));
    if (!syms) return nullptr;

    static bool sym_init = false;
    static HANDLE hProcess = GetCurrentProcess();
    if (!sym_init)
    {
        SymInitialize(hProcess, nullptr, TRUE);
        sym_init = true;
    }

    for (int i = 0; i < frames; ++i)
    {
        char buf[256];
        DWORD64 addr = reinterpret_cast<DWORD64>(buffer[i]);

        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 256];
        auto *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 255;

        DWORD64 disp = 0;
        if (SymFromAddr(hProcess, addr, &disp, sym))
        {
            IMAGEHLP_LINE64 line;
            DWORD lineDisp = 0;
            line.SizeOfStruct = sizeof(line);
            if (SymGetLineFromAddr64(hProcess, addr, &lineDisp, &line))
                snprintf(buf, sizeof(buf), "0x%llX  %s+0x%llX  (%s:%lu)",
                         (unsigned long long)addr, sym->Name,
                         (unsigned long long)disp, line.FileName, line.LineNumber);
            else
                snprintf(buf, sizeof(buf), "0x%llX  %s+0x%llX",
                         (unsigned long long)addr, sym->Name,
                         (unsigned long long)disp);
        }
        else
        {
            snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)addr);
        }

        syms[i] = strdup(buf);
        if (!syms[i])
        {
            for (int j = 0; j < i; ++j) std::free(syms[j]);
            std::free(syms);
            return nullptr;
        }
    }
    return syms;
}

static void portable_free_backtrace_symbols(char **syms)
{
    if (!syms) return;
    std::free(syms);
}

#else // Linux / macOS / POSIX

#include <execinfo.h>

inline int portable_backtrace(void **buffer, int max_len)
{
    return backtrace(buffer, max_len);
}

inline char **portable_backtrace_symbols(void *const *buffer, int frames)
{
    return backtrace_symbols(buffer, frames);
}

inline void portable_free_backtrace_symbols(char **syms)
{
    std::free(syms);
}

#endif // _WIN32

// ═══════════════════════════════════════════════════════════════════════════
//  Platform-specific aligned allocation
// ═══════════════════════════════════════════════════════════════════════════

#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
static inline void *portable_aligned_alloc(size_t align, size_t size)
{
    return _aligned_malloc(size, align);
}
static inline void portable_aligned_free(void *p)
{
    if (p) _aligned_free(p);
}
#else
static inline void *portable_aligned_alloc(size_t align, size_t size)
{
    return std::aligned_alloc(align, size);
}
static inline void portable_aligned_free(void * /*p*/)
{
}
#endif

// ═══════════════════════════════════════════════════════════════════════════
//  Backtrace helpers
// ═══════════════════════════════════════════════════════════════════════════

/* Capture backtrace, skipping the first SKIP_FRAMES internal frames.
   The 0th stored frame will be the caller of capture_bt's caller = user code. */
#define SKIP_FRAMES 2

void capture_bt(void **buf, int max_len, int *out)
{
    /* Request extra frames to discard the internal ones */
    void *tmp[64];
    int n = portable_backtrace(tmp, max_len + SKIP_FRAMES);
    n = (n > SKIP_FRAMES) ? n - SKIP_FRAMES : 0;
    if (n > max_len) n = max_len;
    for (int i = 0; i < n; ++i)
        buf[i] = tmp[i + SKIP_FRAMES];
    *out = n;
}

static void print_backtrace(void *const *bt, int bt_len)
{
    if (bt_len <= 0) return;
    char **sym = portable_backtrace_symbols(bt, bt_len);
    if (!sym) return;
    for (int i = 0; i < bt_len; ++i)
    {
        cout << "    #" << i << "  ";
        if (sym[i]) cout << sym[i];
        else        cout << "0x" << bt[i];
        cout << "\n";
#if defined(_WIN32) || defined(_WIN64)
        std::free(sym[i]);
#endif
    }
    portable_free_backtrace_symbols(sym);
}

// ── statics ─────────────────────────────────────────────────────────────────
memorycontrol::Stripe memorycontrol::m_stripes[MEMORYCONTROL_NUM_STRIPES];
thread_local bool      memorycontrol::t_in_tracking = false;

// ── helper: stripe index (power-of-two-friendly) ───────────────────────────
static inline size_t stripe_idx(const void *p)
{
    return (reinterpret_cast<uintptr_t>(p) >> 4) & (MEMORYCONTROL_NUM_STRIPES - 1);
}

// ── poison ──────────────────────────────────────────────────────────────────
static void poison_fill(void *p, size_t sz)
{
    if (p && sz) std::memset(p, 0xDE, sz);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Singleton
// ═══════════════════════════════════════════════════════════════════════════

memorycontrol *memorycontrol::getInstance()
{
    static memorycontrol inst;
    return &inst;
}

memorycontrol::~memorycontrol()
{
    vector<pair<void *, AllocRecord>> all_leaks;
    size_t total_leaks = 0, total_bytes = 0;
    for (auto &s : m_stripes)
    {
        lock_guard<mutex> lock(s.mtx);
        for (auto &kv : s.map)
        {
            total_leaks++;
            total_bytes += kv.second.size;
            all_leaks.emplace_back(kv.first, kv.second);
        }
    }
    if (total_leaks > 0)
    {
        cout << "\n~memorycontrol LEAK REPORT\n"
             << "  leaked blocks : " << total_leaks << "\n"
             << "  leaked bytes  : " << total_bytes << "\n\n";
        for (auto &kv : all_leaks)
        {
            auto &r = kv.second;
            cout << "  " << kv.first
                 << "  file:" << (r.file ? r.file : "?")
                 << "  line:" << dec << r.line
                 << "  size:" << dec << r.size
                 << "  type:";
            switch (r.type) {
                case AllocType::NewSingle: cout << "new";    break;
                case AllocType::NewArray:  cout << "new[]";  break;
                case AllocType::Calloc:    cout << "calloc"; break;
                case AllocType::Malloc:    cout << "malloc"; break;
            }
            cout << "\n";
            print_backtrace(r.bt, r.bt_len);
        }
        cout << endl;
    }
    else
    {
        cout << "~memorycontrol — memory normal (no leaks)" << endl;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  track_alloc  – called by templates (and global hooks)
// ═══════════════════════════════════════════════════════════════════════════

void memorycontrol::track_alloc(void *p, size_t size, AllocType type,
                                 const char *file, int line,
                                 void **bt, int bt_len)
{
    if (!p) return;
    if (t_in_tracking) return;
    t_in_tracking = true;

    if (m_limit_bytes > 0 && m_current_bytes.load() + size > m_limit_bytes)
    {
        t_in_tracking = false;
        cerr << "memorycontrol: memory cap " << m_limit_bytes << " exceeded" << endl;
        abort();
    }

    AllocRecord rec;
    rec.file = file;
    rec.line = line;
    rec.size = size;
    rec.type = type;
    rec.bt_len = 0;

    if (bt)
    {
        int n = (bt_len > MEMORYCONTROL_BACKTRACE_DEPTH)
                ? MEMORYCONTROL_BACKTRACE_DEPTH : bt_len;
        for (int i = 0; i < n; ++i)
            rec.bt[i] = bt[i];
        rec.bt_len = n;
    }
    else if (m_enable_backtrace)
    {
        capture_bt(rec.bt, MEMORYCONTROL_BACKTRACE_DEPTH, &rec.bt_len);
    }

    {
        auto &stripe = m_stripes[stripe_idx(p)];
        lock_guard<mutex> lock(stripe.mtx);
        stripe.map[p] = rec;
    }

    m_current_bytes.fetch_add(size);
    size_t cur = m_current_bytes.load();
    size_t peak = m_peak_bytes.load();
    while (cur > peak && !m_peak_bytes.compare_exchange_weak(peak, cur))
        ;
    m_num_allocs.fetch_add(1);
    m_total_allocs.fetch_add(1);

    t_in_tracking = false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  untrack_and_free  – called by global operator delete
// ═══════════════════════════════════════════════════════════════════════════

bool memorycontrol::untrack_and_free(void *p)
{
    if (!p) return false;
    if (t_in_tracking) return false;
    t_in_tracking = true;

    auto  &stripe = m_stripes[stripe_idx(p)];
    size_t freed  = 0;
    AllocType atype = AllocType::Malloc;
    bool found = false;

    {
        lock_guard<mutex> lock(stripe.mtx);
        auto it = stripe.map.find(p);
        if (it == stripe.map.end())
        {
            if (m_abort_on_double_free)
            {
                cerr << "memorycontrol: double-free " << p << " — abort" << endl;
                t_in_tracking = false;
                abort();
            }
            t_in_tracking = false;
            return false;
        }
        freed = it->second.size;
        atype = it->second.type;
        stripe.map.erase(it);
        found = true;
    }

    if (m_enable_poison && freed > 0)
        poison_fill(p, freed);

    switch (atype)
    {
    case AllocType::NewSingle:
    case AllocType::Malloc:
        ::operator delete(p);
        break;
    case AllocType::NewArray:
        ::operator delete[](p);
        break;
    case AllocType::Calloc:
        std::free(p);
        break;
    }

    m_current_bytes.fetch_sub(freed);
    m_total_frees.fetch_add(1);
    t_in_tracking = false;
    return found;
}

// ═══════════════════════════════════════════════════════════════════════════
//  delete_memory   – called by DELETE_MEMORY macro
// ═══════════════════════════════════════════════════════════════════════════

int memorycontrol::delete_memory(void *p)
{
    if (!p) return 1;
    if (t_in_tracking) return 1;
    t_in_tracking = true;

    auto  &stripe = m_stripes[stripe_idx(p)];
    size_t freed  = 0;
    AllocType atype = AllocType::Malloc;
    const char *file = nullptr;
    int line = 0;

    {
        lock_guard<mutex> lock(stripe.mtx);
        auto it = stripe.map.find(p);
        if (it == stripe.map.end())
        {
            cout << "delete_memory: not found " << p << endl;
            t_in_tracking = false;
            return 1;
        }
        freed = it->second.size;
        atype = it->second.type;
        file  = it->second.file;
        line  = it->second.line;
        stripe.map.erase(it);
    }

    if (m_enable_poison && freed > 0)
        poison_fill(p, freed);

    switch (atype)
    {
    case AllocType::NewSingle:
    case AllocType::Malloc:
        ::operator delete(p);
        break;
    case AllocType::NewArray:
        ::operator delete[](p);
        break;
    case AllocType::Calloc:
        std::free(p);
        break;
    }

    cout << "release memory: " << p
         << "  file:" << (file ? file : "?")
         << "  line:" << dec << line
         << "  size:" << dec << freed
         << "  succeed" << endl;

    m_current_bytes.fetch_sub(freed);
    m_total_frees.fetch_add(1);
    m_num_allocs.fetch_sub(1);
    t_in_tracking = false;
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Query API
// ═══════════════════════════════════════════════════════════════════════════

MemoryStats memorycontrol::get_stats() const
{
    MemoryStats s;
    s.current_bytes  = m_current_bytes.load();
    s.peak_bytes     = m_peak_bytes.load();
    s.num_allocs     = m_num_allocs.load();
    s.total_allocs   = m_total_allocs.load();
    s.total_frees    = m_total_frees.load();
    return s;
}

void memorycontrol::print_stats() const
{
    auto s = get_stats();
    cout << "── memorycontrol stats ──────────────────────\n"
         << "  current bytes   : " << s.current_bytes << "\n"
         << "  peak bytes      : " << s.peak_bytes << "\n"
         << "  live allocations: " << s.num_allocs << "\n"
         << "  total allocations: " << s.total_allocs << "\n"
         << "  total frees     : " << s.total_frees << "\n"
         << "────────────────────────────────────────────" << endl;
}

void memorycontrol::print_no_release()
{
    for (auto &stripe : m_stripes)
    {
        lock_guard<mutex> lock(stripe.mtx);
        for (auto &kv : stripe.map)
        {
            auto &r = kv.second;
            cout << kv.first
                 << "  file:" << (r.file ? r.file : "?")
                 << "  line:" << r.line
                 << "  size:" << r.size
                 << "  type:";
            switch (r.type) {
                case AllocType::NewSingle: cout << "new";    break;
                case AllocType::NewArray:  cout << "new[]";  break;
                case AllocType::Calloc:    cout << "calloc"; break;
                case AllocType::Malloc:    cout << "malloc"; break;
            }
            cout << "\n";
            print_backtrace(r.bt, r.bt_len);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Global operator new / delete  (MEMORYCONTROL_GLOBAL_HOOK)
// ═══════════════════════════════════════════════════════════════════════════

#ifdef MEMORYCONTROL_GLOBAL_HOOK

static inline void *mc_malloc_impl(size_t size, AllocType type)
{
    void *p = std::malloc(size);
    if (!p && size) throw std::bad_alloc();
    /* Global hooks: no file/line info, capture bt inside track_alloc */
    if (p) memorycontrol::getInstance()->track_alloc(p, size, type, nullptr, 0, nullptr, 0);
    return p;
}

void *operator new(size_t size)                       { return mc_malloc_impl(size, AllocType::Malloc); }
void *operator new(size_t size, const std::nothrow_t &) noexcept
{
    void *p = std::malloc(size);
    if (p) memorycontrol::getInstance()->track_alloc(p, size, AllocType::Malloc, nullptr, 0, nullptr, 0);
    return p;
}
void *operator new[](size_t size)                     { return mc_malloc_impl(size, AllocType::NewArray); }
void *operator new[](size_t size, const std::nothrow_t &) noexcept
{
    void *p = std::malloc(size);
    if (p) memorycontrol::getInstance()->track_alloc(p, size, AllocType::NewArray, nullptr, 0, nullptr, 0);
    return p;
}

void operator delete(void *p) noexcept                { if (p) memorycontrol::getInstance()->untrack_and_free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { if (p) memorycontrol::getInstance()->untrack_and_free(p); }
void operator delete[](void *p) noexcept              { if (p) memorycontrol::getInstance()->untrack_and_free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { if (p) memorycontrol::getInstance()->untrack_and_free(p); }

void *operator new(size_t size, std::align_val_t align)
{
    void *p = portable_aligned_alloc(static_cast<size_t>(align), size);
    if (!p && size) throw std::bad_alloc();
    if (p) memorycontrol::getInstance()->track_alloc(p, size, AllocType::Malloc, nullptr, 0, nullptr, 0);
    return p;
}
void *operator new[](size_t size, std::align_val_t align)
{
    void *p = portable_aligned_alloc(static_cast<size_t>(align), size);
    if (!p && size) throw std::bad_alloc();
    if (p) memorycontrol::getInstance()->track_alloc(p, size, AllocType::NewArray, nullptr, 0, nullptr, 0);
    return p;
}
void operator delete(void *p, std::align_val_t) noexcept   { if (p) memorycontrol::getInstance()->untrack_and_free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { if (p) memorycontrol::getInstance()->untrack_and_free(p); }

#endif // MEMORYCONTROL_GLOBAL_HOOK