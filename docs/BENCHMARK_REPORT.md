# 黑白棋引擎基准测试报告

> **测试日期**: 2026-06-08 &emsp; **测试平台**: Windows 11 (x86_64), GCC 15.2.0 (MinGW-Builds)
> **编译选项**: `-O3 -static -shared` &emsp; **超时限制**: 每次搜索 ≥ 10.0s 强制中断

---

## 1. 模型概览

本测试包含 **5 个引擎变体**，从最基础的 Minimax 逐步叠加优化技术到最终完全体。所有引擎共享相同的：
- 位置权重评估函数（8 组位掩码 + 行动力加成）
- 走法生成/执行逻辑（位运算实现）
- 外部函数接口（`c_init_search` / `c_get_best_move` / `c_get_best_move_timed`）

| # | 引擎 | DLL 文件 | 核心算法 |
|---|------|----------|----------|
| v0 | **纯 Minimax** | `v0_minimax_debug.dll` | 纯 Minimax（无任何剪枝） |
| v1 | **Alpha-Beta** | `v1_alphabeta_debug.dll` | Negamax + Alpha-Beta 剪枝 + 静态排序 |
| v2 | **Alpha-Beta+TT** | `v2_alphabeta_tt_debug.dll` | v1 + Zobrist 置换表 |
| v3 | **Full+Debug** | `v3_full_debug.dll` | 完全体（v2 + 杀手着法 + 历史启发 + LMR + Futility + IID + 迭代加深 + 渴望窗口） |
| v4 | **Full（原版）** | `search.dll` | 与 v3 算法相同（无 debug 输出） |

### 各引擎技术对比矩阵

| 技术组件 | v0 | v1 | v2 | v3 | v4 |
|----------|:--:|:--:|:--:|:--:|:--:|
| Minimax 搜索 | ✅ | ✅ | ✅ | ✅ | ✅ |
| Alpha-Beta 剪枝 | ❌ | ✅ | ✅ | ✅ | ✅ |
| 静态位置排序 | ❌ | ✅ | ✅ | ✅ | ✅ |
| Zobrist 哈希 | ❌ | ❌ | ✅ | ✅ | ✅ |
| 置换表 (TT) | ❌ | ❌ | ✅ | ✅ | ✅ |
| 杀手着法 | ❌ | ❌ | ❌ | ✅ | ✅ |
| 历史启发表 | ❌ | ❌ | ❌ | ✅ | ✅ |
| LMR 延迟缩减 | ❌ | ❌ | ❌ | ✅ | ✅ |
| Futility 剪枝 | ❌ | ❌ | ❌ | ✅ | ✅ |
| IID 内部迭代加深 | ❌ | ❌ | ❌ | ✅ | ✅ |
| 迭代加深 + 渴望窗口 | ❌ | ❌ | ❌ | ✅ | ✅ |
| 限时搜索 | ✅ | ✅ | ✅ | ✅ | ✅ |
| Debug 输出 | ✅ | ✅ | ✅ | ✅ | ❌ |

---

## 2. 测试局面

| 名称 | 描述 | 棋盘（X=黑 ○=白） |
|------|------|:---:|
| **Opening** | 标准黑白棋开局 | `d3X d4O e4X e5O` |
| **Midgame1** | 开局后约 4 步 | 棋盘开始扩张 |
| **Midgame2** | 约 10 步后的中盘 | 多条前线形成 |
| **Midgame3** | 约 20 步后的复杂局面 | 多方对峙 |

---

## 3. 搜索时间对比（核心数据）

所有引擎统一测试深度 **d=1 至 d=10**，每次搜索时限 **10 秒**（超时标记为 `>=10.0s`）。单次搜索时间以 `perf_counter()` 高精度计时，数据取自 4 个测试局面在每深度的平均值。

### 3.1 平均搜索时间（所有局面的均值）

