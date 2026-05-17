from core.board import Bitboard
from core import constants as c


class SearchEngine:
    @staticmethod
    def get_best_move(depth: int, player_bb: int, opponent_bb: int, evaluator):
        """
        供外部调用的入口：返回 (best_move, score)
        """
        # 调用核心搜索函数，初始 Alpha 为负无穷，Beta 为正无穷
        score, move = SearchEngine._negamax(
            depth, player_bb, opponent_bb, c.LOSS_SCORE, c.WIN_SCORE, evaluator)
        return move, score

    @staticmethod
    def _negamax(depth: int, player_bb: int, opponent_bb: int, alpha: float, beta: float, evaluator):
        # 1. 终止条件
        if depth == 0 or Bitboard.is_game_over(player_bb, opponent_bb):
            return evaluator(player_bb, opponent_bb), None

        legal_moves = Bitboard.get_legal_moves(player_bb, opponent_bb)

        # 2. 处理“没棋下”的情况 (Pass)
        if legal_moves == 0:
            opp_moves = Bitboard.get_legal_moves(opponent_bb, player_bb)
            if opp_moves == 0:  # 双方都没棋，游戏结束
                # 假设 evaluator 在游戏结束时能返回很大/很小的分
                return evaluator(player_bb, opponent_bb), None

            # 只有自己没棋下，交换对手，深度减1，分数取负
            score, _ = SearchEngine._negamax(
                depth - 1, opponent_bb, player_bb, -beta, -alpha, evaluator)
            return -score, None

        # 3. 正常遍历落子点
        best_score = c.LOSS_SCORE
        best_move = None

        for i in range(64):
            if (legal_moves >> i) & 1:
                # 模拟落子
                new_player_bb, new_opponent_bb = Bitboard.make_move(
                    player_bb, opponent_bb, i)

                # 递归：Alpha-Beta 的范围翻转
                score, _ = SearchEngine._negamax(
                    depth - 1, new_opponent_bb, new_player_bb, -beta, -alpha, evaluator)
                score = -score

                if score > best_score:
                    best_score = score
                    best_move = i

                # --- Alpha-Beta 剪枝 ---
                alpha = max(alpha, score)
                if alpha >= beta:
                    break

        return best_score, best_move

    # 纯Minimax，不剪枝
    # @staticmethod
    # def minimax_search(depth: int, player_bb: int, opponent_bb: int, evaluator):
    #     if depth == 0 or Bitboard.is_game_over(player_bb, opponent_bb):
    #         return evaluator(player_bb, opponent_bb), None
    #
    #     legal_moves = Bitboard.get_legal_moves(player_bb, opponent_bb)
    #
    #     if legal_moves == 0:
    #         opp_moves = Bitboard.get_legal_moves(opponent_bb, player_bb)
    #
    #         if opp_moves == 0:  # 双方都没棋，游戏结束
    #             return evaluator(player_bb, opponent_bb), None
    #
    #         # 计算交换手分数
    #         score, _ = SearchEngine.minimax_search(
    #             depth-1, opponent_bb, player_bb, evaluator)
    #         return -score, None
    #
    #     best_score = c.LOSS_SCORE
    #     best_move = None
    #
    #     for i in range(64):
    #         if (legal_moves >> i) & 1:
    #             new_player_bb, new_opponent_bb = Bitboard.make_move(
    #                 player_bb, opponent_bb, i)
    #             temp_score, _ = SearchEngine.minimax_search(
    #                 depth-1, new_opponent_bb, new_player_bb, evaluator)
    #             temp_score = -temp_score
    #
    #             if temp_score > best_score:
    #                 best_score = temp_score
    #                 best_move = i
    #
    #     return best_score, best_move
