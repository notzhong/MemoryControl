# MemoryControl

**轻量级 C++ 内存调试库** — 内存泄漏检测、分配追踪、调用栈回溯，支持 Linux / Windows / macOS 三平台。

## 快速开始

```cpp
#include "memorycontrol.hpp"

int main() {
    int *p = NEW_SINGLE(int);    // 分配并追踪
    *p = 42;
    DELETE_MEMORY(p);            // 释放（自动置 nullptr）

    MemoryControlPtr<double> ptr(NEW_MEMORY(double));  // RAII 自动释放
    *ptr = 3.14;

    // 故意泄漏 — 退出时会报告
    auto *leak = CALLOC_ARRAY(int, 4);
    (void)leak;

    return 0;
}
```

```bash
# 构建
g++ -std=c++17 -g main.cpp memorycontrol.cpp -o demo && ./demo
```

## 构建

### CMake（推荐）

```bash
# 构建库 + demo
cmake -B build && cmake --build build
./build/mc_demo

# 仅构建库（跳过 demo）
cmake -B build -DMEMORYCONTROL_BUILD_TESTS=OFF && cmake --build build
```

### 直接 g++

```bash
g++ -std=c++17 -g -Wall -Wextra main.cpp memorycontrol.cpp -o demo && ./demo
```

## 接入到你的项目

### 方式1：作为子目录

```cmake
add_subdirectory(path/to/MemoryControl)
target_link_libraries(your_target PRIVATE memorycontrol)
```

### 方式2：已安装

```cmake
find_package(MemoryControl REQUIRED)
target_link_libraries(your_target PRIVATE mc::memorycontrol)
```

### 方式3：直接编译

```bash
g++ -std=c++17 -I/path/to/MemoryControl your_file.cpp memorycontrol.cpp -o your_prog
```

## API 参考

### 分配宏

| 宏 | 分配方式 | 释放方式 | 说明 |
|----|---------|---------|------|
| `NEW_SINGLE(t)` | `new (nothrow) t` | `::operator delete` | 调用构造函数 |
| `NEW_MEMORY(t)` | `new char[sizeof(t)]{0}` | `delete[]` | 零初始化原始内存，不调构造 |
| `NEW_ARRAY(t, n)` | `new char[sizeof(t)*n]{0}` | `delete[]` | 数组形式 |
| `CALLOC_MEMORY(t)` | `calloc(1, sizeof(t))` | `free()` | 零初始化 |
| `CALLOC_ARRAY(t, n)` | `calloc(n, sizeof(t))` | `free()` | 数组形式 |
| `DELETE_MEMORY(p)` | — | 自动选择 | 释放后置 `nullptr` |

> **注意**：`NEW_SINGLE` 的析构函数不会被自动调用。对于非平凡类型，先手动 `p->~T()` 再 `DELETE_MEMORY(p)`。

### RAII 智能指针

```cpp
MemoryControlPtr<int> ptr(NEW_SINGLE(int));
*ptr = 42;                           // 解引用
MemoryControlPtr<int> p2 = std::move(ptr);  // 转移所有权
// 离开作用域时自动 DELETE_MEMORY
```

### 运行时查询

```cpp
auto *mc = memorycontrol::getInstance();
mc->print_stats();        // 打印当前/峰值/活跃/总分配/总释放
auto s = mc->get_stats(); // 返回 MemoryStats 结构体
```

### 运行时配置

```cpp
mc->set_enable_backtrace(true);      // 启用调用栈捕获（默认开启）
mc->set_enable_poison(true);         // 释放后填 0xDE 检测 use-after-free
mc->set_abort_on_double_free(true);  // 双指针释放直接 abort
mc->set_limit_bytes(1024*1024*100);  // 内存上限 100MB
```

## 特性

| 特性 | 说明 |
|------|------|
| **64 条 Stripe 分片锁** | 按指针哈希分片，多线程低竞争 |
| **O(1) 查找** | `std::unordered_map` 替代 `std::map` |
| **调用栈回溯** | 记录分配时的调用链（默认 8 层，可配置） |
| **泄漏报告** | 退出时自动打印泄漏总块数、总字节数 |
| **峰值追踪** | 自动记录历史最高内存占用 |
| **毒值填充** | 释放后填充 `0xDE`，检测 use-after-free |
| **双释放检测** | 默认 abort，可关闭 |
| **全局拦截** | 可选 `MEMORYCONTROL_GLOBAL_HOOK` 覆盖所有 `new`/`delete` |
| **Release 模式** | 定义 `MEMORYCONTROL_DISABLE` 后所有宏变为 no-op，零开销 |

