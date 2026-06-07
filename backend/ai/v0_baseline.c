#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOSS_SCORE -100000
#define WIN_SCORE 100000

typedef uint64_t uint64;

// 权重表
static const uint64 MASKS[] = {0x8100000000000081, 0x0042000000004200,
                               0x4281000000008142, 0x2400810000810024,
                               0x1800248181240018, 0x003C424242423C00,
                               0x0000184242180000, 0x0000001818000000};
static const int WEIGHTS[] = {100, -50, -20, 10, 5, -2, 1, 0};
static int square_weights[64];

// 分配 2 个杀手着法位
static int killer_moves[64][2];

// 位计数
inline int popcount(uint64 x) { return __builtin_popcountll(x); }

// 定义带分数的走法
typedef struct {
  int move;  // 棋盘索引 (0-63)
  int score; // 该位置的权重得分
} RatedMove;

// 定义置换表的结构体
#define TT_EXACT 0
#define TT_ALPHA 1
#define TT_BETA 2

typedef struct {
  uint64 key;
  int score;
  int best_move;
  int depth;
  uint8_t flag;
} TTEntry;

// 分配表大小
#define TT_SIZE (1 << 20)
#define TT_MASK (TT_SIZE - 1)
TTEntry transposition_table[TT_SIZE];

// 64 位随机数生成器
static uint64_t xorshift64_state = 88172645463325252ULL; // seed
uint64_t next_random64() {
  xorshift64_state ^= xorshift64_state << 13;
  xorshift64_state ^= xorshift64_state >> 7;
  xorshift64_state ^= xorshift64_state << 17;
  return xorshift64_state;
}

// 声明全局变量
uint64 zobrist_P[64];
uint64 zobrist_O[64];
bool is_initialized = false;

// 计算哈希值
uint64 get_zobrist_key(uint64 P, uint64 O) {
  uint64 key = 0;
  uint64 temp_P = P;
  while (temp_P) {
    int i = __builtin_ctzll(temp_P);
    key ^= zobrist_P[i];
    temp_P &= temp_P - 1;
  }
  uint64 temp_O = O;
  while (temp_O) {
    int i = __builtin_ctzll(temp_O);
    key ^= zobrist_O[i];
    temp_O &= temp_O - 1;
  }

  return key;
}

// 查找置换表
bool tt_lookup(uint64 key, int depth, int alpha, int beta, int *score,
               int *best_move) {
  uint64 index = key & TT_MASK;
  TTEntry *entry = &transposition_table[index];
  if (entry->key == key) {
    *best_move = entry->best_move;
    if (entry->depth >= depth) {
      if (entry->flag == TT_EXACT) {
        *score = entry->score;
        return true;
      }
      if (entry->flag == TT_ALPHA && entry->score <= alpha) {
        *score = entry->score;
        return true;
      }
      if (entry->flag == TT_BETA && entry->score >= beta) {
        *score = entry->score;
        return true;
      }
    }
  }
  return false;
}

// 储存置换表
void tt_store(uint64 key, int depth, int score, int alpha, int beta,
              int best_move) {
  uint64 index = key & TT_MASK;
  TTEntry *entry = &transposition_table[index];

  // 冲突解决：深度优先
  if (entry->key == 0 || depth >= entry->depth || entry->key != key) {
    uint8_t flag = TT_EXACT;
    if (score <= alpha) {
      flag = TT_ALPHA;
    } else if (score >= beta) {
      flag = TT_BETA;
    }

    *entry = (TTEntry){.key = key,
                       .score = score,
                       .best_move = best_move,
                       .flag = flag,
                       .depth = depth};
  }
}

