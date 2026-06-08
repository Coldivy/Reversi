/**
 * v1_alphabeta_debug.c — Alpha-Beta 剪枝（无置换表、无杀手、无历史启发）
 *
 * 在 negamax 框架上添加 Alpha-Beta 剪枝，走法按静态位置权重排序。
 * 不包含：TT、杀手着法、历史启发表、LMR、Futility、IID。
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

int g_v1_debug_level = 1; /* 0=off, 1=summary, 2=verbose */
long long g_v1_nodes = 0;
long long g_v1_cutoffs = 0;
clock_t g_v1_start_time;

#define DLOG(level, fmt, ...)                                                  \
  do {                                                                         \
    if (g_v1_debug_level >= (level))                                           \
      fprintf(stderr, "[v1_alphabeta] " fmt, ##__VA_ARGS__);                   \
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
/*  走法结构体（用于排序）                                            */
/* ================================================================== */

typedef struct {
  int move;
  int score;
} RatedMove;

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
/*  Negamax with Alpha-Beta（无 TT、无杀手、无历史、无 LMR）          */
/*                                                                     */
/*  走法按静态位置权重排序，提升剪枝效率。                            */
/* ================================================================== */

int negamax(int depth, uint64 P, uint64 O, int alpha, int beta,
            int *best_move) {
  g_v1_nodes++;

  /* 超时检查 */
  if (timed_out()) { g_timeout_occurred = 1; return evaluate(P, O); }

  /* 叶子节点 */
  if (depth == 0)
    return evaluate(P, O);

  uint64 moves = get_legal_moves(P, O);

  /* 无合法走法 → 轮空或终局 */
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) {
      int p_cnt = popcount(P);
      int o_cnt = popcount(O);
      if (p_cnt > o_cnt)
        return WIN_SCORE + (p_cnt - o_cnt);
      if (p_cnt < o_cnt)
        return LOSS_SCORE + (p_cnt - o_cnt);
      return 0;
    }
    return -negamax(depth - 1, O, P, -beta, -alpha, NULL);
  }

  int best_score = LOSS_SCORE;
  int local_best = -1;
  int original_alpha = alpha;

  /* ── 按静态权重排序走法 ── */
  RatedMove rated_moves[64];
  int size = 0;
  uint64 temp = moves;
  while (temp) {
    int idx = __builtin_ctzll(temp);
    int sc = square_weights[idx]; /* 仅用静态权重 */

    /* 插入排序（降序） */
    int j = size;
    while (j > 0 && rated_moves[j - 1].score < sc) {
      rated_moves[j] = rated_moves[j - 1];
      j--;
    }
    rated_moves[j] = (RatedMove){.move = idx, .score = sc};
    size++;
    temp &= temp - 1;
  }

  /* ── 依次搜索 ── */
  for (int i = 0; i < size; i++) {
    /* 超时检查 */
    if (timed_out()) { g_timeout_occurred = 1; break; }

    int move = rated_moves[i].move;
    uint64 nP, nO;
    make_move(P, O, move, &nP, &nO);

    DLOG(2, "  [d=%d] trying %c%c (static=%d) alpha=%d beta=%d\n", depth,
         'A' + (move % 8), '1' + (move / 8), rated_moves[i].score, alpha,
         beta);

    int score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);

    DLOG(2, "  [d=%d] move %c%c = %d\n", depth, 'A' + (move % 8),
         '1' + (move / 8), score);

    if (score > best_score) {
      best_score = score;
      local_best = move;
    }
    if (score > alpha) {
      alpha = score;
    }
    if (alpha >= beta) {
      /* Beta 截断！ */
      g_v1_cutoffs++;
      DLOG(2, "  [d=%d] BETA CUTOFF at %c%c (alpha=%d, beta=%d)\n", depth,
           'A' + (move % 8), '1' + (move / 8), alpha, beta);
      break;
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
  DLOG(1, "=== Alpha-Beta Engine Initialized (no TT/killer/history) ===\n");
}

int c_get_best_move(int depth, uint64 player_bb, uint64 opponent_bb,
                     int *move) {
  g_timeout_occurred = 0;
  g_v1_nodes = 0;
  g_v1_cutoffs = 0;
  g_v1_start_time = clock();

  int num_legal = popcount(get_legal_moves(player_bb, opponent_bb));

  DLOG(1,
       "\n========== Alpha-Beta Search ==========\n"
       "Depth: %d  |  Player: %llu  |  Opponent: %llu\n"
       "Legal moves: %d\n",
       depth, (unsigned long long)player_bb,
       (unsigned long long)opponent_bb, num_legal);

  int best_move_local = -1;
  int score =
      negamax(depth, player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE,
              &best_move_local);

  clock_t elapsed = clock() - g_v1_start_time;
  double time_ms = (double)elapsed / CLOCKS_PER_SEC * 1000.0;
  double nps = (time_ms > 0) ? g_v1_nodes / (time_ms / 1000.0) : 0;

  DLOG(1,
       "========== Search Complete ==========\n"
       "Best move: %d (%c%c)  |  Score: %d\n"
       "Nodes: %lld  |  Cutoffs: %lld (%.1f%%)\n"
       "Time: %.2f ms  |  NPS: %.0f nodes/sec\n"
       "======================================\n",
       best_move_local,
       best_move_local >= 0 ? 'A' + (best_move_local % 8) : '?',
       best_move_local >= 0 ? '1' + (best_move_local / 8) : '?', score,
       g_v1_nodes, g_v1_cutoffs,
       g_v1_nodes > 0 ? (double)g_v1_cutoffs / g_v1_nodes * 100.0 : 0.0,
       time_ms, nps);

  if (move)
    *move = best_move_local;
  return score;
}

int c_get_best_move_timed(int time_limit_ms, uint64 player_bb,
                           uint64 opponent_bb, int *move,
                           int *depth_searched) {
  /* 简化版：固定深度搜索（无迭代加深） */
  int depth;
  if (time_limit_ms <= 0)
    depth = 10;
  else if (time_limit_ms < 100)
    depth = 5;
  else if (time_limit_ms < 500)
    depth = 7;
  else
    depth = 10;

  int result = c_get_best_move(depth, player_bb, opponent_bb, move);
  if (depth_searched)
    *depth_searched = depth;
  return result;
}
