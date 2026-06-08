/**
 * v0_minimax_debug.c — 纯 Minimax（无 Alpha-Beta 剪枝）
 *
 * 这是最基础的搜索算法，不做任何剪枝，用于与其他版本对比。
 * 深度限制为 <= 5（因为无剪枝，分支因子 ~10，10^5 = 100K 节点）。
 *
 * 导出接口：c_init_search / c_get_best_move / c_get_best_move_timed
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ================================================================== */
/*  Debug 控制                                                         */
/* ================================================================== */

int g_v0_debug_level = 1; /* 0=off, 1=summary, 2=verbose */
long long g_v0_nodes = 0;
clock_t g_v0_start_time;

#define DLOG(level, fmt, ...)                                                  \
  do {                                                                         \
    if (g_v0_debug_level >= (level))                                           \
      fprintf(stderr, "[v0_minimax] " fmt, ##__VA_ARGS__);                     \
  } while (0)

/* ================================================================== */
/*  超时控制                                                           */
/* ================================================================== */

int g_timeout_occurred = 0;
static clock_t timed_deadline = 0;
static inline int timed_out(void) {
  return timed_deadline > 0 && clock() >= timed_deadline;
}
#define TIMED_OUT_SCORE -200000

void c_set_time_limit(int ms) {
  if (ms > 0)
    timed_deadline = clock() + (clock_t)((double)ms / 1000.0 * CLOCKS_PER_SEC);
  else
    timed_deadline = 0;
}

/* ================================================================== */
/*  常量                                                              */
/* ================================================================== */

#define LOSS_SCORE -100000
#define WIN_SCORE 100000
#define MAX_DEPTH 16

typedef uint64_t uint64;

/* ================================================================== */
/*  位置权重表（与 search.c 完全一致）                                */
/* ================================================================== */

static const uint64 MASKS[] = {
    0x8100000000000081, /* 四角 (100)            */
    0x0042000000004200, /* 角内对角位 (-50)      */
    0x4281000000008142, /* 近角边缘位 (-20)      */
    0x2400810000810024, /* 边缘及内层 (10)       */
    0x1800248181240018, /* 剩余边缘 (5)          */
    0x003C424242423C00, /* 近边内层敏感区 (-2)   */
    0x0000184242180000, /* 中心区外围 (1)        */
    0x0000001818000000  /* 棋盘中心四格 (0)      */
};
static const int WEIGHTS[] = {100, -50, -20, 10, 5, -2, 1, 0};
static int square_weights[64];

/* ================================================================== */
/*  位运算工具                                                        */
/* ================================================================== */

static inline int popcount(uint64 x) { return __builtin_popcountll(x); }

/* ================================================================== */
/*  走法生成 & 执行（与 search.c 一致）                               */
/* ================================================================== */

uint64 get_legal_moves(uint64 P, uint64 O) {
  uint64 mask = O & 0x7E7E7E7E7E7E7E7EULL;
  uint64 moves = 0;
  uint64 t;

#define DIRECTION(SHIFT, MASK)                                                 \
  t = (P SHIFT) & (MASK);                                                      \
  for (int _i = 0; _i < 5; ++_i)                                               \
    t |= (t SHIFT) & (MASK);                                                   \
  moves |= (t SHIFT);

  DIRECTION(<< 1, mask) /* Left  */
  DIRECTION(>> 1, mask) /* Right */
  DIRECTION(<< 8, O)    /* Up    */
  DIRECTION(>> 8, O)    /* Down  */
  DIRECTION(<< 7, mask) /* DL    */
  DIRECTION(>> 9, mask) /* UR    */
  DIRECTION(<< 9, mask) /* DR    */
  DIRECTION(>> 7, mask) /* UL    */

  return moves & ~(P | O);
}

void make_move(uint64 P, uint64 O, int move, uint64 *newP, uint64 *newO) {
  uint64 m = 1ULL << move;
  uint64 flipped = 0;
  uint64 mask = O & 0x7E7E7E7E7E7E7E7EULL;

#define FLIP_DIR(SHIFT, MASK)                                                  \
  {                                                                            \
    uint64 t = (m SHIFT) & (MASK);                                             \
    for (int _i = 0; _i < 5; ++_i)                                             \
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

/* ================================================================== */
/*  评估函数                                                          */
/* ================================================================== */

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

/* ================================================================== */
/*  纯 Minimax（无 Alpha-Beta 剪枝）                                  */
/*                                                                     */
/*  注意：此版本不做任何剪枝，每个深度都需要探索所有合法走法。        */
/*  深度上限设为 MAX_DEPTH=5。                                         */
/* ================================================================== */

int minimax(int depth, uint64 P, uint64 O, int *best_move) {
  g_v0_nodes++;

  /* 超时检查 */
  if (timed_out()) { g_timeout_occurred = 1; return evaluate(P, O); }

  /* 叶子节点：返回静态评估 */
  if (depth == 0) {
    return evaluate(P, O);
  }

  uint64 moves = get_legal_moves(P, O);

  /* 无合法走法 → 轮空或终局 */
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) {
      /* 双方都无法走 → 终局 */
      int p_cnt = popcount(P);
      int o_cnt = popcount(O);
      if (p_cnt > o_cnt)
        return WIN_SCORE + (p_cnt - o_cnt);
      if (p_cnt < o_cnt)
        return LOSS_SCORE + (p_cnt - o_cnt);
      return 0;
    }
    /* 轮空，对方继续走 */
    int dummy;
    return -minimax(depth - 1, O, P, &dummy);
  }

  int best_score = LOSS_SCORE;
  int local_best = -1;

  /* 遍历所有合法走法 */
  uint64 temp = moves;
  while (temp) {
    int move = __builtin_ctzll(temp);
    temp &= temp - 1;

    /* 超时检查 */
    if (timed_out()) { g_timeout_occurred = 1; break; }

    uint64 nP, nO;
    make_move(P, O, move, &nP, &nO);

    DLOG(2, "  [depth=%d] trying move %c%c\n", depth, 'A' + (move % 8),
         '1' + (move / 8));

    int score = -minimax(depth - 1, nO, nP, NULL);

    DLOG(2, "  [depth=%d] move %c%c -> score %d\n", depth, 'A' + (move % 8),
         '1' + (move / 8), score);

    if (score > best_score) {
      best_score = score;
      local_best = move;
    }
  }

  if (best_move)
    *best_move = local_best;
  return best_score;
}

/* ================================================================== */
/*  导出接口                                                          */
/* ================================================================== */

void c_init_search(void) {
  /* 初始化 square_weights */
  for (int pos = 0; pos < 64; pos++) {
    uint64 bit = 1ULL << pos;
    square_weights[pos] = 0;
    for (int i = 0; i < 8; i++) {
      if (MASKS[i] & bit) {
        square_weights[pos] = WEIGHTS[i];
        break;
      }
    }
  }
  DLOG(1, "=== Pure Minimax Engine Initialized (max_depth=%d) ===\n",
       MAX_DEPTH);
}

int c_get_best_move(int depth, uint64 player_bb, uint64 opponent_bb,
                     int *move) {
  g_timeout_occurred = 0;
  g_v0_nodes = 0;
  g_v0_start_time = clock();

  DLOG(1,
       "\n========== Pure Minimax Search ==========\n"
       "Depth: %d  |  Player: %llu  |  Opponent: %llu\n"
       "Legal moves: %llu\n",
       depth, (unsigned long long)player_bb,
       (unsigned long long)opponent_bb,
       (unsigned long long)get_legal_moves(player_bb, opponent_bb));

  int best_move_local = -1;
  int score = minimax(depth, player_bb, opponent_bb, &best_move_local);

  clock_t elapsed = clock() - g_v0_start_time;
  double time_ms = (double)elapsed / CLOCKS_PER_SEC * 1000.0;
  double nps = (time_ms > 0) ? g_v0_nodes / (time_ms / 1000.0) : 0;

  DLOG(1,
       "========== Search Complete ==========\n"
       "Best move: %d (%c%c)  |  Score: %d\n"
       "Nodes visited: %lld  |  Time: %.2f ms  |  NPS: %.0f\n"
       "========================================\n",
       best_move_local,
       best_move_local >= 0 ? 'A' + (best_move_local % 8) : '?',
       best_move_local >= 0 ? '1' + (best_move_local / 8) : '?', score,
       g_v0_nodes, time_ms, nps);

  if (move)
    *move = best_move_local;
  return score;
}

int c_get_best_move_timed(int time_limit_ms, uint64 player_bb,
                           uint64 opponent_bb, int *move,
                           int *depth_searched) {
  /* 纯 Minimax 不支持真正的限时搜索，使用固定深度 */
  int depth = (time_limit_ms > 0 && time_limit_ms < 100) ? 3 : MAX_DEPTH;
  int result = c_get_best_move(depth, player_bb, opponent_bb, move);
  if (depth_searched)
    *depth_searched = depth;
  return result;
}
