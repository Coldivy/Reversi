# import unittest

class Bitboard:
    # Bitboard原理：使用一个8位16进制（64位二进制）来表示棋盘，有子的那一位记录为1，否则0。用运算表示移位。一个数不能表示颜色，所以分别使用：black_board和white_board。
    # 方向位移常量
    # N: -8, S: +8, W: -1, E: +1, NW: -9, NE: -7, SW: +7, SE: +9

    # 掩码：防止位移时左右“越界”绕到另一行
    NOT_A_COL = 0xfefefefefefefefe  # 除了第 A 列以外的所有位
    NOT_H_COL = 0x7f7f7f7f7f7f7f7f  # 除了第 H 列以外的所有位
    NOT_AB_COL = 0xfcfcfcfcfcfcfcfc  # 除了 A, B 列
    NOT_GH_COL = 0x3f3f3f3f3f3f3f3f  # 除了 G, H 列

    @staticmethod
    def get_legal_moves(player_bb: int, opponent_bb: int) -> int:
        """
        计算当前玩家的所有合法落子点
        返回一个 64 位整数，对应位为 1 表示可落子
        """
        empty_bb = ~(player_bb | opponent_bb) & 0xffffffffffffffff
        legal_moves = 0

        # 检查 8 个方向
        # 这里以向左(West)为例：
        # 1. 找玩家棋子左侧紧邻对方棋子的位
        # 2. 连续向左探测，直到遇到空位

        # 简化版的 8 方向并行探测逻辑
        for shift, mask in [
            (-1, Bitboard.NOT_H_COL),  # West
            (1, Bitboard.NOT_A_COL),  # East
            (-8, 0xffffffffffffffff),  # North
            (8, 0xffffffffffffffff),  # South
            (-9, Bitboard.NOT_H_COL),  # North-West
            (-7, Bitboard.NOT_A_COL),  # North-East
            (7, Bitboard.NOT_H_COL),  # South-West
            (9, Bitboard.NOT_A_COL)  # South-East
        ]:
            candidates = opponent_bb & Bitboard._shift(player_bb, shift, mask)
            while candidates != 0:
                # 寻找那些在对方棋子序列终点之后的空位
                potential_moves = Bitboard._shift(candidates, shift, mask)
                legal_moves |= (potential_moves & empty_bb)
                # 继续向该方向滑动探测（处理吃掉多颗子的情况）
                candidates = opponent_bb & potential_moves

        return legal_moves

    @staticmethod
    def make_move(player_bb: int, opponent_bb: int, move_idx: int):
        """
        在 move_idx 位置落子，并返回更新后的 (new_player_bb, new_opponent_bb)
        move_idx: 0-63
        """
        move_bb = 1 << move_idx
        flipped_total = 0

        for shift, mask in [
            (-1, Bitboard.NOT_H_COL),  # West
            (1, Bitboard.NOT_A_COL),  # East
            (-8, 0xffffffffffffffff),  # North
            (8, 0xffffffffffffffff),  # South
            (-9, Bitboard.NOT_H_COL),  # North-West
            (-7, Bitboard.NOT_A_COL),  # North-East
            (7, Bitboard.NOT_H_COL),  # South-West
            (9, Bitboard.NOT_A_COL)  # South-East
        ]:
            flipped_in_dir = 0
            candidates = Bitboard._shift(move_bb, shift, mask)

            while candidates != 0 and (candidates & opponent_bb) != 0:
                flipped_in_dir |= candidates
                candidates = Bitboard._shift(candidates, shift, mask)

            # 如果序列终点是玩家自己的棋子，则这一排都被翻转
            if (candidates & player_bb) != 0:
                flipped_total |= flipped_in_dir

        new_player_bb = player_bb | move_bb | flipped_total
        new_opponent_bb = opponent_bb & ~flipped_total
        return new_player_bb, new_opponent_bb

    @staticmethod
    def _shift(bb: int, step: int, mask: int) -> int:
        """辅助函数：处理位移并应用遮罩防止越界"""
        if step > 0:
            return (bb << step) & mask
        else:
            return (bb >> abs(step)) & mask

    @staticmethod
    def from_array(grid: list[list[int]]) -> tuple[int, int]:
        """将前端传来的 8x8 数组转为 (black_bb, white_bb)"""
        black_bb, white_bb = 0, 0
        for r in range(8):
            for c in range(8):
                idx = r * 8 + c
                if grid[r][c] == 1:
                    black_bb |= (1 << idx)
                elif grid[r][c] == -1:
                    white_bb |= (1 << idx)
        return black_bb, white_bb

    @staticmethod
    def to_array(black_bb: int, white_bb: int) -> list[list[int]]:
        """将 Bitboard 转回 8x8 数组"""
        grid = [[0 for _ in range(8)] for _ in range(8)]
        for i in range(64):
            r, c = i // 8, i % 8
            if (black_bb >> i) & 1:
                grid[r][c] = 1
            elif (white_bb >> i) & 1:
                grid[r][c] = -1
        return grid

    @staticmethod
    def is_game_over(black_bb: int, white_bb: int) -> bool:
        """
        判断游戏是否结束
        当黑棋和白棋都无法找到合法落子点时，游戏结束
        """
        # 1. 检查黑棋是否还有合法落子
        if Bitboard.get_legal_moves(black_bb, white_bb) != 0:
            return False

        # 2. 检查白棋是否还有合法落子
        if Bitboard.get_legal_moves(white_bb, black_bb) != 0:
            return False

        # 只有当两边都返回 0 时，才返回 True
        return True