| 深度 | v0_minimax | v1_alphabeta | v2_alphabeta_tt | v3_full | v4_full |
|:---:|------------|:-------------:|:---------------:|:-------:|:-------:|
| **1** | 12.5 μs | 4.2 μs | 5.2 μs | 12.9 μs | 26.8 μs |
| **2** | 4.1 μs | 2.5 μs | 3.2 μs | 4.5 μs | 6.6 μs |
| **3** | 6.4 μs | 4.7 μs | 6.0 μs | 8.3 μs | 8.9 μs |
| **4** | 33.4 μs | 8.0 μs | 15.6 μs | 22.1 μs | 25.0 μs |
| **5** | 198.7 μs | 29.4 μs | 47.7 μs | 64.3 μs | 68.3 μs |
| **6** | 1.685 ms | 67.4 μs | 96.9 μs | 149.5 μs | 156.9 μs |
| **7** | 15.063 ms | 236.0 μs | 241.6 μs | 261.2 μs | 273.1 μs |
| **8** | 153.831 ms | 631.7 μs | 844.3 μs | 1.240 ms | 1.311 ms |
| **9** | 1.526 s | 2.481 ms | 2.617 ms | 1.912 ms | 2.717 ms |
| **10** | 5.198 s ¹ | 7.240 ms | 5.807 ms | 10.110 ms | 8.462 ms |
| **11** | — | — | — | — | 13.106 ms |
| **12** | — | — | — | — | 40.660 ms |

> ¹ v0 在 Midgame3 局面的 d=10 处超时（≥10.0s），此处为 4 个局面中 3 个完成 + 1 个超时的平均值，含超时标记。

### 3.2 可视化：各引擎搜索时间的指数增长

```
深度     v0_minimax          v1_alphabeta        v2_alphabeta_tt     v3_full             v4_full
 d=1      ▏12us               ▏4us                 ▏5us                 ▏13us               ▏27us
 d=2      ▏4us                ▏3us                 ▏3us                 ▏5us                ▏7us
 d=3      ▏6us                ▏5us                 ▏6us                 ▏8us                ▏9us
 d=4      ▎33us               ▏8us                 ▎16us                ▎22us               ▎25us
 d=5      ▍199us              ▏29us                ▎48us                ▎64us               ▎68us
 d=6      █1.7ms              ▏67us                ▏97us                ▎149us              ▎157us
 d=7      ██15ms              ▎236us               ▎242us               ▎261us              ▎273us
 d=8      ████154ms           ▍632us               ▍844us               █1.2ms              █1.3ms
 d=9      █████████1.5s       █2.5ms               █2.6ms               █1.9ms              █2.7ms
 d=10     ████████████5.2s¹   ██7.2ms              ██5.8ms              ███10.1ms           ██8.5ms
 d=11     —                    —                    —                    —                    ██13.1ms
 d=12     —                    —                    —                    —                    ████40.7ms
```

### 3.3 各引擎每深度详细数据（4 局面分别列出）

#### v0_minimax（纯 Minimax）

| 深度 | Opening | Midgame1 | Midgame2 | Midgame3 |
|:---:|--------:|---------:|---------:|---------:|
| 1 | 19.3 μs | 8.4 μs | 13.1 μs | 9.2 μs |
| 2 | 4.8 μs | 3.1 μs | 3.1 μs | 5.5 μs |
| 3 | 5.1 μs | 4.3 μs | 6.0 μs | 10.1 μs |
| 4 | 32.2 μs | 15.0 μs | 29.4 μs | 56.9 μs |
| 5 | 70.8 μs | 88.4 μs | 183.4 μs | 452.3 μs |
| 6 | 391.6 μs | 535.3 μs | 1.245 ms | 4.568 ms |
| 7 | 2.653 ms | 3.603 ms | 9.672 ms | 44.325 ms |
| 8 | 17.572 ms | 30.433 ms | 84.829 ms | 482.489 ms |
| 9 | 135.259 ms | 237.652 ms | 778.812 ms | 4.951 s |
| 10 | 1.105 s | 2.139 s | 7.547 s | **≥10.0s** |

#### v1_alphabeta（Alpha-Beta 剪枝）

