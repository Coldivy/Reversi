from core.board import Bitboard
from core import constants as c


class Evaluator:
    # 经典的静态权重矩阵：角落分极高，角落旁边分极低
    WEIGHT_MATRIX = [
        100, -20,  10,   5,   5,  10, -20, 100,
        -20, -50,  -2,  -2,  -2,  -2, -50, -20,
        10,  -2,   5,   1,   1,   5,  -2,  10,
        5,  -2,   1,   0,   0,   1,  -2,   5,
        5,  -2,   1,   0,   0,   1,  -2,   5,
        10,  -2,   5,   1,   1,   5,  -2,  10,
        -20, -50,  -2,  -2,  -2,  -2, -50, -20,
        100, -20,  10,   5,   5,  10, -20, 100
    ]

    @staticmethod
    def simple_evaluate(player_bb: int, opponent_bb: int) -> float:
        """
        基础评估：权重矩阵 + 棋子个数差
        """
        score = 0
        for i in range(64):
            if (player_bb >> i) & 1:
                score += Evaluator.WEIGHT_MATRIX[i]
            elif (opponent_bb >> i) & 1:
                score -= Evaluator.WEIGHT_MATRIX[i]

        # 加上行动力因素：你能下的地方越多，分越高
        player_moves = bin(Bitboard.get_legal_moves(
            player_bb, opponent_bb)).count('1')
        opponent_moves = bin(Bitboard.get_legal_moves(
            opponent_bb, player_bb)).count('1')

        score += (player_moves - opponent_moves) * 5

        return float(score)
