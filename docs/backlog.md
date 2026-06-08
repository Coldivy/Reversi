# 项目代办事项

## 🏗 架构改进

### `[DONE]` 引擎注册机制简化 — 消除前后端重复声明

**现状问题：** 加一个新 AI 引擎需要改 4 个文件（`search.py` / `routes.py` / `ai.ts` / `choose.ts`），其中前端 2 个文件的改动是纯机械劳动——引擎名、参数名、默认值在前后端各声明了一遍。

**目标：** 加新引擎只需改 `search.py` 一个文件，前端和后端路由零改动。

**方案（A+B 结合）：**

| 步骤 | 改什么 | 效果 |
|------|--------|------|
| ① `search.py` | 每个 Adapter 声明 `spec`（name, label, params schema），注册到统一 dict | 引擎元信息集中一处 |
| ② `routes.py` | 加 `GET /api/engines`，返回所有已注册引擎的 schema；`POST /api/ai-move` 分发改为按 `engine_type` 动态查找适配器 | 消除所有 `if engine_type ==` 硬编码分支 |
| ③ `ai.ts` | `EngineConfig` 改为 `{url, engine, params: Record<string, any>}`，POST body 把 `params` 打平合并 | 不再需要 `EngineType` 联合类型 |
| ④ `choose.ts` | 改为通用表单：启动时 fetch `/api/engines`，动态渲染引擎下拉 + 参数输入行（key=value，可增删） | 一次改动，永久免疫 |

**改完之后，加一个新引擎的流程：**

```
search.py: 写 Adapter + 一行注册 → 完成。前端零改动，routes.py 零改动。
```

**备选方案：** 如果暂时不想大规模重构，可先只做 ①+②（后端自描述 + 动态分发），前端先保持硬编码。这样至少后端侧是干净的，前端以后再做也不影响接口。

**预估工作量：** 2-3 小时（四个文件，每个改动量都不大）

---

## 🐛 Bug 修复 & 改进

### `[P2]` MCTS 与 Negamax 对战时后台日志刷屏问题

**现象：** MCTS 引擎在中局持续输出搜索日志但前端不落子，前端报 `TypeError: Failed to fetch`。

**根因（已定位）：**
1. MCTS 白方颜色反转 —— `_MCTSAdapter._search` 对白方传入了调换后的位棋盘，导致为对方搜索、返回非法走法
2. 全局 `set_active_engine()` 在两个并发请求间存在竞争条件

已于 `routes.py` 和 `search.py` 中修复，待最终验证。

---

## 📝 文档

### `[P3]` README 更新

当前 `README.md` 描述的是旧版单引擎架构（`/api/ai-move` vs `/api/ai-move-timelimit`）。需要更新到新版多引擎架构：

- API 接口文档（新增 `engine_type`、`iterations`、`depth` 等字段）
- 项目结构（新增 `search_mct.c` / `search_mct.dll`）
- AI 引擎技术表（新增 MCTS）
- 使用说明（更新面板截图和引擎选择说明）

### `[P4]` 添加 AGENTS.md 或 CLAUDE.md

为 AI 编程助手提供项目上下文，包括：

- 项目架构概览
- 代码风格约定（2 空格缩进、`/* */` 注释等）
- 常用命令（编译 C、构建前端、启动服务）
- 已知陷阱（棋盘颜色编码转换链、置换表共享问题）

---

## 🚀 可能的未来引擎

以下是一些可以接入的引擎想法（仅作备忘，不做承诺）：

| 引擎 | 语言 | 描述 | 预计实现方式 |
|------|------|------|-------------|
| **深度学习模型** | Python/ONNX | 加载训练好的神经网络权重做局面评估 + MCTS | 纯 Python Adapter，零 DLL |
| **Edax 移植** | C | 著名的开源黑白棋引擎，Wthor 级别 | C DLL，接口类似现有 negamax |
| **多线程 MCTS** | C++ | 支持多线程并行模拟的 MCTS | DLL，参数加 `threads` |
| **开局库** | Python | 读取 `.obf` 格式开局库文件，开局阶段直接查表 | 纯 Python，可叠加到任意引擎前面 |
| **残局库** | Python/C | 预计算的 ≤N 空格完美解 | 独立文件，引擎内部查表 |
| **云端 LLM** | Python | 调 OpenAI / Claude API，prompt 是 ASCII 棋盘 + 走法列表 | 纯 Python Adapter |

---

## ✅ 已完成

- [x] Negamax 引擎（`search.dll`）—— Alpha-Beta + PVS + LMR + TT
- [x] MCTS 引擎（`search_mct.dll`）—— 位棋盘 UCB1 + 置换表缓存随机模拟
- [x] `search.py` 多引擎架构 —— `EngineSpec` + `_XxxAdapter` + `SearchEngine` 外观
- [x] `routes.py` 多引擎分发 —— 根据 `engine_type` 字段路由到不同适配器
- [x] 前端引擎选择面板 —— 引擎下拉 + 动态参数 UI
- [x] CORS 中间件
- [x] 引擎注册简化（P1 A+B 方案）—— 加新引擎只需改 `search.py` 一个文件