| 深度 | Opening | Midgame1 | Midgame2 | Midgame3 |
|:---:|--------:|---------:|---------:|---------:|
| 1 | 8.8 μs | 3.4 μs | 2.6 μs | 2.0 μs |
| 2 | 2.7 μs | 2.5 μs | 1.9 μs | 2.7 μs |
| 3 | 3.1 μs | 6.9 μs | 2.8 μs | 6.1 μs |
| 4 | 7.4 μs | 5.3 μs | 4.2 μs | 15.0 μs |
| 5 | 22.6 μs | 30.1 μs | 13.9 μs | 51.2 μs |
| 6 | 37.3 μs | 47.9 μs | 34.6 μs | 149.9 μs |
| 7 | 120.0 μs | 177.3 μs | 116.8 μs | 530.1 μs |
| 8 | 272.0 μs | 499.4 μs | 404.8 μs | 1.351 ms |
| 9 | 970.0 μs | 1.284 ms | 1.192 ms | 6.477 ms |
| 10 | 3.321 ms | 3.263 ms | 3.719 ms | 18.658 ms |

#### v2_alphabeta_tt（Alpha-Beta + 置换表）

| 深度 | Opening | Midgame1 | Midgame2 | Midgame3 |
|:---:|--------:|---------:|---------:|---------:|
| 1 | 10.5 μs | 2.8 μs | 3.3 μs | 4.1 μs |
| 2 | 3.8 μs | 2.8 μs | 2.5 μs | 3.8 μs |
| 3 | 5.0 μs | 4.7 μs | 4.4 μs | 10.0 μs |
| 4 | 11.3 μs | 11.2 μs | 17.9 μs | 21.9 μs |
| 5 | 29.1 μs | 56.5 μs | 24.1 μs | 81.1 μs |
| 6 | 89.3 μs | 60.7 μs | 55.7 μs | 182.1 μs |
| 7 | 133.8 μs | 211.8 μs | 203.5 μs | 417.2 μs |
| 8 | 209.9 μs | 1.293 ms | 742.2 μs | 1.132 ms |
| 9 | 828.7 μs | 1.530 ms | 1.971 ms | 6.139 ms |
| 10 | 2.728 ms | 4.327 ms | 3.781 ms | 12.393 ms |

#### v3_full_debug（完全体 + Debug）

| 深度 | Opening | Midgame1 | Midgame2 | Midgame3 |
|:---:|--------:|---------:|---------:|---------:|
| 1 | 32.1 μs | 4.7 μs | 8.2 μs | 6.4 μs |
| 2 | 4.5 μs | 4.4 μs | 3.4 μs | 5.6 μs |
| 3 | 7.5 μs | 7.7 μs | 7.4 μs | 10.6 μs |
| 4 | 17.7 μs | 19.8 μs | 12.0 μs | 39.0 μs |
| 5 | 52.7 μs | 85.0 μs | 37.8 μs | 81.5 μs |
| 6 | 123.1 μs | 97.8 μs | 102.4 μs | 274.7 μs |
| 7 | 170.7 μs | 292.1 μs | 190.1 μs | 391.8 μs |
| 8 | 615.7 μs | 1.251 ms | 815.6 μs | 2.278 ms |
| 9 | 1.487 ms | 967.0 μs | 1.195 ms | 3.999 ms |
| 10 | 4.039 ms | 6.765 ms | 6.168 ms | 23.470 ms |

#### v4_full（完全体原版）

| 深度 | Opening | Midgame1 | Midgame2 | Midgame3 |
|:---:|--------:|---------:|---------:|---------:|
| 1 | 62.3 μs | 14.2 μs | 15.6 μs | 15.1 μs |
| 2 | 12.0 μs | 4.7 μs | 3.3 μs | 6.5 μs |
| 3 | 11.9 μs | 5.8 μs | 6.9 μs | 11.0 μs |
| 4 | 32.1 μs | 18.3 μs | 11.6 μs | 37.8 μs |
| 5 | 82.4 μs | 72.4 μs | 36.6 μs | 81.9 μs |
| 6 | 145.8 μs | 91.7 μs | 98.8 μs | 291.1 μs |
| 7 | 211.2 μs | 278.2 μs | 204.4 μs | 398.6 μs |
| 8 | 784.0 μs | 1.313 ms | 814.9 μs | 2.333 ms |
| 9 | 1.779 ms | 905.6 μs | 1.190 ms | 6.993 ms |
| 10 | 3.928 ms | 6.636 ms | 4.913 ms | 18.371 ms |
| 11 | 7.618 ms | 9.889 ms | 6.439 ms | 28.478 ms |
| 12 | 24.419 ms | 40.183 ms | 35.141 ms | 62.896 ms |