# 假设你的 Bitboard 类代码在这里
# class Bitboard: ...


# class TestBitboard(unittest.TestCase):
#
#     def setUp(self):
#         # 构造经典的黑白棋初始开局 (8x8)
#         # 1 为黑 (Black), -1 为白 (White), 0 为空
#         self.init_grid = [
#             [0, 0, 0, 0, 0, 0, 0, 0],
#             [0, 0, 0, 0, 0, 0, 0, 0],
#             [0, 0, 0, 0, 0, 0, 0, 0],
#             [0, 0, 0, -1, 1, 0, 0, 0],  # D4(白), E4(黑)
#             [0, 0, 0, 1, -1, 0, 0, 0],  # D5(黑), E5(白)
#             [0, 0, 0, 0, 0, 0, 0, 0],
#             [0, 0, 0, 0, 0, 0, 0, 0],
#             [0, 0, 0, 0, 0, 0, 0, 0],
#         ]
#
#     def test_array_conversion(self):
#         """测试 2D 数组到 Bitboard 的来回转换是否无损"""
#         b_bb, w_bb = Bitboard.from_array(self.init_grid)
#         recovered_grid = Bitboard.to_array(b_bb, w_bb)
#         self.assertEqual(self.init_grid, recovered_grid)
#
#     def test_initial_legal_moves(self):
#         """测试初始状态下黑棋的合法落子点"""
#         b_bb, w_bb = Bitboard.from_array(self.init_grid)
#
#         # 获取黑棋的合法步
#         legal_bb = Bitboard.get_legal_moves(b_bb, w_bb)
#
#         # 根据黑白棋规则，黑棋开局有 4 个合法步：
#         # D3 (row 2, col 3) -> index 19
#         # C4 (row 3, col 2) -> index 26
#         # F5 (row 4, col 5) -> index 37
#         # E6 (row 5, col 4) -> index 44
#         expected_indices = [19, 26, 37, 44]
#         expected_bb = sum(1 << idx for idx in expected_indices)
#
#         self.assertEqual(legal_bb, expected_bb, "初始合法步计算错误！")
#
#     def test_make_move(self):
#         """测试落子和翻转逻辑"""
#         b_bb, w_bb = Bitboard.from_array(self.init_grid)
#
#         # 黑棋下在 D3 (index 19)
#         # 应该翻转 D4 (index 27) 位置的白棋
#         move_idx = 19
#         new_b_bb, new_w_bb = Bitboard.make_move(b_bb, w_bb, move_idx)
#
#         # 验证 19 号位现在是黑棋
#         self.assertTrue((new_b_bb & (1 << 19)) != 0)
#
#         # 验证 27 号位（原先是白棋）现在变成了黑棋
#         self.assertTrue((new_b_bb & (1 << 27)) != 0, "没有成功翻转对方棋子")
#         self.assertTrue((new_w_bb & (1 << 27)) == 0, "对方被翻转的棋子没有被清除")
#
#     def test_edge_wrapping(self):
#         """【关键】测试边界防缠绕 (Mask) 是否生效"""
#         # 我们故意在 H 列放黑棋，下一行的 A 列放白棋
#         # 如果 Mask 没生效，黑棋向右(East)探测时会错误地跨行吃到白棋
#         b_bb = 1 << 23  # H3 (第3行最右)
#         w_bb = 1 << 24  # A4 (第4行最左)
#
#         # 黑棋的合法步
#         legal_bb = Bitboard.get_legal_moves(b_bb, w_bb)
#
#         # 如果存在跨行 Bug，25号位会被认为是合法步
#         # 由于我们加了正确的 NOT_H_COL 掩码，这里不应该有任何因为跨行产生的合法步
#         self.assertTrue((legal_bb & (1 << 25)) == 0,
#                         "检测到跨行越界 Bug！East方向Mask失效")
#
#         # 同理测试向左(West)越界
#         b_bb2 = 1 << 16  # A3 (第3行最左)
#         w_bb2 = 1 << 15  # H2 (第2行最右)
#         legal_bb2 = Bitboard.get_legal_moves(b_bb2, w_bb2)
#         self.assertTrue((legal_bb2 & (1 << 14)) == 0,
#                         "检测到跨行越界 Bug！West方向Mask失效")
#
#
# # 提供一个辅助打印函数，方便肉眼 debug (非测试必需)
# def print_bitboard(bb: int):
#     """可视化打印一个 64位 整数"""
#     print("-" * 17)
#     for r in range(8):
#         row_str = ""
#         for c in range(8):
#             idx = r * 8 + c
#             if (bb & (1 << idx)) != 0:
#                 row_str += "1 "
#             else:
#                 row_str += ". "
#         print(row_str)
#     print("-" * 17)
#
#
# if __name__ == '__main__':
#     unittest.main()
