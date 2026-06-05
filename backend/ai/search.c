#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

// 定义置换表的结构体
#define TT_EXACT 0
#define TT_ALPHA 1
#define TT_BETA 2

typedef struct {
  uint64 key;
  float score;
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
bool tt_lookup(uint64 key, int depth, float alpha, float beta, float *score,
               int *best_move) {
  uint64 index = key & TT_MASK;
  TTEntry *entry = &transposition_table[index];
  if (entry->key == key) {
    *best_move = entry->best_move;
    if (entry->depth >= depth) {
      // 根据不同的 Flag 结合当前 alpha/beta 边界进行判断
      if (entry->flag == TT_EXACT) {
        *score = entry->score;
        return true;
      }
      if (entry->flag == TT_ALPHA && entry->score <= alpha) {
        *score = alpha;
        return true;
      }
      if (entry->flag == TT_BETA && entry->score >= beta) {
        *score = beta;
        return true;
      }
    }
  }
  return false;
}

// 储存置换表
void tt_store(uint64 key, int depth, float score, float alpha, float beta,
              int best_move) {
  uint64 index = key & TT_MASK;
  TTEntry *entry = &transposition_table[index];

  // 冲突解决：深度优先
  if (entry->key == 0 || depth >= entry->depth || entry->key != key) {
    // 计算flag
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
  float original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1;
  float tt_score = -1;

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
      return 0.0f;                           // 平局
    }
    return -negamax(depth - 1, O, P, -beta, -alpha, NULL);
  }

  float best_score = LOSS_SCORE;
  int local_best_move = -1;

  // 优先搜索tt_move
  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);
    float score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);

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

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);

  if (best_move)
    *best_move = local_best_move;
  return best_score;
}

// 终局完美搜索部分
// 终局叶子节点评估：计算纯子数差
// 根据标准黑白棋规
float evaluate_exact_score(uint64 P, uint64 O) {
  int p_cnt = popcount(P);
  int o_cnt = popcount(O);
  if (p_cnt == 0)
    return -64.0f; // 己方无子
  if (o_cnt == 0)
    return 64.0f; // 对方无子
  return (float)(p_cnt - o_cnt);
}

// 终局递归 solve：没有深度限制，直达游戏结束
float solve(uint64 P, uint64 O, float alpha, float beta, int *best_move) {
  // 直接将空位数作为depth
  int occupied = popcount(P | O);
  int depth = 64 - occupied;

  float original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1;
  float tt_score = -1;

  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);

  if (hit) {
    if (best_move) {
      *best_move = tt_move;
    }
    return tt_score;
  }

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

  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);
    float score = -solve(nO, nP, -beta, -alpha, NULL);

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

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);

  if (best_move)
    *best_move = local_best_move;
  return best_score;
}

// 导出的接口函数
// 初始化
void c_init_search() {
  for (int i = 0; i < 64; i++) {
    zobrist_P[i] = next_random64();
    zobrist_O[i] = next_random64();
  }
  // 每次调用都会清空置换表，适合新一局游戏开始时重置
  memset(transposition_table, 0, sizeof(transposition_table));
}

// 查找。通过指针返回 best_move
float c_get_best_move(int depth, uint64 player_bb, uint64 opponent_bb,
                      int *move) {
  if (move)
    *move = -1;

  // 计算棋盘上的空格数
  int occupied_squares = popcount(player_bb | opponent_bb);
  int empty_squares = 64 - occupied_squares;

  // 如果剩余空格 <= 12，自动切换到“终局完美搜索”
  if (empty_squares <= 12) {
    // 使用 solve 替代 negamax，初始分数边界依旧是 LOSS_SCORE 到 WIN_SCORE
    return solve(player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, move);
  }

  // 否则，使用常规的启发式深度限制搜索
  return negamax(depth, player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, move);
}
