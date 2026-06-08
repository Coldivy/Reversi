"""
search.py — 黑白棋 AI 搜索引擎封装

多引擎架构：
  • "negamax" — Alpha-Beta + PVS + LMR + 置换表 (search.dll)
  • "mcts"    — 蒙特卡洛树搜索 (search_mct.dll)

上层调用：
  routes.py 通过 SearchEngine 外观类或直接通过 _adapter_registry[name]
  调用适配器的 search(p_bb, o_bb, params, player) 统一入口。

spec 参数 schema 支持的字段：
  • key          — 参数键名（在 params dict 中的键）
  • type         — "int" | "select"
  • label        — 前端显示名
  • default      — 默认值
  • min / max    — 数值范围（type=int 时）
  • options      — 下拉选项 [{value, label}]（type=select 时）
  • show_if      — 条件显示 {other_key: required_value}（可选）
                    当 other_key 的值 != required_value 时，该参数在前端隐藏。

加新引擎：
  1. 写 Adapter 类（实现 init / search / spec）
  2. 加到 _adapter_registry
  → 前端自动感知（通过 GET /api/engines），routes.py 无需改动。
"""

import ctypes
import os
import platform
import sys
from typing import Any, Dict, List, Optional, Tuple

# ======================================================================
#  路径 & 平台
# ======================================================================

current_dir = os.path.dirname(os.path.abspath(__file__))
system_name = platform.system()
_LIB_EXT = ".dll" if system_name == "Windows" else ".so"

# ======================================================================
#  ctypes 辅助类型
# ======================================================================

# int[8][8] — MCTS 引擎的棋盘参数类型
Board8x8 = (ctypes.c_int * 8) * 8


def _bb_to_board(black_bb: int, white_bb: int) -> Board8x8:
    """将位棋盘转换为 8×8 int 数组，0=空 1=黑 2=白。"""
    board = Board8x8()
    for i in range(64):
        r, c = divmod(i, 8)
        if (black_bb >> i) & 1:
            board[r][c] = 1
        elif (white_bb >> i) & 1:
            board[r][c] = 2
        else:
            board[r][c] = 0
    return board


# ======================================================================
#  DLL 规格 & 加载
# ======================================================================

class EngineSpec:
    """描述一个搜索引擎 DLL 的函数签名元信息。"""

    def __init__(self, name: str, lib_filename: str):
        self.name = name
        self.lib_filename = lib_filename
        self.lib: Optional[ctypes.CDLL] = None
        self._pending_funcs: Dict[str, Tuple[List[Any], Any]] = {}

    def register_func(self, name: str, argtypes: List[Any], restype: Any) -> None:
        """登记一个导出函数，DLL 加载后统一绑定签名。"""
        self._pending_funcs[name] = (argtypes, restype)

    def bind(self, lib: ctypes.CDLL) -> None:
        """将已登记的签名绑定到 CDLL 实例上。"""
        self.lib = lib
        for func_name, (argtypes, restype) in self._pending_funcs.items():
            try:
                func = getattr(lib, func_name)
            except AttributeError:
                print(f"WARNING: function '{func_name}' not found in "
                      f"'{self.lib_filename}'")
                continue
            func.argtypes = argtypes
            func.restype = restype


# ---------- DLL 注册表 ----------

_dll_registry: Dict[str, EngineSpec] = {}


def _load_dll(spec: EngineSpec) -> bool:
    """加载一个引擎 DLL 并绑定函数签名。成功返回 True。"""
    lib_path = os.path.join(current_dir, spec.lib_filename)

    if not os.path.exists(lib_path):
        print(f"WARNING: Engine '{spec.name}' DLL not found: {lib_path}")
        return False

    try:
        lib = ctypes.CDLL(lib_path)
        spec.bind(lib)
    except Exception as e:
        print(f"ERROR: Failed to load engine '{spec.name}': {e}")
        return False

    _dll_registry[spec.name] = spec
    print(f"  [search.py] DLL 已加载 '{spec.name}' ← {spec.lib_filename}")
    return True


# ---------- negamax DLL ----------

