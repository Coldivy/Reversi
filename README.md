# Reversi（黑白棋）

一个完整的黑白棋对战平台，支持人机/人机/AI 对战。AI 引擎使用 C 语言编写并通过 ctypes 调用，前端使用 TypeScript + Vite 单文件输出。

**内置两种搜索引擎：Negamax（Alpha-Beta 精确搜索）和 MCTS（蒙特卡洛树搜索），可混合对战。**

---

## 快速开始

```bash
# 安装 Python 依赖（建议 conda 环境）
cd backend
pip install -r requirements.txt

# 启动服务（自动打开浏览器）
python main.py
```

访问 `http://127.0.0.1:8000`，在配置面板中选择引擎和参数后开始对局。

> **Windows** 已预编译 `.dll`，开箱即用。**Linux/macOS** 需编译 C 引擎：
> ```bash
> cd backend/ai
> gcc -O3 -static -shared -o search.dll search.c
> gcc -O3 -static -shared -o search_mct.dll search_mct.c
> ```

---

## 特性

| | |
|---|---|
| **对战模式** | 人机、人人、AI vs AI——黑白双方独立配置引擎和参数 |
| **双引擎** | Negamax（精确搜索，87% 胜率对 MCTS）+ MCTS（蒙特卡洛） |
| **前端自动适配** | `GET /api/engines` 返回引擎参数 Schema，配置面板动态渲染——加引擎只改 `search.py` |
| **位棋盘** | 64-bit 位运算走法生成与翻转，亚毫秒级 |
| **构建** | 前端 Vite 单文件输出 → `backend/static/index.html`，零外部依赖 |

---

## 项目结构

```
Reversi/
├── backend/
│   ├── main.py                 # FastAPI 入口
│   ├── core/board.py           # Bitboard 棋盘引擎
│   ├── ai/
│   │   ├── search.py           # 引擎注册表 + Adapter（加引擎改这里）
│   │   ├── search.c / .dll     # Negamax C 引擎
│   │   ├── search_mct.c / .dll # MCTS C 引擎
│   │   ├── benchmark.py        # 基准测试
│   │   └── v0~v3_*_debug.*    # 历史对照版本
│   ├── api/routes.py           # API 路由（无引擎特化代码）
│   └── static/index.html       # 前端构建产物
├── frontend/
│   └── src/
│       ├── main.ts             # 游戏主循环
│       ├── core/game.ts        # 前端游戏逻辑
│       ├── ui/{board,msg,choose}.ts
│       └── api/ai.ts           # AI 通信（泛化 EngineConfig）
└── docs/
    ├── engine-registry-design.md  # 注册机制架构设计
    ├── adding-ai-engine.md        # 加引擎指南
    ├── BENCHMARK_REPORT.md        # 性能评测报告
    └── backlog.md
```

---

## AI 引擎

### Negamax（精确搜索）

Alpha-Beta + PVS + 置换表（2²⁰ 条目）+ 迭代加深 + LMR + Futility + 杀手着法 + 历史启发表 + 渴望窗口 + IID。

| 模式 | 参数 | 说明 |
|------|------|------|
| 固定深度 | `depth` (1–64) | 每步搜到指定深度 |
| 限时搜索 | `time_limit_ms` (10–600000) | 迭代加深 + 时间控制 |

### MCTS（蒙特卡洛树搜索）

UCB1 选择 + 启发式扩展 + 随机模拟 + 置换表缓存 + 反向传播。

| 模式 | 参数 | 说明 |
|------|------|------|
| 固定迭代 | `iterations` (100–10M) | 指定模拟次数 |
| 限时搜索 | `time_limit_ms` | 在时限内跑尽可能多的迭代 |

### 基准测试摘要

固定深度搜索时间（6 局面平均，10 次重复）：

| 深度 | v0（纯 Minimax） | v1（Alpha-Beta） | v4（完全体） |
|:---:|:---:|:---:|:---:|
| d=6 | 25.7 ms | 277 μs | 368 μs |
| d=8 | 2.51 s | 3.47 ms | 2.87 ms |
| d=10 | 6.81 s | 42.6 ms | 22.0 ms |

限时对局：Negamax vs MCTS，30 局，**Negamax 胜率 87%**。详见 [`docs/BENCHMARK_REPORT.md`](docs/BENCHMARK_REPORT.md)。

---

## API

### `GET /api/engines`

返回已注册引擎的元信息（名称、标签、参数 Schema）。前端启动时调用，驱动整个配置面板。

### `POST /api/ai-move`

```
{ "board": [[0,0,...],...], "player": 1, "engine_type": "negamax",
  "params": {"strategy": "fixed_depth", "depth": 14} }
→ { "r": 2, "c": 3, "depth": 14 }
```

### `POST /api/init`

重置引擎状态。

---

## 加新引擎

只需修改 `backend/ai/search.py`——写 Adapter 类 + 注册到 `_adapter_registry`。前端和路由**零改动**。详见 [`docs/adding-ai-engine.md`](docs/adding-ai-engine.md)。

```python
class MyAdapter:
    spec = {"name": "my_engine", "label": "我的引擎", "params": [...]}
    @staticmethod
    def init(): pass
    @staticmethod
    def search(p_bb, o_bb, params, player_value):
        # 返回 (move, score, extra)
        return move, score, 0

_adapter_registry["my_engine"] = MyAdapter
```

---

## 技术栈

| 层 | 技术 |
|---|---|
| 后端框架 | FastAPI (Python 3.10+) |
| AI 引擎 | C (GCC, `-O3`), ctypes 调用 |
| 引擎架构 | Registry + Adapter 模式, `/api/engines` 自描述 |
| 前端 | TypeScript + Vite, vanilla DOM（无框架） |
| 输出 | vite-plugin-singlefile → 单 HTML, 零依赖 |

## 许可证

MIT
