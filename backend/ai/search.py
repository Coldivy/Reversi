import platform
import os
import sys
import ctypes


# class Evaluator:
#     # 按权重值分组的位掩码 (Hex 表示)
#     # 每一行注释代表该权重在棋盘上覆盖的格子逻辑
#     WEIGHT_MASKS = {
#         100: 0x8100000000000081,  # 四个角 (A1, H1, A8, H8)
#         -50: 0x0042000000004200,  # 角落内对角位 (B2, G2, B7, G7)
#         -20: 0x4281000000008142,  # 靠近角落的边缘位
#         10: 0x2400810000810024,  # 边缘及内层对应位
#         5: 0x1800248181240018,  # 剩余边缘及内层位
#         -2: 0x003C424242423C00,  # 靠近边缘的内层敏感区
#         1: 0x0000184242180000,  # 中心区外围
#         0: 0x0000001818000000  # 棋盘最中心四个格
#     }
#
#     @staticmethod
#     def simple_evaluate(player_bb: int, opponent_bb: int) -> float:
#         score = 0
#         # 使用 Python 3.10+ 的 int.bit_count()，这是内置的高性能实现
#         for weight, mask in Evaluator.WEIGHT_MASKS.items():
#             score += (player_bb & mask).bit_count() * weight
#             score -= (opponent_bb & mask).bit_count() * weight
#
#         # 行动力优化：直接用 bit_count
#         p_moves = Bitboard.get_legal_moves(player_bb, opponent_bb).bit_count()
#         o_moves = Bitboard.get_legal_moves(opponent_bb, player_bb).bit_count()
#         score += (p_moves - o_moves) * 5
#         return float(score)


# 1. 获取动态库的绝对路径
current_dir = os.path.dirname(os.path.abspath(__file__))
system_name = platform.system()

if system_name == "Windows":
    lib_name = "search.dll"
else:
    lib_name = "search.so"

lib_path = os.path.join(current_dir, lib_name)

# 2. 强制检查文件是否存在
if not os.path.exists(lib_path):
    # 打印出尝试搜索的路径，方便调试
    print(f"CRITICAL ERROR: 找不到动态库文件!")
    print(f"期待路径: {lib_path}")
    print(f"当前目录下文件: {os.listdir(current_dir)}")
    sys.exit(1)  # 直接停止程序，防止报 NameError

# 3. 加载库
try:
    _lib = ctypes.CDLL(lib_path)
    # 注册c_get_best_move函数
    _lib.c_get_best_move.argtypes = [
        ctypes.c_int,
        ctypes.c_uint64,
        ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_int)
    ]
    _lib.c_get_best_move.restype = ctypes.c_float

    # 注册初始化函数 c_init_search
    _lib.c_init_search.argtypes = []
    _lib.c_init_search.restype = None

except Exception as e:
    print(f"CRITICAL ERROR: 加载动态库失败: {e}")
    sys.exit(1)


class SearchEngine:
    @staticmethod
    def init_engine():
        """
        初始化搜索引擎（生成随机数表，并清空置换表缓存）。
        应该在程序启动时，以及每局新游戏开始前调用一次。
        """
        _lib.c_init_search()

    @staticmethod
    def get_best_move(depth: int, player_bb: int, opponent_bb: int):
        """
        返回 (best_move, score)
        """
        best_move = ctypes.c_int(-1)
        # 调用 C 函数
        score = _lib.c_get_best_move(
            depth,
            ctypes.c_uint64(player_bb),
            ctypes.c_uint64(opponent_bb),
            ctypes.byref(best_move)
        )

        move_val = best_move.value
        if move_val == -1:
            return None, float(score)
        return move_val, float(score)


SearchEngine.init_engine()