## 启用全局拦截

覆盖所有 `new`/`delete`（包括 STL 和三方库的内部分配）：

```bash
cmake -B build -DMEMORYCONTROL_GLOBAL_HOOK=ON && cmake --build build
```

或直接编译：

```bash
g++ -DMEMORYCONTROL_GLOBAL_HOOK -std=c++17 -g main.cpp memorycontrol.cpp -o demo
```

## Release 模式（零开销）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
# 或手动定义
g++ -DMEMORYCONTROL_DISABLE -std=c++17 -O2 main.cpp memorycontrol.cpp -o demo
```

## 泄漏报告解读

### 示例输出

```
~memorycontrol LEAK REPORT
  leaked blocks : 3              ← 泄漏总数
  leaked bytes  : 37             ← 泄漏总字节数

  0x56167f54cd50  file:main.cpp  line:110  size:4  type:new
    #0  ./build/mc_demo(+0x30b9)
    #1  ./build/mc_demo(+0x2c79)
    #2  libc.so.6(__libc_start_main+0x8b)
```

### 逐字段说明

| 字段 | 含义 | 示例 |
|------|------|------|
| `leaked blocks` | 未释放的内存块总数 | `3` 表示有 3 处泄漏 |
| `leaked bytes` | 泄漏的总字节数 | `37` |
| `0x...` | 泄漏内存的起始地址 | 用于调试器对照 |
| `file:...` | 泄漏所在的源文件 | `main.cpp` ← `__FILE__` 自动记录 |
| `line:...` | 泄漏所在的代码行号 | `110` ← `__LINE__` 自动记录 |
| `size:...` | 泄漏的字节数 | `4` 即 `sizeof(int)` |
| `type:...` | 分配方式 | `new`, `new[]`, `calloc` |
| `#0`, `#1`... | 调用栈帧 | 用 `addr2line` 解码为源码位置 |

### 定位泄漏的步骤

#### 第一步：直接看 file:line

`file:main.cpp  line:110` → 打开 `main.cpp` 第 110 行就是泄漏的分配代码。

#### 第二步：看 size + type 确认分配方式

`size:4  type:new` → 是个 `NEW_SINGLE(int)` 或 `new int`，分配了 4 字节。

#### 第三步：解码调用栈（需要时）

```bash
# 从泄漏输出中提取偏移量并解码
addr2line -e ./build/mc_demo -f -C 0x30b9 0x2c79
```

输出示例：
```
main                                    ← 调用者函数名
/home/.../main.cpp:110                  ← 调用者源码位置
```

如果输出为 `??`，说明编译时没加 `-g` 参数，需要重新编译。

#### 批量解码脚本

```bash
# 自动提取 #0 #1 等帧的偏移量并批量解码
./build/mc_demo 2>&1 | grep '^\s\+#' | sed 's/.*(\(+0x[^)]*\)).*/\1/' \
  | xargs addr2line -e ./build/mc_demo -f -C
```

### 实际排查示例

```
泄漏报告：
  leaked blocks : 3
  leaked bytes  : 37

  0x...  file:main.cpp  line:110  size:4  type:new     ← NEW_SINGLE(int)
  0x...  file:main.cpp  line:114  size:32 type:calloc  ← CALLOC_ARRAY(double, 4)
  0x...  file:main.cpp  line:118  size:1  type:new[]   ← NEW_MEMORY(char)
```

每个泄漏的 `file:line` 已经精确指出了代码位置，直接用编辑器打开对应行即可修复。调用栈用于追踪分配发生的完整路径——比如同一个函数被多处调用时，看 `#0` 能知道具体是从哪个调用链进入的。

## 项目结构

```
MemoryControl/
├── memorycontrol.hpp   ← 公开头文件（包含所有宏、模板、RAII）
├── memorycontrol.cpp   ← 唯一需要编译的源文件
├── main.cpp            ← 测试/演示程序
├── CMakeLists.txt      ← 构建系统
├── README.md
└── CLAUDE.md
```

需要编译的只有 **1 个头文件 + 1 个源文件**，无任何外部依赖。

## 平台支持

| 平台 | 编译器 | 状态 |
|------|--------|------|
| Linux | GCC / Clang | ✅ 已验证 |
| Windows | MSVC / MinGW | 代码就绪 |
| macOS | Apple Clang | 代码就绪 |

## License

MIT