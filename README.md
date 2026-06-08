# Reversi (黑白棋)

一个功能完整的黑白棋（Othello）对战平台，支持人机对战、人人对战、AI 对战等多种模式。AI 引擎使用 C 语言编写并通过 `ctypes` 调用 Python，具有极高的搜索效率。

## 特性

- 🎮 **多种对战模式** — 玩家 vs 玩家、玩家 vs AI、AI vs AI（黑白双方可分别配置不同引擎和参数）
- ⚡ **多引擎架构** — 内置 Negamax（Alpha-Beta + PVS）和 MCTS（蒙特卡洛树搜索）两套引擎，加新引擎只需改一个文件
- 🎨 **现代 UI** — 清新的绿色棋盘设计，引擎选择面板由后端 `/api/engines` 动态生成
- 🔧 **灵活配置** — 前端自动适配引擎参数 schema，Negamax 支持固定深度 / 限时搜索切换，MCTS 支持迭代次数调节
- 🧠 **Bitboard 棋盘** — 使用 64 位整数表示棋盘，位运算加速走法生成与翻转

## 项目结构

```
Reversi/
├── backend/
│   ├── main.py              # FastAPI 入口，启动服务器（含 CORS 中间件）
│   ├── requirements.txt     # Python 依赖
│   ├── core/
│   │   └── board.py         # Bitboard 棋盘引擎（位运算走法生成、翻转、合法性判断）
│   ├── ai/
│   │   ├── search.py        # Python 侧 AI 封装（多引擎注册表 + ctypes 适配器）
│   │   ├── search.c         # Negamax C 引擎（Alpha-Beta + PVS + LMR + 置换表）
│   │   ├── search.dll       # Negamax 编译产物（Windows）
│   │   ├── search_mct.c     # MCTS C 引擎（位棋盘 UCB1 + 置换表缓存随机模拟）
│   │   ├── search_mct.dll   # MCTS 编译产物（Windows）
│   │   ├── benchmark.py     # AI 引擎性能基准测试脚本
│   │   └── BENCHMARK_REPORT.md  # 各版本 AI 引擎评测报告
│   ├── api/
│   │   └── routes.py        # API 路由（/engines /ai-move /init — 动态引擎分发）
│   └── static/
│       └── index.html       # 构建后的前端页面（单文件部署）
├── frontend/
│   ├── package.json         # 前端依赖（Vite + TypeScript）
│   ├── vite.config.js       # Vite 配置（含 singlefile 插件）
│   ├── public/              # 静态资源（图标等）
│   └── src/
│       ├── main.ts          # 前端入口，游戏主循环与回合调度
│       ├── core/
│       │   └── game.ts      # 游戏逻辑引擎（走法合法性、翻转、计分）
│       ├── ui/
│       │   ├── board.ts     # 棋盘 UI 组件
│       │   ├── msg.ts       # 状态栏与比分显示组件
│       │   └── choose.ts    # 开局选择面板（动态渲染引擎 + 参数表单）
│       └── api/
│           └── ai.ts        # AI API 通信模块（泛化 EngineConfig）
├── docs/
│   ├── engine-registry-design.md  # 引擎注册机制架构设计
│   ├── adding-ai-engine.md        # 添加自定义 AI 引擎指南
│   └── backlog.md                 # 项目代办事项
└── README.md
```

## 快速开始

### 环境要求

- Python 3.10+
- Node.js 18+（仅前端开发时需要）
- Windows / Linux / macOS

### 1. 安装 Python 依赖

```bash
cd backend
pip install -r requirements.txt
```

### 2. 编译 AI 动态库（可选）

项目已包含预编译的 `search.dll` 和 `search_mct.dll`（Windows）。如需重新编译：

**Negamax 引擎：**
```bash
cd backend/ai
gcc -O2 -static -shared -o search.dll search.c
```

**MCTS 引擎：**
```bash
cd backend/ai
gcc -O2 -static -shared -o search_mct.dll search_mct.c
```

**Linux/macOS：**
```bash
cd backend/ai
gcc -O2 -static -shared -fPIC -o search.so search.c
gcc -O2 -static -shared -fPIC -o search_mct.so search_mct.c
```

> `-static` 是必要的，否则 Python 运行时可能找不到 `libgcc_s_seh-1.dll`。跨平台时 `search.py` 自动选择 `.dll` 或 `.so`。

### 3. 启动后端

```bash
cd backend
python main.py
```

服务器启动后会自动打开浏览器访问 `http://127.0.0.1:8000`。