_spec_negamax = EngineSpec("negamax", f"search{_LIB_EXT}")
_spec_negamax.register_func("c_init_search", [], None)
_spec_negamax.register_func("c_get_best_move", [
    ctypes.c_int, ctypes.c_uint64, ctypes.c_uint64,
    ctypes.POINTER(ctypes.c_int),
], ctypes.c_int)
_spec_negamax.register_func("c_get_best_move_timed", [
    ctypes.c_int, ctypes.c_uint64, ctypes.c_uint64,
    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
], ctypes.c_int)

# ---------- MCTS DLL ----------

_spec_mcts = EngineSpec("mcts", f"search_mct{_LIB_EXT}")
_spec_mcts.register_func("mcts_init_seed", [], None)
_spec_mcts.register_func("mcts_search", [
    Board8x8, ctypes.c_int,
    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
    ctypes.c_int,
], None)
_spec_mcts.register_func("get_legal_moves_list", [
    Board8x8, ctypes.c_int, ctypes.POINTER(ctypes.c_int),
], ctypes.c_int)
_spec_mcts.register_func("make_move_dll", [
    Board8x8, ctypes.c_int, ctypes.c_int, ctypes.c_int,
], ctypes.c_int)
_spec_mcts.register_func("is_game_over_dll", [
    Board8x8,
], ctypes.c_int)
_spec_mcts.register_func("get_score_dll", [
    Board8x8, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
], None)

# ---------- 加载 ----------

_loaded_any = False
_loaded_any |= _load_dll(_spec_negamax)
_loaded_any |= _load_dll(_spec_mcts)

if not _loaded_any:
    print(f"CRITICAL ERROR: 找不到任何搜索引擎 DLL!")
    print(f"搜索目录: {current_dir}")
    print(f"目录下文件: {os.listdir(current_dir)}")
    sys.exit(1)


# ======================================================================
#  适配器类（每个引擎一个）
#
#  每个适配器必须提供:
#    spec : dict — { name, label, params[{key,type,default,min,max,label}] }
#    init()     — 初始化引擎状态
#    search(player_bb, opponent_bb, params, player_value) → (move, score, extra)
#
#  其中 params 是自由键值对，适配器自行提取需要的键。
#  这样 routes.py 不需要知道任何引擎特定参数。
# ======================================================================