// 获取合法动作
uint64 get_legal_moves(uint64 P, uint64 O) {
  uint64 mask = O & 0x7E7E7E7E7E7E7E7EULL;
  uint64 moves = 0;
  uint64 flip, t;

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
int evaluate(uint64 P, uint64 O) {
  int score = 0;
  for (int i = 0; i < 8; i++) {
    score += popcount(P & MASKS[i]) * WEIGHTS[i];
    score -= popcount(O & MASKS[i]) * WEIGHTS[i];
  }
  uint64 pm = get_legal_moves(P, O);
  uint64 om = get_legal_moves(O, P);
  score += (popcount(pm) - popcount(om)) * 5;
  return score;
}

// Negamax 递归搜索
int negamax(int depth, uint64 P, uint64 O, int alpha, int beta,
            int *best_move) {
  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1;
  int tt_score = -1;

  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);

  if (hit) {
    if (best_move) {
      *best_move = tt_move;
    }
    return tt_score;
  }

  if (depth == 0)
    return evaluate(P, O);

  uint64 moves = get_legal_moves(P, O);
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) {
      int p_cnt = popcount(P);
      int o_cnt = popcount(O);
      if (p_cnt > o_cnt)
        return WIN_SCORE + (p_cnt - o_cnt); // 给予极高的赢棋奖励
      if (p_cnt < o_cnt)
        return LOSS_SCORE + (p_cnt - o_cnt); // 给予极高的输棋惩罚
      return 0;                              // 平局
    }
    return -negamax(depth - 1, O, P, -beta, -alpha, NULL);
  }

  int best_score = LOSS_SCORE;
  int local_best_move = -1;
  bool is_first_move = true;

  // 优先搜索tt_move
  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);
    int score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);

    is_first_move = false;

    if (score > best_score) {
      best_score = score;
      local_best_move = tt_move;
    }
    if (score > alpha)
      alpha = score;
    if (alpha >= beta)
      goto end_search;
  }

  uint64 temp_moves = moves;
  if (tt_move != -1) {
    temp_moves &= ~(1ULL << tt_move); // 去掉先前优先搜索的那步
  }

  RatedMove rated_moves[64];
  uint64 iter_moves = temp_moves;
  int size = 0;
  while (iter_moves) {
    int index = __builtin_ctzll(iter_moves);
    int score = square_weights[index];

    // 杀手着法加分
    if (index == killer_moves[depth][0]) {
      score += 10000;
    } else if (index == killer_moves[depth][1]) {
      score += 5000;
    }

    int j = size;
    while (j > 0 && rated_moves[j - 1].score < score) {
      rated_moves[j] = rated_moves[j - 1];
      j--;
    }
    rated_moves[j] = (RatedMove){.move = index, .score = score};
    size++;

    iter_moves &= iter_moves - 1;
  }

  for (int idx = 0; idx < size; idx++) {
    int i = rated_moves[idx].move;
    uint64 nP, nO;
    make_move(P, O, i, &nP, &nO);
    int score;

    if (is_first_move) {
      // 没搜索过任何棋
      score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);
      is_first_move = false; // 置为 false
    } else {
      // 核心 PVS 逻辑：不是第一步棋，先用 [-alpha-1, -alpha] 零窗口进行快速探测
      score = -negamax(depth - 1, nO, nP, -alpha - 1, -alpha, NULL);
      // 如果快速探测失败（Fail-High，说明这步棋意外地好，得分超越了 alpha）
      // 且它没有直接触发 beta 剪枝（score < beta），必须用全窗口重新搜索它
      if (score > alpha && score < beta) {
        score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);
      }
    }

    if (score > best_score) {
      best_score = score;
      local_best_move = i;
    }
    if (score > alpha)
      alpha = score;
    if (alpha >= beta) {
      // 记录杀手着法
      if (i != tt_move) {
        if (killer_moves[depth][0] != i) {
          // 将原来的第一杀手挤到第二顺位，新杀手成为第一顺位
          killer_moves[depth][1] = killer_moves[depth][0];
          killer_moves[depth][0] = i;
        }
      }
      break; // Alpha-Beta 剪枝
    }
  }

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);

  if (best_move)
    *best_move = local_best_move;
  return best_score;
}

// 终局完美搜索部分
int evaluate_exact_score(uint64 P, uint64 O) {
  int p_cnt = popcount(P);
  int o_cnt = popcount(O);
  if (p_cnt == 0)
    return -64; // 己方无子
  if (o_cnt == 0)
    return 64; // 对方无子
  return (int)(p_cnt - o_cnt);
}

