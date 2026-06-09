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
