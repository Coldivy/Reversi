"""
routes.py — Reversi AI 后端 API 路由

端点:
  GET  /api/engines     → 返回所有已注册引擎的元信息
  POST /api/ai-move     → 统一 AI 走法请求（按 engine_type 动态分发）
  POST /api/ai-move-timelimit → 向后兼容（内部转发到 /ai-move）
  POST /api/init        → 重置引擎状态
"""

from fastapi import APIRouter
from pydantic import BaseModel
from typing import Any, Dict, List, Optional

from core.board import Bitboard
from ai.search import SearchEngine, get_adapter, get_engine_specs

import time

router = APIRouter()

# ======================================================================
#  请求模型
#
#  params 是引擎自由参数（如 {"depth": 14}、{"iterations": 20000}）。
#  为保持向后兼容，也接受旧式的 depth / time_limit_ms / iterations
#  顶层字段，在路由层合并到 params 中再传给适配器。
# ======================================================================


class GameRequest(BaseModel):
    board: List[List[int]]
    player: int                                   # 1=黑, -1=白
    engine_type: str = "negamax"                   # 引擎名
    params: Dict[str, Any] = {}                    # 引擎参数（新格式）
    # 以下为向后兼容字段
    depth: Optional[int] = None
    time_limit_ms: Optional[int] = None
    iterations: Optional[int] = None


class InitRequest(BaseModel):
    engine_type: Optional[str] = None


# ======================================================================
#  辅助：合并旧格式参数到 params
# ======================================================================

def _merge_legacy_params(request: GameRequest) -> Dict[str, Any]:
    """将旧式顶层字段合并到 params dict（params 优先）。"""
    merged = dict(request.params)
    if "depth" not in merged and request.depth is not None:
        merged["depth"] = request.depth
    if "time_limit_ms" not in merged and request.time_limit_ms is not None:
        merged["time_limit_ms"] = request.time_limit_ms
    if "iterations" not in merged and request.iterations is not None:
        merged["iterations"] = request.iterations
    return merged


# ======================================================================
#  API 端点
# ======================================================================


@router.get("/engines")
async def list_engines():
    """
    返回所有已注册引擎的元信息。

    前端启动时调用此端点，动态渲染引擎选择面板。

    响应示例:
    [
      {
        "name": "negamax",
        "label": "Negamax (Alpha-Beta)",
        "params": [
          {"key": "depth", "type": "int", "default": 14, "min": 1, "max": 64, "label": "搜索深度"},
          ...
        ]
      },
      ...
    ]
    """
    return get_engine_specs()


@router.post("/ai-move")
async def get_ai_move(request: GameRequest):
    """
    统一 AI 走法端点。

    引擎分发逻辑：根据 engine_type 从适配器注册表查找对应的 Adapter，
    调用其 search(p_bb, o_bb, params, player_value) 方法。
    路由层不包含任何引擎特定分支。

    请求体示例:
      {"board": [...], "player": 1, "engine_type": "negamax", "params": {"depth": 14}}
      {"board": [...], "player": -1, "engine_type": "mcts", "params": {"iterations": 20000}}
      旧格式: {"board": [...], "player": 1, "engine_type": "negamax", "depth": 14}
    """
    # ── 位棋盘转换 ──
    black_bb, white_bb = Bitboard.from_array(request.board)

    # ── 查找适配器 ──
    adapter = get_adapter(request.engine_type)
    if adapter is None:
        return {
            "r": -1, "c": -1,
            "error": f"引擎 '{request.engine_type}' 未注册",
        }

    # ── 确定己方/对方位棋盘 ──
    # 对 MCTS 等需要真实颜色的引擎，传原始 black_bb/white_bb + player_value
    # 对 Negamax 等对称引擎，传 p_bb/o_bb（不影响结果）
    if request.player == 1:
        p_bb, o_bb = black_bb, white_bb
    else:
        p_bb, o_bb = white_bb, black_bb

    # ── 合并参数 ──
    params = _merge_legacy_params(request)

    start_time = time.perf_counter()

    # ── 统一分发（适配器自行解释 params 键） ──
    move, score, extra = adapter.search(p_bb, o_bb, params, request.player)

    elapsed = time.perf_counter() - start_time

    # ── 日志 ──
    side = "黑" if request.player == 1 else "白"
    param_repr = ", ".join(f"{k}={v}" for k, v in params.items())
    print(f"AI [{request.engine_type}] ({side}) | {param_repr} | "
          f"耗时: {elapsed:.3f}s | 落子: {move} | 估分: {score}")

    if move is None:
        return {"r": -1, "c": -1, "depth": extra}

    return {"r": move // 8, "c": move % 8, "depth": extra}


# ======================================================================
#  向后兼容端点
# ======================================================================


@router.post("/ai-move-timelimit")
async def get_ai_move_timed(request: GameRequest):
    """
    旧版限时搜索端点 — 自动补 time_limit_ms 后转发到 /ai-move。
    """
    if request.time_limit_ms is None and "time_limit_ms" not in request.params:
        request.time_limit_ms = 3000
    if request.engine_type == "negamax" and request.params == {}:
        request.engine_type = request.engine_type  # 保持默认
    return await get_ai_move(request)


# ======================================================================
#  引擎初始化 / 重置
# ======================================================================


@router.post("/init")
async def init_ai_engine(request: InitRequest = InitRequest()):
    """
    重置 AI 引擎状态（清空置换表 / 重新播种）。

    • engine_type 为空 → 重置所有已注册引擎
    • engine_type 指定 → 仅重置该引擎
    """
    if request.engine_type:
        engine_type = request.engine_type
        adapter = get_adapter(engine_type)
        if adapter is None:
            return {"status": "error", "message": f"引擎 '{engine_type}' 未注册"}

        adapter.init()
        print(f"  [routes] 已重置引擎 '{engine_type}'")
        return {"status": "success", "message": f"引擎 '{engine_type}' 已重置"}
    else:
        for name in SearchEngine.get_available_engines():
            get_adapter(name).init()
        print(f"  [routes] 已重置全部引擎: {SearchEngine.get_available_engines()}")
        return {"status": "success", "message": "全部引擎已重置"}
