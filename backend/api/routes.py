from fastapi import APIRouter
from pydantic import BaseModel
from typing import List

from core.board import Bitboard
from ai.search import SearchEngine

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

    # 调用 AI 搜索
    move, score = SearchEngine.get_best_move(
        depth=12,
        player_bb=p_bb,
        opponent_bb=o_bb,
    )

    if move is None:
        return {"r": -1, "c": -1}

    return {"r": move // 8, "c": move % 8}


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