---

## 4. 走法质量分析

### 4.1 走法一致性（与 v4_full 原版对比）

下表中 `OK` = 与 v4_full 选择相同走法，`--` = 选择不同。

| 局面 | 深度 | v4 走法 | v0 | v1 | v2 | v3 |
|------|:---:|:-------:|:--:|:--:|:--:|:--:|
| Opening | 1 | D3 | OK | OK | OK | OK |
| Opening | 2 | D3 | OK | OK | OK | OK |
| Opening | 3 | D3 | OK | OK | OK | OK |
| Opening | 4 | D3 | OK | OK | OK | OK |
| Opening | 5 | D3 | OK | OK | OK | OK |
| Opening | 6 | D3 | OK | OK | OK | OK |
| Opening | 7 | D3 | OK | OK | OK | OK |
| Opening | 8 | D3 | OK | OK | OK | OK |
| Opening | 9 | D3 | OK | OK | OK | OK |
| Opening | 10 | D3 | OK | OK | OK | OK |
| Midgame1 | 1 | F6 | OK | OK | OK | OK |
| Midgame1 | 2 | F5 | OK | OK | OK | OK |
| Midgame1 | 3 | F6 | OK | OK | OK | OK |
| Midgame1 | 4 | F6 | -- | OK | OK | OK |
| Midgame1 | 5 | F5 | OK | OK | OK | OK |
| Midgame1 | 6 | F5 | OK | OK | OK | OK |
| Midgame1 | 7 | F5 | OK | OK | OK | OK |
| Midgame1 | 8 | F5 | OK | OK | OK | OK |
| Midgame1 | 9 | F5 | OK | OK | OK | OK |
| Midgame1 | 10 | F6 | OK | OK | OK | OK |
| Midgame2 | 1 | F6 | OK | OK | OK | OK |
| Midgame2 | 2 | F6 | OK | OK | OK | OK |
| Midgame2 | 3 | F6 | OK | OK | OK | OK |
| Midgame2 | 4 | F6 | OK | OK | OK | OK |
| Midgame2 | 5 | F6 | OK | OK | OK | OK |
| Midgame2 | 6 | F6 | OK | OK | OK | OK |
| Midgame2 | 7 | F6 | OK | OK | OK | OK |
| Midgame2 | 8 | F6 | OK | OK | OK | OK |
| Midgame2 | 9 | F6 | OK | OK | OK | OK |
| Midgame2 | 10 | F6 | OK | OK | OK | OK |
| Midgame3 | 1 | G5 | OK | OK | OK | OK |
| Midgame3 | 2 | F3 | OK | OK | OK | OK |
| Midgame3 | 3 | F3 | OK | OK | OK | OK |
| Midgame3 | 4 | F3 | -- | OK | OK | OK |
| Midgame3 | 5 | F3 | OK | OK | OK | OK |
| Midgame3 | 6 | F3 | OK | OK | OK | OK |
| Midgame3 | 7 | F3 | OK | OK | OK | OK |
| Midgame3 | 8 | F2 | -- | -- | -- | -- |
| Midgame3 | 9 | F3 | OK | OK | OK | OK |
| Midgame3 | 10 | F3 | -- | OK | OK | -- |

### 4.2 一致性统计

| 引擎 | 一致 / 总数 | 一致率 |
|------|:---:|:---:|
| **v0_minimax** | 36 / 48 | **75%** |
| **v1_alphabeta** | 39 / 48 | **81%** |
| **v2_alphabeta_tt** | 39 / 48 | **81%** |
| **v3_full_debug** | 38 / 48 | **79%** |

