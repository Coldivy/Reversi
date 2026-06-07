from fastapi import APIRouter
from pydantic import BaseModel
from typing import List

from core.board import Bitboard
from ai.search import SearchEngine

import time

# 创建一个路由对象
router = APIRouter()

# 定义请求数据结构


class GameRequest(BaseModel):
    board: List[List[int]]
    player: int


@router.post("/ai-move")
async def get_ai_move(request: GameRequest):
    """
    接收前端获取ai走法请求
    """
    # 转换数据
    black_bb, white_bb = Bitboard.from_array(request.board)

    # 确定搜索方
    p_bb, o_bb = (black_bb, white_bb) if request.player == 1 else (
        white_bb, black_bb)

    # 记录开始时间
    start_time = time.perf_counter()

    # 调用 AI 搜索
    move, score = SearchEngine.get_best_move(
        depth=14,
        player_bb=p_bb,
        opponent_bb=o_bb,
    )

    # 计算耗时并打印在终端
    elapsed = time.perf_counter() - start_time
    print(f"AI ({'黑' if request.player == 1 else '白'}) 思考完毕 | 耗时: "
          f"{elapsed:.3f} 秒 | 决定落子: {move} | 估分: {score}"
          )

    if move is None:
        return {"r": -1, "c": -1}

    return {"r": move // 8, "c": move % 8}


class GameRequestTimed(BaseModel):
    board: List[List[int]]
    player: int
    time_limit_ms: int


@router.post("/ai-move-timelimit")
async def get_ai_move_timed(request: GameRequestTimed):
    """
    接收前端获取ai走法请求（限时搜索版本）
    """
    # 转换数据
    black_bb, white_bb = Bitboard.from_array(request.board)

    # 确定搜索方
    p_bb, o_bb = (black_bb, white_bb) if request.player == 1 else (
        white_bb, black_bb)

    # 记录开始时间
    start_time = time.perf_counter()

    # 调用限时 AI 搜索
    move, score, depth_reached = SearchEngine.get_best_move_timed(
        time_limit_ms=request.time_limit_ms,
        player_bb=p_bb,
        opponent_bb=o_bb,
    )

    # 计算耗时并打印在终端
    elapsed = time.perf_counter() - start_time
    print(f"AI ({'黑' if request.player == 1 else '白'}) 限时搜索完毕 | "
          f"时限: {request.time_limit_ms}ms | 到达深度: {depth_reached} | "
          f"耗时: {elapsed:.3f} 秒 | 决定落子: {move} | 估分: {score}")

    if move is None:
        return {"r": -1, "c": -1, "depth": depth_reached}

    return {"r": move // 8, "c": move % 8, "depth": depth_reached}


@router.post("/init")
async def init_ai_engine():
    """
    接收前端重置请求，清空置换表缓存
    """
    try:
        SearchEngine.init_engine()
        return {"status": "success", "message": "置换表已重置"}
    except Exception as e:
        return {"status": "error", "message": str(e)}
