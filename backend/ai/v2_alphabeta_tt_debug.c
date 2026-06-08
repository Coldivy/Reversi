/**
 * v2_alphabeta_tt_debug.c — Alpha-Beta 剪枝 + 置换表（TT）
 *
 * 在 negamax + Alpha-Beta 基础上，添加 Zobrist 哈希置换表。
 * 不包含：杀手着法、历史启发表、LMR、Futility、IID。
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

int g_v2_debug_level = 1; /* 0=off, 1=summary, 2=verbose */
long long g_v2_nodes = 0;
long long g_v2_cutoffs = 0;
long long g_v2_tt_hits = 0;
long long g_v2_tt_stores = 0;
clock_t g_v2_start_time;

#define DLOG(level, fmt, ...)                                                  \
  do {                                                                         \
    if (g_v2_debug_level >= (level))                                           \
      fprintf(stderr, "[v2_alphabeta_tt] " fmt, ##__VA_ARGS__);                \
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
/*  位置权重表                                                        */
/* ================================================================== */

static const uint64 MASKS[] = {
    0x8100000000000081, 0x0042000000004200, 0x4281000000008142,
    0x2400810000810024, 0x1800248181240018, 0x003C424242423C00,
    0x0000184242180000, 0x0000001818000000,
};
static const int WEIGHTS[] = {100, -50, -20, 10, 5, -2, 1, 0};
static int square_weights[64];

/* ================================================================== */
/*  走法结构体                                                        */
/* ================================================================== */

typedef struct {
  int move;
  int score;
} RatedMove;

/* ================================================================== */
/*  置换表（TT）                                                      */
/* ================================================================== */

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

/* 使用 2^18 = 262144 条目，足够覆盖搜索空间 */
#define TT_SIZE (1 << 18)
#define TT_MASK (TT_SIZE - 1)
TTEntry transposition_table[TT_SIZE];

/* ================================================================== */
/*  Zobrist 哈希                                                      */
/* ================================================================== */

static uint64_t xorshift64_state = 88172645463325252ULL;

uint64_t next_random64(void) {
  xorshift64_state ^= xorshift64_state << 13;
  xorshift64_state ^= xorshift64_state >> 7;
  xorshift64_state ^= xorshift64_state << 17;
  return xorshift64_state;
}

uint64 zobrist_P[64];
uint64 zobrist_O[64];

uint64 get_zobrist_key(uint64 P, uint64 O) {
  uint64 key = 0;
  uint64 t = P;
  while (t) {
    int i = __builtin_ctzll(t);
    key ^= zobrist_P[i];
    t &= t - 1;
  }
  t = O;
  while (t) {
    int i = __builtin_ctzll(t);
    key ^= zobrist_O[i];
    t &= t - 1;
  }
  return key;
}

/* ================================================================== */
/*  置换表存取                                                        */
/* ================================================================== */

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

void tt_store(uint64 key, int depth, int score, int alpha, int beta,
              int best_move) {
  uint64 index = key & TT_MASK;
  TTEntry *entry = &transposition_table[index];

  /* 深度优先替换策略 */
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

/* ================================================================== */
/*  位运算工具                                                        */
/* ================================================================== */

static inline int popcount(uint64 x) { return __builtin_popcountll(x); }

/* ================================================================== */
/*  走法生成 & 执行                                                   */
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

  DIRECTION(<< 1, mask)
  DIRECTION(>> 1, mask)
  DIRECTION(<< 8, O)
  DIRECTION(>> 8, O)
  DIRECTION(<< 7, mask)
  DIRECTION(>> 9, mask)
  DIRECTION(<< 9, mask)
  DIRECTION(>> 7, mask)

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
/*  Negamax + Alpha-Beta + TT（无杀手、历史、LMR、Futility、IID）     */
/* ================================================================== */

int negamax(int depth, uint64 P, uint64 O, int alpha, int beta,
            int *best_move) {
  g_v2_nodes++;

  /* 超时检查 */
  if (timed_out()) { g_timeout_occurred = 1; return evaluate(P, O); }

  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);

  /* ── 1. 置换表查找 ── */
  int tt_move = -1;
  int tt_score = -1;
  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) {
    g_v2_tt_hits++;
    DLOG(2, "  [d=%d] TT hit! score=%d, move=%d\n", depth, tt_score, tt_move);
    if (best_move)
      *best_move = tt_move;
    return tt_score;
  }

  /* ── 2. 叶子节点 ── */
  if (depth == 0)
    return evaluate(P, O);

  uint64 moves = get_legal_moves(P, O);

  /* ── 3. 无合法走法 → 轮空或终局 ── */
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) {
      int p_cnt = popcount(P), o_cnt = popcount(O);
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

  /* ── 4. 优先搜索 TT 提示的走法 ── */
  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);

    DLOG(2, "  [d=%d] trying TT move %c%c first\n", depth, 'A' + (tt_move % 8),
         '1' + (tt_move / 8));

    int score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);

    DLOG(2, "  [d=%d] TT move %c%c = %d\n", depth, 'A' + (tt_move % 8),
         '1' + (tt_move / 8), score);

    if (score > best_score) {
      best_score = score;
      local_best = tt_move;
    }
    if (score > alpha)
      alpha = score;
    if (alpha >= beta) {
      g_v2_cutoffs++;
      DLOG(2, "  [d=%d] BETA CUTOFF at TT move %c%c\n", depth,
           'A' + (tt_move % 8), '1' + (tt_move / 8));
      goto end_search;
    }
  }

  /* ── 5. 剩余走法按静态权重排序 ── */
  {
    uint64 temp = moves;
    if (tt_move != -1)
      temp &= ~(1ULL << tt_move);

    RatedMove rated_moves[64];
    int size = 0;
    while (temp) {
      int idx = __builtin_ctzll(temp);
      int sc = square_weights[idx];

      int j = size;
      while (j > 0 && rated_moves[j - 1].score < sc) {
        rated_moves[j] = rated_moves[j - 1];
        j--;
      }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc};
      size++;
      temp &= temp - 1;
    }

    /* ── 6. 依次搜索 ── */
    for (int i = 0; i < size; i++) {
      /* 超时检查 */
      if (timed_out()) { g_timeout_occurred = 1; break; }

      int move = rated_moves[i].move;
      uint64 nP, nO;
      make_move(P, O, move, &nP, &nO);

      DLOG(2, "  [d=%d] trying %c%c (w=%d) alpha=%d beta=%d\n", depth,
           'A' + (move % 8), '1' + (move / 8), rated_moves[i].score, alpha,
           beta);

      int score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);

      DLOG(2, "  [d=%d] move %c%c = %d\n", depth, 'A' + (move % 8),
           '1' + (move / 8), score);

      if (score > best_score) {
        best_score = score;
        local_best = move;
      }
      if (score > alpha)
        alpha = score;
      if (alpha >= beta) {
        g_v2_cutoffs++;
        DLOG(2, "  [d=%d] BETA CUTOFF at %c%c\n", depth, 'A' + (move % 8),
             '1' + (move / 8));
        break;
      }
    }
  }