// 终局递归 solve
int solve(uint64 P, uint64 O, int alpha, int beta, int *best_move) {
  int occupied = popcount(P | O);
  int depth = 64 - occupied;

  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1;
  int tt_score = -1;

  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);

  if (hit) {
    if (best_move) {
      *best_move = tt_move;
    }
    return tt_score;
  }

  uint64 moves = get_legal_moves(P, O);

  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) {
      return evaluate_exact_score(P, O);
    }
    return -solve(O, P, -beta, -alpha, NULL);
  }

  int best_score = LOSS_SCORE;
  int local_best_move = -1;

  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);
    int score = -solve(nO, nP, -beta, -alpha, NULL);

    if (score > best_score) {
      best_score = score;
      local_best_move = tt_move;
    }
    if (score > alpha)
      alpha = score;
    if (alpha >= beta)
      goto end_search;
  }

  uint64 temp_moves = moves;

  if (tt_move != -1) {
    temp_moves &= ~(1ULL << tt_move);
  }

  while (temp_moves) {
    int i = __builtin_ctzll(temp_moves);

    uint64 nP, nO;
    make_move(P, O, i, &nP, &nO);

    int score = -solve(nO, nP, -beta, -alpha, NULL);

    if (score > best_score) {
      best_score = score;
      local_best_move = i;
    }
    if (score > alpha)
      alpha = score;
    if (alpha >= beta)
      break;

    temp_moves &= temp_moves - 1;
  }

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);

  if (best_move)
    *best_move = local_best_move;
  return best_score;
}

// 导出的接口函数
void c_init_search() {
  for (int i = 0; i < 64; i++) {
    zobrist_P[i] = next_random64();
    zobrist_O[i] = next_random64();
  }
  memset(transposition_table, 0, sizeof(transposition_table));

  for (int pos = 0; pos < 64; pos++) {
    uint64_t bit = 1ULL << pos;
    square_weights[pos] = 0;
    for (int i = 0; i < 8; i++) {
      if (MASKS[i] & bit) {
        square_weights[pos] = WEIGHTS[i];
        break;
      }
    }
  }

  memset(killer_moves, 0, sizeof(killer_moves));
}

int c_get_best_move(int depth, uint64 player_bb, uint64 opponent_bb,
                    int *move) {
  if (move)
    *move = -1;

  int occupied_squares = popcount(player_bb | opponent_bb);
  int empty_squares = 64 - occupied_squares;

  // 用静态变量标记是否已经为终局完美搜索重置过置换表
  static bool tt_cleared_for_endgame = false;

  if (empty_squares <= 12) {
    // 从常规搜索首次切换到终局搜索时，必须清空置换表，防止缓存污染
    if (!tt_cleared_for_endgame) {
      memset(transposition_table, 0, sizeof(transposition_table));
      tt_cleared_for_endgame = true;
    }
    return solve(player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, move);
  } else {
    // 在非终局阶段，将该标记重置（为下一盘棋做准备）
    tt_cleared_for_endgame = false;
  }

  int score = LOSS_SCORE;
  int temp_move = -1;

  // 迭代加深
  for (int d = 1; d <= depth; d++) {
    int current_best_move = -1;

    // 窗口逐步放大
    if (d >= 3) {
      int margin = 50;
      int alpha = score - margin;
      int beta = score + margin;

      while (1) {
        if (alpha < LOSS_SCORE)
          alpha = LOSS_SCORE;
        if (beta > WIN_SCORE)
          beta = WIN_SCORE;

        current_best_move = -1;
        int val =
            negamax(d, player_bb, opponent_bb, alpha, beta, &current_best_move);

        if (val <= alpha) {
          if (alpha == LOSS_SCORE) {
            score = val;
            break;
          }
          margin *= 2;
          alpha = val - margin;
          beta = val + margin;
          continue;
        } else if (val >= beta) {
          if (beta == WIN_SCORE) {
            score = val;
            break;
          }
          margin *= 2;
          beta = val + margin;
          alpha = val - margin;
          continue;
        } else {
          score = val;
          break;
        }
      }
    } else {
      score = negamax(d, player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE,
                      &current_best_move);
    }

    if (current_best_move != -1) {
      temp_move = current_best_move;
    }
  }

  if (move) {
    *move = temp_move;
  }

  return score;
}