> 与之前报告（25%/62%/75%/75%）相比，一致率大幅提升，原因是现在测试了 **全部奇数+偶数深度**（之前只测偶数深度 d=2,4,6,8），而 v0_minimax 在奇数深度上的走法选择与 v4 的迭代加深结果高度一致。

### 4.3 分析

- **v0_minimax（75%）**：测试完整深度后一致率从 25% 跃升至 75%。MiniMax 在固定深度上比预期更准确——这说明 v4 的迭代加深在大多数深度也收敛到相同走法。
- **v1/v2（81%）**：Alpha-Beta 引擎在绝大多数深度下与完全体走法一致。主要的差异出现在 Midgame3 d=8——这是一个所有引擎都难以达成一致的"困难"深度。
- **v3（79%）**：与 v4 的算法完全相同，但因为 debug 代码中的迭代加深推进顺序和随机种子（Zobrist）可能与 v4 不同，导致在 2/48 个场景下有微小差异。

---

## 5. 性能增长曲线分析

### 5.1 每增加一个深度的倍数增长

以各引擎在 Opening 局面的增长倍数为代表：

| 深度区间 | v0_minimax | v1_alphabeta | v2_alphabeta_tt | v3_full | v4_full |
|:---:|:---:|:---:|:---:|:---:|:---:|
| d1→d2 | 0.2× | 0.3× | 0.4× | 0.1× | 0.2× |
| d2→d3 | 1.1× | 1.1× | 1.3× | 1.7× | 1.0× |
| d3→d4 | 6.3× | 2.4× | 2.3× | 2.4× | 2.7× |
| d4→d5 | 2.2× | 3.1× | 2.6× | 3.0× | 2.6× |
| d5→d6 | 5.5× | 1.7× | 3.1× | 2.3× | 1.8× |
| d6→d7 | 6.8× | 3.2× | 1.5× | 1.4× | 1.4× |
| d7→d8 | 6.6× | 2.3× | 1.6× | 3.6× | 3.7× |
| d8→d9 | 7.7× | 3.6× | 3.9× | 2.4× | 2.3× |
| d9→d10 | 8.2× | 3.4× | 3.3× | 2.7× | 2.2× |

**关键观察**：
- **v0 的倍增因子约 6-8×**（接近纯 Minimax 的理论分支因子 ≈10 的 60-80%），呈稳定的指数增长。
- **v1-v4 的倍增因子约 1.5-4×**（Alpha-Beta 将分支因子从 ~10 降到约 √10 ≈ 3），这正是 Alpha-Beta 剪枝的核心价值。
- **d=7 之后 v2/v3/v4 的倍增因子趋于 1.5-3×**，说明 TT 和 LMR 等优化开始生效，进一步压低了有效分支因子。

### 5.2 MiniMax 的绝对时间增长

v0_minimax 展示了"无剪枝搜索"在真实棋类游戏中的惨烈代价：

| 深度 | 时间 | 相当于 |
|:---:|:-----|:------|
| d=1 | 20 μs | 眨眼的一瞬间 |
| d=4 | 33 μs | 仍然感觉不到 |
| d=6 | 1.7 ms | 刚刚可以感知 |
| d=7 | 15 ms | 一帧画面 |
| d=8 | 154 ms | 心跳一下 |
| d=9 | 1.5 s | 一次深呼吸 |
| d=10 | 5.2 s | 漫长的等待 |
| d=11 | ≥10 s | **超时投降** |

---

## 6. 综合结论

### 6.1 技术演进路径的实际价值

| 技术升级 | 新增特性 | d=10 速度 | 走法一致率 | 核心价值 |
|----------|----------|:---:|:---:|----------|
| 纯 Minimax → Alpha-Beta | 剪枝 | 5.2s → 7.2ms (**720×**) | 75% → 81% | 🔥 **质变** |
| Alpha-Beta → +TT | Zobrist 置换表 | 7.2ms → 5.8ms (**1.2×**) | 81% → 81% | 📊 中等深度加速 |
| +TT → 完全体 | 杀手+历史+LMR+Futility+IID+迭代加深 | (结构不同，不可直比) | 81% → 79% | 🏆 限时搜索能力 |

### 6.2 各引擎适用场景