end_search:
  /* ── 7. 存入置换表 ── */
  tt_store(key, depth, best_score, original_alpha, beta, local_best);
  g_v2_tt_stores++;

  if (best_move)
    *best_move = local_best;
  return best_score;
}

/* ================================================================== */
/*  导出接口                                                          */
/* ================================================================== */

void c_init_search(void) {
  /* 初始化 Zobrist 随机数 */
  for (int i = 0; i < 64; i++) {
    zobrist_P[i] = next_random64();
    zobrist_O[i] = next_random64();
  }

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

  /* 清空置换表 */
  memset(transposition_table, 0, sizeof(transposition_table));

  DLOG(1, "=== Alpha-Beta + TT Engine Initialized ===\n");
}

int c_get_best_move(int depth, uint64 player_bb, uint64 opponent_bb,
                     int *move) {
  g_timeout_occurred = 0;
  g_v2_nodes = 0;
  g_v2_cutoffs = 0;
  g_v2_tt_hits = 0;
  g_v2_tt_stores = 0;
  g_v2_start_time = clock();

  int num_legal = popcount(get_legal_moves(player_bb, opponent_bb));

  DLOG(1,
       "\n========== Alpha-Beta + TT Search ==========\n"
       "Depth: %d  |  Player: %llu  |  Opponent: %llu\n"
       "Legal moves: %d\n",
       depth, (unsigned long long)player_bb,
       (unsigned long long)opponent_bb, num_legal);

  int best_move_local = -1;
  int score =
      negamax(depth, player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE,
              &best_move_local);

  clock_t elapsed = clock() - g_v2_start_time;
  double time_ms = (double)elapsed / CLOCKS_PER_SEC * 1000.0;
  double nps = (time_ms > 0) ? g_v2_nodes / (time_ms / 1000.0) : 0;

  DLOG(1,
       "========== Search Complete ==========\n"
       "Best move: %d (%c%c)  |  Score: %d\n"
       "Nodes: %lld  |  Cutoffs: %lld (%.1f%%)\n"
       "TT hits: %lld  |  TT stores: %lld  |  Hit rate: %.1f%%\n"
       "Time: %.2f ms  |  NPS: %.0f nodes/sec\n"
       "======================================\n",
       best_move_local,
       best_move_local >= 0 ? 'A' + (best_move_local % 8) : '?',
       best_move_local >= 0 ? '1' + (best_move_local / 8) : '?', score,
       g_v2_nodes, g_v2_cutoffs,
       g_v2_nodes > 0 ? (double)g_v2_cutoffs / g_v2_nodes * 100.0 : 0.0,
       g_v2_tt_hits, g_v2_tt_stores,
       g_v2_nodes > 0 ? (double)g_v2_tt_hits / g_v2_nodes * 100.0 : 0.0,
       time_ms, nps);

  if (move)
    *move = best_move_local;
  return score;
}

int c_get_best_move_timed(int time_limit_ms, uint64 player_bb,
                           uint64 opponent_bb, int *move,
                           int *depth_searched) {
  /* 简化版：固定深度搜索 */
  int depth;
  if (time_limit_ms <= 0)
    depth = 12;
  else if (time_limit_ms < 100)
    depth = 6;
  else if (time_limit_ms < 500)
    depth = 8;
  else
    depth = 12;

  int result = c_get_best_move(depth, player_bb, opponent_bb, move);
  if (depth_searched)
    *depth_searched = depth;
  return result;
}
