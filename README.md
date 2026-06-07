# Reversi (黑白棋)

一个功能完整的黑白棋（Othello）对战平台，支持人机对战、人人对战、AI 对战等多种模式。AI 引擎使用 C 语言编写并通过 `ctypes` 调用 Python，具有极高的搜索效率。

## 特性

- 🎮 **多种对战模式** — 玩家 vs 玩家、玩家 vs AI、AI vs AI（黑白双方可分别配置不同 API）
- ⚡ **高性能 AI** — C 语言实现的核心搜索引擎，包含置换表、PVS、空步裁剪、延迟走法缩减等优化
- 🎨 **现代 UI** — 清新的绿色棋盘设计，实时显示比分和游戏状态
- 🔧 **灵活配置** — 支持固定深度搜索（`/api/ai-move`）和限时搜索（`/api/ai-move-timelimit`）两种模式
- 🧠 **Bitboard 棋盘** — 使用 64 位整数表示棋盘，位运算加速走法生成与翻转

## 项目结构

```
Reversi/
├── backend/
│   ├── main.py              # FastAPI 入口，启动服务器
│   ├── requirements.txt     # Python 依赖
│   ├── core/
│   │   └── board.py         # Bitboard 棋盘引擎（位运算走法生成、翻转、合法性判断）
│   ├── ai/
│   │   ├── search.py        # Python 侧 AI 封装（通过 ctypes 加载 C 动态库）
│   │   ├── search.c         # C 搜索引擎（置换表 + PVS + 迭代加深 + 多种剪枝）
│   │   ├── search.dll       # 编译后的 Windows 动态库
│   │   ├── benchmark.py     # AI 引擎性能基准测试脚本
│   │   └── BENCHMARK_REPORT.md  # 各版本 AI 引擎评测报告
│   ├── api/
│   │   └── routes.py        # API 路由（ai-move / ai-move-timelimit / init）
│   ├── scripts/
│   │   ├── self_play.py     # 自对弈训练脚本
│   │   └── train.py         # 模型训练脚本
│   └── static/
│       └── index.html       # 构建后的前端页面（单文件部署）
├── frontend/
│   ├── package.json         # 前端依赖（Vite + TypeScript）
│   ├── vite.config.ts       # Vite 配置（含 singlefile 插件）
│   ├── public/              # 静态资源（图标等）
│   └── src/
│       ├── main.ts          # 前端入口，游戏主循环与回合调度
│       ├── core/
│       │   └── game.ts      # 游戏逻辑引擎（走法合法性、翻转、计分）
│       ├── ui/
│       │   ├── board.ts     # 棋盘 UI 组件
│       │   ├── msg.ts       # 状态栏与比分显示组件
│       │   └── choose.ts    # 开局选择面板（配置黑白双方 API）
│       └── api/
│           └── ai.ts        # AI API 通信模块
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

项目已包含预编译的 `search.dll`（Windows）。如需重新编译：

**Windows (MSYS2/GCC):**
```bash
cd backend/ai
gcc -O3 -march=native -shared -o search.dll search.c
```

**Linux/macOS:**
```bash
cd backend/ai
gcc -O3 -march=native -shared -fPIC -o search.so search.c
```

> `search.py` 会自动根据操作系统选择 `.dll` 或 `.so` 后缀加载。

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

### 开局配置

启动后会弹出配置面板：

- **Black's API URL** — 黑棋的 AI 后端地址
- **White's API URL** — 白棋的 AI 后端地址
- **留空** = 人类玩家手动点击落子
- **`/api/ai-move`** = 使用内置 AI（固定深度 14 层搜索）
- **`/api/ai-move-timelimit`** = 使用内置 AI（限时搜索，默认 3 秒）

### API 接口

#### `POST /api/ai-move`

固定深度搜索（depth=14）。

```json
// 请求
{ "board": [[0,0,0,...], ...], "player": 1 }
// 响应
{ "r": 2, "c": 3 }
```

#### `POST /api/ai-move-timelimit`

限时迭代加深搜索。

```json
// 请求
{ "board": [[0,0,0,...], ...], "player": -1, "time_limit_ms": 3000 }
// 响应
{ "r": 2, "c": 3, "depth": 12 }
```

#### `POST /api/init`

重置 AI 引擎的置换表缓存（新局开始时应调用）。

```json
// 响应
{ "status": "success", "message": "置换表已重置" }
```

## AI 引擎

AI 核心由 C 语言编写，通过 `ctypes` 与 Python 集成。主要技术：

| 技术 | 说明 |
|------|------|
| **Bitboard** | 64 位整数表示棋盘，位运算加速走法生成 |
| **置换表 (TT)** | 缓存搜索过的局面，避免重复计算 |
| **PVS (Principal Variation Search)** | 零窗口搜索加速 alpha-beta 剪枝 |
| **迭代加深** | 逐层加深搜索，配合时间控制 |
| **空步裁剪** | 假设跳过己方回合仍能 beta 截断则剪枝 |
| **延迟走法缩减 (LMR)** | 对排序靠后的走法降低搜索深度 |
| **静态剪枝** | 在浅层节点提前评估截断 |
| **历史启发表** | 记录走法有效性，优化走法排序 |
| **杀手着法** | 记录引发截断的走法，优先搜索 |
| **IID** | 对无 TT 命中的 PV 节点内嵌迭代加深 |

根据基准测试，当前版本相比原始算法有 **约 35 倍加速**（详见 `backend/ai/BENCHMARK_REPORT.md`）。

## 技术栈

| 层 | 技术 |
|----|------|
| 后端框架 | FastAPI (Python) |
| AI 引擎 | C (ctypes 调用) |
| 前端 | TypeScript + Vite |
| 样式 | 原生 CSS（内联） |
| 部署 | 单文件 HTML（vite-plugin-singlefile） |

## 许可证

MIT