class _NegamaxAdapter:
    """Negamax 引擎适配器 — 封装 search.dll。"""

    spec = {
        "name": "negamax",
        "label": "Negamax (Alpha-Beta)",
        "params": [
            {"key": "strategy",       "type": "select",
             "options": [
                 {"value": "fixed_depth", "label": "固定深度"},
                 {"value": "time_limit",  "label": "限时搜索"},
             ],
             "label": "搜索模式"},
            {"key": "depth",          "type": "int", "default": 14,
             "min": 1, "max": 64,     "label": "搜索深度",
             "show_if": {"strategy": "fixed_depth"}},
            {"key": "time_limit_ms",  "type": "int", "default": 3000,
             "min": 10, "max": 600000, "label": "时间上限 (ms)",
             "show_if": {"strategy": "time_limit"}},
        ],
    }

    @staticmethod
    def init() -> None:
        _dll_registry["negamax"].lib.c_init_search()

    # ── 底层 DLL 调用 ──

    @staticmethod
    def get_best_move(depth: int, player_bb: int, opponent_bb: int
                      ) -> Tuple[Optional[int], int]:
        best_move = ctypes.c_int(-1)
        score = _dll_registry["negamax"].lib.c_get_best_move(
            ctypes.c_int(depth),
            ctypes.c_uint64(player_bb),
            ctypes.c_uint64(opponent_bb),
            ctypes.byref(best_move),
        )
        move_val = best_move.value
        if move_val == -1:
            return None, int(score)
        return move_val, int(score)

    @staticmethod
    def get_best_move_timed(time_limit_ms: int,
                            player_bb: int, opponent_bb: int
                            ) -> Tuple[Optional[int], int, int]:
        best_move = ctypes.c_int(-1)
        depth_searched = ctypes.c_int(0)
        score = _dll_registry["negamax"].lib.c_get_best_move_timed(
            ctypes.c_int(time_limit_ms),
            ctypes.c_uint64(player_bb),
            ctypes.c_uint64(opponent_bb),
            ctypes.byref(best_move),
            ctypes.byref(depth_searched),
        )
        move_val = best_move.value
        if move_val == -1:
            return None, int(score), depth_searched.value
        return move_val, int(score), depth_searched.value

    # ── 统一搜索入口（供 routes.py 无差别调用） ──

    @staticmethod
    def search(player_bb: int, opponent_bb: int,
               params: dict, player_value: int = 1
               ) -> Tuple[Optional[int], int, int]:
        """
        Negamax 统一搜索。

        params 键:
          • strategy → "fixed_depth"（固定深度）或 "time_limit"（限时搜索）
            若 strategy 未指定，按是否存在 time_limit_ms 推断（向后兼容）。
          • depth → 搜索层数（strategy=fixed_depth 时生效，默认 14）
          • time_limit_ms → 时间上限（strategy=time_limit 时生效，默认 3000）

        Returns: (move, score, extra)
          extra = depth_reached（限时模式）或 0（固定深度）
        """
        strategy = params.get("strategy")
        if strategy is None:
            # 向后兼容旧格式（无 strategy 字段）
            strategy = "time_limit" if "time_limit_ms" in params else "fixed_depth"

        if strategy == "time_limit":
            return _NegamaxAdapter.get_best_move_timed(
                params.get("time_limit_ms", 3000), player_bb, opponent_bb)
        else:
            depth = params.get("depth", 14)
            move, score = _NegamaxAdapter.get_best_move(
                depth, player_bb, opponent_bb)
            return move, score, 0

    @staticmethod
    def search_with_iterations(iterations: int,
                               player_bb: int, opponent_bb: int
                               ) -> Tuple[Optional[int], int]:
        """Negamax 没有迭代概念，映射为 timed 搜索。"""
        time_ms = max(100, iterations // 50)
        move, score, _ = _NegamaxAdapter.get_best_move_timed(
            time_ms, player_bb, opponent_bb)
        return move, score


class _MCTSAdapter:
    """MCTS 引擎适配器 — 封装 search_mct.dll。

    MCTS 核心参数是 iterations（模拟次数），不使用 depth/time_limit。
    """

    spec = {
        "name": "mcts",
        "label": "MCTS (蒙特卡洛)",
        "params": [
            {"key": "iterations", "type": "int", "default": 20000,
             "min": 100, "max": 10000000, "label": "模拟次数"},
        ],
    }

    DEFAULT_ITERATIONS = 20000

    @classmethod
    def init(cls) -> None:
        spec = _dll_registry["mcts"]
        if hasattr(spec.lib, "mcts_init_seed"):
            spec.lib.mcts_init_seed()

    # ── 底层 DLL 调用 ──

    @classmethod
    def _search(cls, iterations: int, black_bb: int, white_bb: int,
                player_value: Optional[int] = None
                ) -> Tuple[Optional[int], int]:
        """
        底层 MCTS 搜索。

        Parameters:
            black_bb: 真实黑棋位棋盘（不可调换）
            white_bb: 真实白棋位棋盘（不可调换）
            player_value: 1=黑方, -1=白方。None 时按 occupied 奇偶推断。
        """
        spec = _dll_registry["mcts"]

        # 构建 2D 棋盘（1=黑 2=白）
        board = _bb_to_board(black_bb, white_bb)

        # DLL 需要 player 值 (1=黑 2=白)
        if player_value == 1:
            player = 1
        elif player_value == -1:
            player = 2
        else:
            occupied = (black_bb | white_bb).bit_count()
            player = 1 if occupied % 2 == 0 else 2

        out_x = ctypes.c_int(-1)
        out_y = ctypes.c_int(-1)

        spec.lib.mcts_search(
            board, ctypes.c_int(player),
            ctypes.byref(out_x), ctypes.byref(out_y),
            ctypes.c_int(iterations),
        )

        if out_x.value < 0 or out_y.value < 0:
            return None, 0

        move = out_x.value * 8 + out_y.value
        return move, 0

    # ── 统一搜索入口 ──

    @classmethod
    def search(cls, player_bb: int, opponent_bb: int,
               params: dict, player_value: int = 1
               ) -> Tuple[Optional[int], int, int]:
        """
        MCTS 统一搜索。

        params 键:
          • iterations → 模拟次数（默认 20000）

        **注意:** player_bb/opponent_bb 必须是真实的 black_bb/white_bb
        （不可调换），由 player_value 指示搜索方颜色。

        Returns: (move, score, extra)
          extra = 实际执行的迭代次数
        """
        iters = params.get("iterations", cls.DEFAULT_ITERATIONS)

        # 恢复真实的 black/white 并根据 player_value 传给底层搜索
        if player_value == 1:
            move, score = cls._search(iters, player_bb, opponent_bb, 1)
        else:
            move, score = cls._search(iters, opponent_bb, player_bb, -1)

        return move, score, iters

    @classmethod
    def search_with_iterations(cls, iterations: int,
                                player_bb: int, opponent_bb: int,
                                player_value: int = 1
                                ) -> Tuple[Optional[int], int]:
        """直接按迭代次数搜索（兼容旧接口）。"""
        if player_value == 1:
            return cls._search(iterations, player_bb, opponent_bb, 1)
        else:
            return cls._search(iterations, opponent_bb, player_bb, -1)

    # ── 工具方法 ──

    @classmethod
    def get_legal_moves(cls, black_bb: int, white_bb: int, player: int
                        ) -> List[int]:
        """返回合法走法列表（棋盘索引 0-63）。"""
        spec = _dll_registry["mcts"]
        board = _bb_to_board(black_bb, white_bb)
        out_moves = (ctypes.c_int * 64)()
        count = spec.lib.get_legal_moves_list(
            board, ctypes.c_int(player), out_moves)
        return [out_moves[i] for i in range(count)]


# ======================================================================
#  适配器注册表（引擎名 → 适配器类）
#
#  加新引擎只需:
#    1. 写 Adapter 类（实现 spec + init + search）
#    2. 加到下面这个 dict
#  → routes.py 和前端自动感知，零改动。
# ======================================================================

_adapter_registry: Dict[str, type] = {
    "negamax": _NegamaxAdapter,
    "mcts": _MCTSAdapter,
}


def get_adapter(name: str):
    """按名称获取适配器类。不存在返回 None。"""
    return _adapter_registry.get(name)


def get_engine_specs() -> List[dict]:
    """返回所有已注册引擎的 spec 列表（供 GET /api/engines 使用）。"""
    return [cls.spec for cls in _adapter_registry.values()]


# ======================================================================
#  SearchEngine — 统一外观（保持向后兼容）
# ======================================================================

_active_engine_name: str = "negamax"


class SearchEngine:
    """
    统一的搜索引擎外观。

    基本用法:
        SearchEngine.set_active_engine("mcts")
        move, score, extra = SearchEngine.search(p_bb, o_bb, {"iterations": 5000})
        SearchEngine.init_engine()

    也支持直接调用:
        SearchEngine.get_best_move(depth=14, ...)      # 向后兼容
        SearchEngine.get_best_move_timed(ms=3000, ...) # 向后兼容
    """

    _adapter = _adapter_registry[_active_engine_name]

    @classmethod
    def set_active_engine(cls, name: str) -> bool:
        """动态切换活跃引擎。返回 True 表示成功。"""
        global _active_engine_name
        if name not in _adapter_registry:
            print(f"ERROR: 引擎 '{name}' 未注册。可用引擎: "
                  f"{list(_adapter_registry.keys())}")
            return False
        _active_engine_name = name
        cls._adapter = _adapter_registry[name]
        print(f"  [search.py] 已切换到引擎 '{name}'")
        return True

    @classmethod
    def get_active_engine(cls) -> str:
        return _active_engine_name

    @classmethod
    def get_available_engines(cls) -> List[str]:
        return list(_adapter_registry.keys())

    # ── 向后兼容接口 ──

    @classmethod
    def init_engine(cls):
        cls._adapter.init()

    @classmethod
    def get_best_move(cls, depth: int, player_bb: int, opponent_bb: int
                      ) -> Tuple[Optional[int], int]:
        return cls._adapter.get_best_move(depth, player_bb, opponent_bb)

    @classmethod
    def get_best_move_timed(cls, time_limit_ms: int,
                            player_bb: int, opponent_bb: int
                            ) -> Tuple[Optional[int], int, int]:
        return cls._adapter.get_best_move_timed(
            time_limit_ms, player_bb, opponent_bb)

    @classmethod
    def search_with_iterations(cls, iterations: int,
                                player_bb: int, opponent_bb: int,
                                player_value: int = 1
                                ) -> Tuple[Optional[int], int]:
        return cls._adapter.search_with_iterations(
            iterations, player_bb, opponent_bb, player_value)


# ======================================================================
#  模块加载时自动初始化默认引擎
# ======================================================================

SearchEngine.init_engine()