### 4. 前端开发（可选）

如需修改前端代码：

```bash
cd frontend
npm install
npm run dev      # 启动 Vite 开发服务器
npm run build    # 构建并输出到 backend/static/index.html
```

> 前端构建使用 `vite-plugin-singlefile`，将所有 JS/CSS 内联到单个 HTML 文件中，方便部署。

## 使用说明

### 引擎配置

启动后会弹出配置面板，黑白双方可独立配置：

- **留空 URL** → 该方由人类手动点击落子
- **填 `/api/ai-move`** → 使用本机 AI 引擎
- **引擎下拉** → 选择 Negamax（Alpha-Beta）或 MCTS（蒙特卡洛）
- **参数** → 由引擎类型动态决定（深度 / 时间上限 / 迭代次数等）

面板由 `GET /api/engines` 的响应动态生成，加新引擎无需改前端代码。

### API 接口

#### `GET /api/engines`

返回所有已注册引擎的元信息（引擎名、参数 schema 等）。前端启动时调用此端点动态渲染选择面板。

#### `POST /api/ai-move`

统一 AI 走法端点。

```json
// 请求（新格式）
{
  "board": [[0,0,0,...], ...],
  "player": 1,
  "engine_type": "negamax",
  "params": { "strategy": "fixed_depth", "depth": 14 }
}

// 请求（MCTS）
{
  "board": [[0,0,0,...], ...],
  "player": -1,
  "engine_type": "mcts",
  "params": { "iterations": 20000 }
}

// 请求（旧格式，仍兼容）
{ "board": [[0,0,0,...], ...], "player": 1, "depth": 14 }

// 响应
{ "r": 2, "c": 3, "depth": 0 }
```

#### `POST /api/init`

重置 AI 引擎状态（清空置换表 / 重新播种）。

```json
// 请求
{ "engine_type": "negamax" }
// 响应
{ "status": "success", "message": "引擎 'negamax' 已重置" }
```

## AI 引擎

AI 核心由 C 语言编写，通过 `ctypes` 与 Python 集成。内置两种引擎：

| 引擎 | 算法 | 参数 | 适用场景 |
|------|------|------|---------|
| **Negamax** | Alpha-Beta + PVS + LMR + 置换表 + 迭代加深 | `depth` (固定深度) 或 `time_limit_ms` (限时) | 中盘精确计算 |
| **MCTS** | 蒙特卡洛树搜索 + UCB1 + 位棋盘 + 置换表缓存 | `iterations` (模拟次数) | 探索性强，适合开局 |

**Negamax 主要技术：**

| 技术 | 说明 |
|------|------|
| **Bitboard** | 64 位整数表示棋盘，位运算加速走法生成 |
| **置换表 (TT)** | 缓存搜索过的局面，避免重复计算 |
| **PVS (Principal Variation Search)** | 零窗口搜索加速 alpha-beta 剪枝 |
| **迭代加深** | 逐层加深搜索，配合时间控制 |
| **延迟走法缩减 (LMR)** | 对排序靠后的走法降低搜索深度 |
| **静态剪枝 (Futility Pruning)** | 在浅层节点提前评估截断 |
| **历史启发表** | 记录走法有效性，优化走法排序 |
| **杀手着法** | 记录引发截断的走法，优先搜索 |
| **IID (内部迭代加深)** | 对无 TT 命中的 PV 节点内嵌迭代加深 |

> 空步裁剪（Null Move Pruning）经实测在黑白棋中盘会错误截断（迫移/zugzwang 频发），已移除。

根据基准测试，当前 Negamax 版本相比原始算法有 **约 15 倍加速**（详见 `backend/ai/BENCHMARK_REPORT.md`）。

### 加新引擎

P1 重构后，加一个新引擎只需修改 `backend/ai/search.py` 一个文件 — 写 Adapter 类 + 一行注册，前端和后端路由**零改动**。详见 [`docs/adding-ai-engine.md`](docs/adding-ai-engine.md)。

## 技术栈

| 层 | 技术 |
|----|------|
| 后端框架 | FastAPI (Python) |
| AI 引擎 | C（Negamax + MCTS，ctypes 调用） |
| 引擎架构 | 注册表模式 + Adapter 契约 + `/api/engines` 自描述 |
| 前端 | TypeScript + Vite（vite-plugin-singlefile 单文件输出） |
| 样式 | 原生 CSS（内联，无外部依赖） |

## 许可证

MIT
