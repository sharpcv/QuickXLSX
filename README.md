# QuickXLSX

轻量、高性能的 C++20 CSV / XLSX 读写库，面向数据清洗与大规模表格处理。设计哲学是「够用就好」：只实现最常用、最通用的数据读写子集，不追求完整 OOXML 兼容，也不关心格式、样式与公式。

## 特性

- **两条独立路径**
  - **DOM 模型**：`Workbook` → `Worksheet` → `Row` → `Cell` → `Value`，完整加载到内存，支持随机访问、A1 区域（`Range`）与惰性视图（`View`）。
  - **流式（Stream）**：`Reader` / `Writer` 逐行处理，内存恒定，适合大文件与顺序读写。
- **内存恒定**：流式读取不物化整个工作表，峰值内存只由 ZIP 缓冲、扫描窗口与单行决定。
- **稀疏工作表**：行、单元格均按需存储，缺失单元格返回 owning 的空值（`Null`），不产生悬空引用。
- **原子写入**：XLSX 先写临时文件，`close()` 时同目录 `rename` 原子替换目标，失败不破坏既有文件。
- **统一 API**：CSV 与 XLSX 使用同一套 `Value` / `Row` / `Reader` / `Writer` 接口。
- **离线可构建**：依赖以 vendored 源码形式置于 `third_party/`，随仓库分发，不依赖 xmake 远程包仓库。

## 构建

依赖：C++20 编译器（GCC 11+ / Clang 14+ / MSVC 19.30+）与 [XMake](https://xmake.io)。

```sh
# 完整构建（XLSX + CSV）
xmake f -y --quickxlsx_xlsx=y
xmake

# 运行测试
xmake run test

# 仅 CSV 构建（不引入 ZIP/XML 依赖）
xmake f -y --quickxlsx_xlsx=n
xmake
```

常用选项（`xmake f --help` 查看全部）：

| 选项 | 默认 | 说明 |
|------|------|------|
| `quickxlsx_xlsx` | `y` | 启用 XLSX 读写（ZIP / DEFLATE / XML 依赖） |
| `quickxlsx_simd` | `y` | 启用 simdutf 加速 UTF-8 / DEFLATE |
| `quickxlsx_native` | `n` | `-march=native`，按本机 CPU 优化（不利于可移植产物） |
| `quickxlsx_benchmarks` | `n` | 构建 `benchmarks/` 下的基准测试 |

## 快速上手

```cpp
#include <quickxlsx/quickxlsx.hpp>
using namespace quickxlsx;
```

### 流式读取

```cpp
Reader reader("data.xlsx");
std::size_t rows = 0, cells = 0;
reader.read_rows([&](const Row& row) {
    ++rows;
    cells += row.size();
});
```

### 流式写入

```cpp
Writer writer("out.csv");   // 扩展名自动选择 CSV / XLSX
writer.write_row("name", "age");
writer.write_row("Alice", 30);
writer.write_row("Bob", 25);
writer.close();
```

### DOM 模型

```cpp
Workbook wb = Workbook::open("data.xlsx");
Worksheet& ws = wb.sheet(0);
for (const Row& row : ws) {
    for (const Cell& cell : row) {
        // cell.column() + cell.value()
    }
}
```

### 读 → 筛选 → 写（流式）

```cpp
Reader reader("input.xlsx");
Writer writer("output.xlsx");
writer.set_sheet_name(reader.sheet_names().front());

reader.read_rows([&](const Row& row) {
    // 第 4 列（0-based 索引 3）包含 "西子电商" 的行
    const Value value = row[3];
    if (value.is_string() &&
        value.as_string_unchecked().find("西子电商") != std::string_view::npos) {
        writer.write_row(row.values());
    }
});
writer.close();
```

## 性能

在 `sample.xlsx`（42.1 万行、880 万单元格）上实测（Linux，便携 `-O3`，3 次取均值）：

| 引擎 | 模式 | 读取耗时 | 峰值内存 |
|------|------|---------|---------|
| **QuickXLSX** | stream | **8.8 s** | **7.4 MiB** |
| QuickXLSX | DOM | 9.5 s | 697 MiB |
| OpenXLSX | DOM | 8.6 s | 4232 MiB |
| xlnt | stream | 35.0 s | 62 MiB |
| openpyxl (calamine) | DOM | 11.4 s | 807 MiB |

流式模式是唯一能以个位数 MiB 内存读完整个工作表的方案。读 → 筛选 → 写（输出 3.2 万行）时，QuickXLSX 全程约 10.2 s / 10 MiB，而 xlnt 退回 DOM 写需 40 s / 753 MiB，OpenXLSX 的 DOM 写则因逐格线性扫描在数十分钟内无法完成。

## 目录结构

```
include/quickxlsx/  公共头文件（Cell / Value / Row / Worksheet / Workbook / Reader / Writer / Range / View）
src/                实现源码（含流式 XLSX 解析与原子写）
tests/              单元测试（Value / Range / View / CSV / XLSX）
benchmarks/         与 OpenXLSX、xlnt、openpyxl(calamine) 的对比基准
third_party/        vendored 依赖源码与 xmake 包定义
```

## 许可证

MIT。`third_party/` 中的依赖适用各自许可证，分发时需一并保留。