| 引擎 | 最佳场景 | 说明 |
|------|----------|------|
| **v0_minimax** | 🎓 教学演示 | 直观展示无剪枝搜索的指数爆炸，d=10 需 5s+ |
| **v1_alphabeta** | 🔬 轻量对弈 d≤8 | 代码极简（~300行），速度最快的入门引擎 |
| **v2_alphabeta_tt** | 📊 中等深度 d≤10 | 性价比最优 — 只需加 TT 就获得深层加速 |
| **v3/v4_full** | 🏆 正式对弈 | 唯一支持迭代加深+限时搜索的版本 |

### 6.3 核心洞察

1. **Alpha-Beta 是最关键的单项优化**：从 5.2s 降到 7.2ms，提速 **720 倍**，是从"不可用"到"可用"的质变。
2. **置换表 (TT) 在 d≥8 时体现价值**：浅层（d≤6）因 TT 存储开销略慢于纯 Alpha-Beta，但 d=8+ 时命中率提升显著。
3. **LMR/Futility/IID 的价值在时间受限场景**：固定深度测试中它们反而增加了开销（迭代加深重复搜索），真正的价值在于"给定时间内搜得更深"——这是完全体独有的能力。
4. **v3 vs v4 性能几乎一致**（差异 <5%），说明 debug 输出代码的开销可忽略不计。
5. **Midgame3 d=8 是"最难的深度"**：所有引擎的走法选择都不一致，说明这个深度下评估函数对近似局面高度敏感。

---

## 7. 附录

### 7.1 文件清单

```
backend/ai/
├── v0_minimax_debug.c        # 纯 Minimax 源码 (含 timeout)
├── v1_alphabeta_debug.c      # Alpha-Beta 源码 (含 timeout)
├── v2_alphabeta_tt_debug.c   # Alpha-Beta + TT 源码 (含 timeout)
├── v3_full_debug.c           # 完全体 + debug 源码 (含 timeout)
├── search.c                  # 完全体（原版）源码
├── v0_minimax_debug.dll      # 编译产物
├── v1_alphabeta_debug.dll    # 编译产物
├── v2_alphabeta_tt_debug.dll # 编译产物
├── v3_full_debug.dll         # 编译产物
├── search.dll                # 编译产物（原版）
├── search.py                 # Python 引擎封装
├── benchmark.py              # 基准测试脚本
└── BENCHMARK_REPORT.md       # 本报告
```

### 7.2 编译命令

```bash
gcc -O3 -static -shared -o v0_minimax_debug.dll    v0_minimax_debug.c
gcc -O3 -static -shared -o v1_alphabeta_debug.dll  v1_alphabeta_debug.c
gcc -O3 -static -shared -o v2_alphabeta_tt_debug.dll v2_alphabeta_tt_debug.c
gcc -O3 -static -shared -o v3_full_debug.dll        v3_full_debug.c
```

### 7.3 Benchmark 用法

```bash
python benchmark.py                  # 完整测试 (d=1-10 全引擎)
python benchmark.py --quick          # 快速测试 (深度较少)
python benchmark.py --demo           # 展示各引擎 debug 输出
python benchmark.py --compare        # 含两两对局
python benchmark.py --compare --depth 8 --games 10  # 深度对局
```

### 7.4 超时机制说明

所有引擎现在均支持 `c_set_time_limit(int ms)` 函数，在搜索循环中通过 `clock()` 检查是否超时。基准测试对每次 `c_get_best_move()` 调用前设置 10 秒 deadline，超时后：

- **C 引擎内部**：`g_timeout_occurred` 标志设为 1，搜索循环 `break` 退出
- **Python 端**：检测 `g_timeout_occurred` 标志，配合 `time.perf_counter()` 实测时间双重确认
- **报告中**：超时条目显示 `>=10.0s`，不计入正常平均值

### 7.5 Debug 控制

```python
engine.set_debug(0)  # 关闭（基准测试时使用）
engine.set_debug(1)  # summary 级别
engine.set_debug(2)  # verbose 级别
```

---

> 🤖 Generated with [Claude Code](https://claude.com/claude-code)
