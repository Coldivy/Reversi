# --- 棋盘基础配置 ---
BOARD_SIZE = 8
TOTAL_CELLS = 64

# --- 玩家标识 (与前端 TypeScript 保持一致) ---
BLACK = 1    # 黑棋
WHITE = -1   # 白棋
EMPTY = 0    # 空位

# --- 初始棋盘的位看板表示 (Bitboard) ---
# 初始状态：
#   3 4 (列)
# 3 W B
# 4 B W
# 对应索引：27(W), 28(B), 35(B), 36(W)
INITIAL_BLACK_BB = (1 << 28) | (1 << 35)
INITIAL_WHITE_BB = (1 << 27) | (1 << 36)

# --- 位运算遮罩 (用于防止位移时逻辑越界) ---
# 这些遮罩用于 bitboard.py 逻辑
NOT_A_COL = 0xfefefefefefefefe  # 除了第 1 列 (A列) 以外的所有位
NOT_H_COL = 0x7f7f7f7f7f7f7f7f  # 除了第 8 列 (H列) 以外的所有位

# --- AI 搜索配置 ---
DEFAULT_SEARCH_DEPTH = 6  # 默认 Minimax 搜索深度
WIN_SCORE = 1000000       # 胜局评分（极大值）
LOSS_SCORE = -1000000     # 败局评分（极小值）

# --- PyTorch 模型相关 ---
MODEL_PATH = "models/value_net.pth"
INPUT_CHANNELS = 2        # 输入 Tensor 的通道数 (黑棋层, 白棋层)

# --- 坐标映射辅助 ---
# 将方向名映射到索引偏移量
DIRECTIONS = {
    "N": -8, "S":   8, "W": -1, "E":   1,
    "NW": -9, "NE": -7, "SW":  7, "SE":  9
}
