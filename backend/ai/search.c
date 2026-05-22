#include <stdint.h>
#include <stdio.h>

#define LOSS_SCORE -1000000.0f
#define WIN_SCORE 1000000.0f

typedef uint64_t uint64;

// 权重表
static const uint64 MASKS[] = {0x8100000000000081, 0x0042000000004200,
                               0x4281000000008142, 0x2400810000810024,
                               0x1800248181240018, 0x003C424242423C00,
                               0x0000184242180000, 0x0000001818000000};
static const int WEIGHTS[] = {100, -50, -20, 10, 5, -2, 1, 0};

// 位计数
inline int popcount(uint64 x) { return __builtin_popcountll(x); }

// 核心逻辑：获取合法动作
uint64 get_legal_moves(uint64 P, uint64 O) {
  uint64 mask = O & 0x7E7E7E7E7E7E7E7EULL;
  uint64 moves = 0;
  uint64 flip, t;

// 8个方向的遍历简化逻辑
#define DIRECTION(SHIFT, MASK)                                                 \
  t = (P SHIFT) & (MASK);                                                      \
  for (int i = 0; i < 5; ++i)                                                  \
    t |= (t SHIFT) & (MASK);                                                   \
  moves |= (t SHIFT);

  DIRECTION(<< 1, mask) // Left
  DIRECTION(>> 1, mask) // Right
  DIRECTION(<< 8, O)    // Up
  DIRECTION(>> 8, O)    // Down
  DIRECTION(<< 7, mask) // DL
  DIRECTION(>> 9, mask) // UR
  DIRECTION(<< 9, mask) // DR
  DIRECTION(>> 7, mask) // UL

  return moves & ~(P | O);
}

// 执行落子并返回翻转后的位棋盘
void make_move(uint64 P, uint64 O, int move, uint64 *newP, uint64 *newO) {
  uint64 m = 1ULL << move;
  uint64 flipped = 0;
  uint64 mask = O & 0x7E7E7E7E7E7E7E7EULL;

#define FLIP_DIR(SHIFT, MASK)                                                  \
  {                                                                            \
    uint64 t = (m SHIFT) & (MASK);                                             \
    for (int i = 0; i < 5; ++i)                                                \
      t |= (t SHIFT) & (MASK);                                                 \
    if ((t SHIFT) & P)                                                         \
      flipped |= t;                                                            \
  }

  FLIP_DIR(<< 1, mask)
  FLIP_DIR(>> 1, mask)
  FLIP_DIR(<< 8, O)
  FLIP_DIR(>> 8, O)
  FLIP_DIR(<< 7, mask)
  FLIP_DIR(>> 9, mask)
  FLIP_DIR(<< 9, mask)
  FLIP_DIR(>> 7, mask)

  *newP = P | m | flipped;
  *newO = O & ~flipped;
}

// 评估函数
float evaluate(uint64 P, uint64 O) {
  float score = 0;
  for (int i = 0; i < 8; i++) {
    score += popcount(P & MASKS[i]) * WEIGHTS[i];
    score -= popcount(O & MASKS[i]) * WEIGHTS[i];
  }
  uint64 pm = get_legal_moves(P, O);
  uint64 om = get_legal_moves(O, P);
  score += (popcount(pm) - popcount(om)) * 5.0f;
  return score;
}

// Negamax 递归搜索
float negamax(int depth, uint64 P, uint64 O, float alpha, float beta,
              int *best_move) {
  if (depth == 0)
    return evaluate(P, O);

  uint64 moves = get_legal_moves(P, O);
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0)
      return evaluate(P, O); // 游戏结束
    return -negamax(depth - 1, O, P, -beta, -alpha, NULL);
  }

  float best_score = LOSS_SCORE;
  int local_best_move = -1;

  uint64 temp_moves = moves;
  while (temp_moves) {
    // 获取最低位的 1 对应的棋盘索引 (0-63)
    int i = __builtin_ctzll(temp_moves);

    uint64 nP, nO;
    make_move(P, O, i, &nP, &nO);
    float score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);

    if (score > best_score) {
      best_score = score;
      local_best_move = i;
    }
    if (score > alpha)
      alpha = score;
    if (alpha >= beta)
      break; // Alpha-Beta 剪枝

    // 清除最低位的 1，进入下一次迭代
    temp_moves &= temp_moves - 1;
  }

  if (best_move)
    *best_move = local_best_move;
  return best_score;
}

// 终局完美搜索部分
// 终局叶子节点评估：计算纯子数差
// 根据标准黑白棋规则，如果一方子被吃光，另一方获得棋盘上所有的剩余空格
float evaluate_exact_score(uint64 P, uint64 O) {
  int p_cnt = popcount(P);
  int o_cnt = popcount(O);
  if (p_cnt == 0)
    return -64.0f; // 己方被剃光头
  if (o_cnt == 0)
    return 64.0f; // 对方被剃光头
  return (float)(p_cnt - o_cnt);
}

// 终局递归 Solver：没有深度限制，直达游戏结束
float solve(uint64 P, uint64 O, float alpha, float beta, int *best_move) {
  uint64 moves = get_legal_moves(P, O);

  // 如果自己没棋下
  if (moves == 0) {
    // 如果对手也没棋下，说明游戏彻底结束
    if (get_legal_moves(O, P) == 0) {
      return evaluate_exact_score(P, O);
    }
    // 只有自己 Pass，交换棋权，不扣减步数（继续搜索直到终局）
    return -solve(O, P, -beta, -alpha, NULL);
  }

  float best_score = LOSS_SCORE;
  int local_best_move = -1;

  uint64 temp_moves = moves;
  while (temp_moves) {
    int i = __builtin_ctzll(temp_moves);

    uint64 nP, nO;
    make_move(P, O, i, &nP, &nO);

    // 递归，没有 depth - 1 限制
    float score = -solve(nO, nP, -beta, -alpha, NULL);

    if (score > best_score) {
      best_score = score;
      local_best_move = i;
    }
    if (score > alpha)
      alpha = score;
    if (alpha >= beta)
      break; // Alpha-Beta 剪枝

    temp_moves &= temp_moves - 1;
  }

  if (best_move)
    *best_move = local_best_move;
  return best_score;
}

// 导出的接口函数
// 返回值是 score，通过指针返回 best_move

float c_get_best_move(int depth, uint64 player_bb, uint64 opponent_bb,
                      int *move) {
  // 1. 计算棋盘上的空格数
  int occupied_squares = popcount(player_bb | opponent_bb);
  int empty_squares = 64 - occupied_squares;

  // 2. 如果剩余空格 <= 12，自动切换到“终局完美搜索”
  if (empty_squares <= 12) {
    // 使用 solve 替代 negamax，初始分数边界依旧是 LOSS_SCORE 到 WIN_SCORE
    return solve(player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, move);
  }

  // 3. 否则，使用常规的启发式深度限制搜索
  return negamax(depth, player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, move);
}
