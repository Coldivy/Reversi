/**
 * v3_full_debug.c — 完全体算法 + Debug 输出
 *
 * 基于 search.c，包含全部增强：
 *   TT + 杀手着法 + 历史启发表 + LMR + Futility + IID + 迭代加深 + 渴望窗口
 *
 * 额外添加了 debug 输出，可通过 g_v3_debug_level 控制（0=off, 1=summary, 2=verbose）。
 *
 * 导出接口：c_init_search / c_get_best_move / c_get_best_move_timed
 */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ================================================================== */
/*  Debug 控制                                                         */
/* ================================================================== */

int g_v3_debug_level = 1; /* 0=off, 1=summary, 2=verbose */
long long g_v3_nodes = 0;
long long g_v3_cutoffs = 0;
long long g_v3_tt_hits = 0;
long long g_v3_tt_stores = 0;
long long g_v3_futility_skips = 0;
long long g_v3_lmr_reductions = 0;
long long g_v3_iid_calls = 0;
clock_t g_v3_start_time;

#define DLOG(level, fmt, ...)                                                  \
  do {                                                                         \
    if (g_v3_debug_level >= (level))                                           \
      fprintf(stderr, "[v3_full] " fmt, ##__VA_ARGS__);                        \
  } while (0)

/* ================================================================== */
/*  常量                                                              */
/* ================================================================== */

#define LOSS_SCORE   -100000
#define WIN_SCORE     100000
#define HISTORY_MAX   800000
#define TIMED_OUT_SCORE -200000
int g_timeout_occurred = 0;
static clock_t timed_deadline = 0;
static inline int timed_out(void) {
  return timed_deadline > 0 && clock() >= timed_deadline;
}

void c_set_time_limit(int ms) {
  if (ms > 0)
    timed_deadline = clock() + (clock_t)((double)ms / 1000.0 * CLOCKS_PER_SEC);
  else
    timed_deadline = 0;
}

typedef uint64_t uint64;

/* ================================================================== */
/*  位置权重表                                                        */
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
/*  杀手着法 + 历史启发表                                             */
/* ================================================================== */

static int killer_moves[64][2];
static int history_table[64];

/* ================================================================== */
/*  位运算工具                                                        */
/* ================================================================== */

static inline int popcount(uint64 x) { return __builtin_popcountll(x); }

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
#define TT_BETA  2

typedef struct {
  uint64 key;
  int     score;
  int     best_move;
  int     depth;
  uint8_t flag;
} TTEntry;

#define TT_SIZE (1 << 20)
#define TT_MASK (TT_SIZE - 1)
TTEntry transposition_table[TT_SIZE];

/* ================================================================== */
/*  Zobrist 哈希                                                      */
/* ================================================================== */

static uint64_t xorshift64_state = 88172645463325252ULL;

uint64_t next_random64() {
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
        g_v3_tt_hits++;
        return true;
      }
      if (entry->flag == TT_ALPHA && entry->score <= alpha) {
        *score = entry->score;
        g_v3_tt_hits++;
        return true;
      }
      if (entry->flag == TT_BETA && entry->score >= beta) {
        *score = entry->score;
        g_v3_tt_hits++;
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

  if (entry->key == 0 || depth >= entry->depth || entry->key != key) {
    uint8_t flag = TT_EXACT;
    if (score <= alpha) {
      flag = TT_ALPHA;
    } else if (score >= beta) {
      flag = TT_BETA;
    }
    *entry = (TTEntry){.key = key, .score = score, .best_move = best_move,
                       .flag = flag, .depth = depth};
    g_v3_tt_stores++;
  }
}

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

  DIRECTION(<< 1, mask) DIRECTION(>> 1, mask)
  DIRECTION(<< 8, O)    DIRECTION(>> 8, O)
  DIRECTION(<< 7, mask) DIRECTION(>> 9, mask)
  DIRECTION(<< 9, mask) DIRECTION(>> 7, mask)

  return moves & ~(P | O);
}

void make_move(uint64 P, uint64 O, int move, uint64 *newP, uint64 *newO) {
  uint64 m = 1ULL << move;
  uint64 flipped = 0;
  uint64 mask = O & 0x7E7E7E7E7E7E7E7EULL;

#define FLIP_DIR(SHIFT, MASK)                                                  \
  { uint64 t = (m SHIFT) & (MASK);                                             \
    for (int _i = 0; _i < 5; ++_i) t |= (t SHIFT) & (MASK);                  \
    if ((t SHIFT) & P) flipped |= t; }

  FLIP_DIR(<< 1, mask) FLIP_DIR(>> 1, mask)
  FLIP_DIR(<< 8, O)    FLIP_DIR(>> 8, O)
  FLIP_DIR(<< 7, mask) FLIP_DIR(>> 9, mask)
  FLIP_DIR(<< 9, mask) FLIP_DIR(>> 7, mask)

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
/*  走法排序评分                                                      */
/* ================================================================== */

static inline int move_ordering_score(int move, int depth) {
  int score = square_weights[move];
  score += history_table[move] / 16;
  if (move == killer_moves[depth][0]) {
    score += 20000;
  } else if (move == killer_moves[depth][1]) {
    score += 10000;
  }
  return score;
}

/* ================================================================== */
/*  终局完美搜索                                                      */
/* ================================================================== */

int evaluate_exact_score(uint64 P, uint64 O) {
  int p_cnt = popcount(P), o_cnt = popcount(O);
  if (p_cnt == 0) return -64;
  if (o_cnt == 0) return  64;
  return (int)(p_cnt - o_cnt);
}

int solve(uint64 P, uint64 O, int alpha, int beta, int *best_move) {
  /* 超时检查 */
  if (timed_out()) { g_timeout_occurred = 1; return evaluate_exact_score(P, O); }

  int occupied = popcount(P | O);
  int depth = 64 - occupied;

  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1, tt_score = -1;

  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) {
    if (best_move) *best_move = tt_move;
    return tt_score;
  }

  uint64 moves = get_legal_moves(P, O);
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) return evaluate_exact_score(P, O);
    return -solve(O, P, -beta, -alpha, NULL);
  }

  int best_score = LOSS_SCORE;
  int local_best_move = -1;

  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);
    int score = -solve(nO, nP, -beta, -alpha, NULL);
    if (score > best_score) { best_score = score; local_best_move = tt_move; }
    if (score > alpha) alpha = score;
    if (alpha >= beta) goto end_search;
  }

  {
    uint64 temp_moves = moves;
    if (tt_move != -1) temp_moves &= ~(1ULL << tt_move);

    RatedMove rated_moves[64]; int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves);
      int sc = square_weights[idx] + history_table[idx] / 16;
      if (idx == killer_moves[depth][0]) sc += 20000;
      else if (idx == killer_moves[depth][1]) sc += 10000;
      int j = size;
      while (j > 0 && rated_moves[j - 1].score < sc) { rated_moves[j] = rated_moves[j - 1]; j--; }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc}; size++;
      temp_moves &= temp_moves - 1;
    }

    for (int idx = 0; idx < size; idx++) {
      /* 超时检查 */
      if (timed_out()) { g_timeout_occurred = 1; goto end_search; }

      int i = rated_moves[idx].move;
      uint64 nP, nO; make_move(P, O, i, &nP, &nO);
      int score = -solve(nO, nP, -beta, -alpha, NULL);
      if (score > best_score) { best_score = score; local_best_move = i; }
      if (score > alpha) alpha = score;
      if (alpha >= beta) break;
    }
  }

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move) *best_move = local_best_move;
  return best_score;
}

/* ================================================================== */
/*  Negamax（完全体，无空步，带 debug）                                */
/* ================================================================== */

int negamax(int depth, uint64 P, uint64 O, int alpha, int beta,
            int *best_move) {
  g_v3_nodes++;

  /* 超时检查 */
  if (timed_out()) { g_timeout_occurred = 1; return evaluate(P, O); }

  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1, tt_score = -1;

  /* ---- 1. 置换表查找 ---- */
  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) {
    DLOG(2, "  [d=%d] TT hit -> score=%d move=%d\n", depth, tt_score, tt_move);
    if (best_move) *best_move = tt_move;
    return tt_score;
  }

  /* ---- 2. 叶子节点 ---- */
  if (depth == 0) return evaluate(P, O);

  uint64 moves = get_legal_moves(P, O);

  /* ---- 3. 无合法走法 → 轮空 ---- */
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) {
      int p_cnt = popcount(P), o_cnt = popcount(O);
      if (p_cnt > o_cnt) return WIN_SCORE + (p_cnt - o_cnt);
      if (p_cnt < o_cnt) return LOSS_SCORE + (p_cnt - o_cnt);
      return 0;
    }
    return -negamax(depth - 1, O, P, -beta, -alpha, NULL);
  }

  /* ---- 4. IID ---- */
  if (tt_move == -1 && depth >= 5) {
    DLOG(2, "  [d=%d] IID: shallow search at depth %d\n", depth, depth / 2);
    g_v3_iid_calls++;
    negamax(depth / 2, P, O, alpha, beta, &tt_move);
    DLOG(2, "  [d=%d] IID result: tt_move=%d\n", depth, tt_move);
  }

  int best_score = LOSS_SCORE;
  int local_best_move = -1;
  int moves_searched = 0;
  bool is_first_move = true;

  /* ---- 5. 优先搜索 TT/IID 提示的走法 ---- */
  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO; make_move(P, O, tt_move, &nP, &nO);
    DLOG(2, "  [d=%d] trying TT move %c%c first\n", depth,
         'A' + (tt_move % 8), '1' + (tt_move / 8));
    int score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);
    is_first_move = false; moves_searched++;
    if (score > best_score) { best_score = score; local_best_move = tt_move; }
    if (score > alpha) alpha = score;
    if (alpha >= beta) {
      g_v3_cutoffs++;
      DLOG(2, "  [d=%d] CUTOFF on TT move %c%c\n", depth,
           'A' + (tt_move % 8), '1' + (tt_move / 8));
      history_table[tt_move] += depth * depth;
      goto end_search;
    }
  }

  /* ---- 6. 剩余走法排序 ---- */
  {
    uint64 temp_moves = moves;
    if (tt_move != -1) temp_moves &= ~(1ULL << tt_move);

    RatedMove rated_moves[64]; int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves);
      int sc = move_ordering_score(idx, depth);
      int j = size;
      while (j > 0 && rated_moves[j - 1].score < sc) { rated_moves[j] = rated_moves[j - 1]; j--; }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc}; size++;
      temp_moves &= temp_moves - 1;
    }

    /* ---- 7. 依次搜索（PVS + LMR + Futility + 超时检查） ---- */
    for (int idx = 0; idx < size; idx++) {
      /* 超时检查 */
      if (timed_out()) { g_timeout_occurred = 1; goto end_search; }

      int i = rated_moves[idx].move;
      uint64 nP, nO; make_move(P, O, i, &nP, &nO);
      int score;

      /* ---- 7a. Futility Pruning ---- */
      if (depth == 1 && moves_searched >= 3) {
        int stand_pat = -evaluate(nO, nP);
        if (stand_pat + 300 < alpha) {
          g_v3_futility_skips++;
          DLOG(2, "  [d=%d] FUTILITY skip %c%c (stand_pat=%d alpha=%d)\n",
               depth, 'A' + (i % 8), '1' + (i / 8), stand_pat, alpha);
          if (stand_pat > best_score) best_score = stand_pat;
          continue;
        }
      }

      /* ---- 7b. LMR ---- */
      int lmr_reduction = 0;
      if (moves_searched >= 4 && depth >= 3 && !is_first_move) {
        lmr_reduction = 1 + (moves_searched - 4) / 8;
        if (lmr_reduction > 3) lmr_reduction = 3;
        if (depth - 1 - lmr_reduction < 1) lmr_reduction = depth - 2;
        if (lmr_reduction > 0) g_v3_lmr_reductions++;
      }

      /* ---- 7c. PVS + LMR ---- */
      if (is_first_move) {
        DLOG(2, "  [d=%d] PV move %c%c full window\n", depth,
             'A' + (i % 8), '1' + (i / 8));
        score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);
        is_first_move = false;
      } else if (lmr_reduction > 0) {
        DLOG(2, "  [d=%d] LMR move %c%c reduced by %d\n", depth,
             'A' + (i % 8), '1' + (i / 8), lmr_reduction);
        score = -negamax(depth - 1 - lmr_reduction, nO, nP,
                         -alpha - 1, -alpha, NULL);
        if (score > alpha) {
          DLOG(2, "  [d=%d] LMR re-search %c%c at full depth\n", depth,
               'A' + (i % 8), '1' + (i / 8));
          score = -negamax(depth - 1, nO, nP, -alpha - 1, -alpha, NULL);
        }
      } else {
        score = -negamax(depth - 1, nO, nP, -alpha - 1, -alpha, NULL);
      }

      /* ---- 7d. 零窗口通过 → 全窗口确认 ---- */
      if (!is_first_move && score > alpha && score < beta) {
        DLOG(2, "  [d=%d] re-search %c%c full window\n", depth,
             'A' + (i % 8), '1' + (i / 8));
        score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);
      }

      moves_searched++;

      if (score > best_score) { best_score = score; local_best_move = i; }
      if (score > alpha) alpha = score;
      if (alpha >= beta) {
        g_v3_cutoffs++;
        DLOG(2, "  [d=%d] CUTOFF at %c%c (moves_searched=%d)\n", depth,
             'A' + (i % 8), '1' + (i / 8), moves_searched);
        if (i != tt_move) {
          if (killer_moves[depth][0] != i) {
            killer_moves[depth][1] = killer_moves[depth][0];
            killer_moves[depth][0] = i;
          }
        }
        history_table[i] += depth * depth;
        if (history_table[i] > HISTORY_MAX) {
          DLOG(2, "  [history] overflow guard: half all\n");
          for (int h = 0; h < 64; h++) history_table[h] /= 2;
        }
        break;
      }
    }
  }

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move) *best_move = local_best_move;
  return best_score;
}

/* ================================================================== */
/*  限时版 negamax（negamax 镜像 + 超时中断）                         */
/* ================================================================== */

int negamax_timed(int depth, uint64 P, uint64 O, int alpha, int beta,
                  int *best_move) {
  if (timed_out()) return TIMED_OUT_SCORE;
  g_v3_nodes++;

  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1, tt_score = -1;

  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) { if (best_move) *best_move = tt_move; return tt_score; }
  if (depth == 0) return evaluate(P, O);

  uint64 moves = get_legal_moves(P, O);
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) {
      int p_cnt = popcount(P), o_cnt = popcount(O);
      if (p_cnt > o_cnt) return WIN_SCORE + (p_cnt - o_cnt);
      if (p_cnt < o_cnt) return LOSS_SCORE + (p_cnt - o_cnt);
      return 0;
    }
    return -negamax_timed(depth - 1, O, P, -beta, -alpha, NULL);
  }

  if (tt_move == -1 && depth >= 5) {
    g_v3_iid_calls++;
    negamax_timed(depth / 2, P, O, alpha, beta, &tt_move);
  }

  int best_score = LOSS_SCORE;
  int local_best_move = -1;
  int moves_searched = 0;
  bool aborted = false;
  bool is_first_move = true;

  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO; make_move(P, O, tt_move, &nP, &nO);
    int score = -negamax_timed(depth - 1, nO, nP, -beta, -alpha, NULL);
    if (score == TIMED_OUT_SCORE) { aborted = true; goto end_search; }
    is_first_move = false; moves_searched++;
    if (score > best_score) { best_score = score; local_best_move = tt_move; }
    if (score > alpha) alpha = score;
    if (alpha >= beta) {
      g_v3_cutoffs++;
      history_table[tt_move] += depth * depth;
      goto end_search;
    }
  }

  {
    uint64 temp_moves = moves;
    if (tt_move != -1) temp_moves &= ~(1ULL << tt_move);

    RatedMove rated_moves[64]; int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves);
      int sc = move_ordering_score(idx, depth);
      int j = size;
      while (j > 0 && rated_moves[j - 1].score < sc) { rated_moves[j] = rated_moves[j - 1]; j--; }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc}; size++;
      temp_moves &= temp_moves - 1;
    }

    for (int idx = 0; idx < size; idx++) {
      if (timed_out()) goto end_search;

      int i = rated_moves[idx].move;
      uint64 nP, nO; make_move(P, O, i, &nP, &nO);
      int score;

      if (depth == 1 && moves_searched >= 3) {
        int stand_pat = -evaluate(nO, nP);
        if (stand_pat + 300 < alpha) {
          g_v3_futility_skips++;
          if (stand_pat > best_score) best_score = stand_pat;
          continue;
        }
      }

      int lmr_reduction = 0;
      if (moves_searched >= 4 && depth >= 3 && !is_first_move) {
        lmr_reduction = 1 + (moves_searched - 4) / 8;
        if (lmr_reduction > 3) lmr_reduction = 3;
        if (depth - 1 - lmr_reduction < 1) lmr_reduction = depth - 2;
        if (lmr_reduction > 0) g_v3_lmr_reductions++;
      }

      if (is_first_move) {
        score = -negamax_timed(depth - 1, nO, nP, -beta, -alpha, NULL);
        is_first_move = false;
      } else if (lmr_reduction > 0) {
        score = -negamax_timed(depth - 1 - lmr_reduction, nO, nP,
                               -alpha - 1, -alpha, NULL);
        if (score != TIMED_OUT_SCORE && score > alpha)
          score = -negamax_timed(depth - 1, nO, nP, -alpha - 1, -alpha, NULL);
      } else {
        score = -negamax_timed(depth - 1, nO, nP, -alpha - 1, -alpha, NULL);
      }

      if (score == TIMED_OUT_SCORE) { aborted = true; goto end_search; }

      if (!is_first_move && score > alpha && score < beta) {
        score = -negamax_timed(depth - 1, nO, nP, -beta, -alpha, NULL);
        if (score == TIMED_OUT_SCORE) { aborted = true; goto end_search; }
      }

      moves_searched++;
      if (score > best_score) { best_score = score; local_best_move = i; }
      if (score > alpha) alpha = score;
      if (alpha >= beta) {
        g_v3_cutoffs++;
        if (i != tt_move) {
          if (killer_moves[depth][0] != i) { killer_moves[depth][1] = killer_moves[depth][0]; killer_moves[depth][0] = i; }
        }
        history_table[i] += depth * depth;
        if (history_table[i] > HISTORY_MAX) { for (int h = 0; h < 64; h++) history_table[h] /= 2; }
        break;
      }
    }
  }

end_search:
  if (aborted && local_best_move == -1) return TIMED_OUT_SCORE;
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move) *best_move = local_best_move;
  return best_score;
}

/* ================================================================== */
/*  限时版 solve                                                      */
/* ================================================================== */

int solve_timed(uint64 P, uint64 O, int alpha, int beta, int *best_move) {
  if (timed_out()) return TIMED_OUT_SCORE;

  int occupied = popcount(P | O);
  int depth = 64 - occupied;
  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1, tt_score = -1;

  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) { if (best_move) *best_move = tt_move; return tt_score; }

  uint64 moves = get_legal_moves(P, O);
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) return evaluate_exact_score(P, O);
    return -solve_timed(O, P, -beta, -alpha, NULL);
  }

  int best_score = LOSS_SCORE, local_best_move = -1;
  bool aborted = false;

  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO; make_move(P, O, tt_move, &nP, &nO);
    int score = -solve_timed(nO, nP, -beta, -alpha, NULL);
    if (score == TIMED_OUT_SCORE) { aborted = true; goto end_search; }
    if (score > best_score) { best_score = score; local_best_move = tt_move; }
    if (score > alpha) alpha = score;
    if (alpha >= beta) goto end_search;
  }

  {
    uint64 temp_moves = moves;
    if (tt_move != -1) temp_moves &= ~(1ULL << tt_move);

    RatedMove rated_moves[64]; int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves);
      int sc = square_weights[idx] + history_table[idx] / 16;
      if (idx == killer_moves[depth][0]) sc += 20000;
      else if (idx == killer_moves[depth][1]) sc += 10000;
      int j = size;
      while (j > 0 && rated_moves[j - 1].score < sc) { rated_moves[j] = rated_moves[j - 1]; j--; }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc}; size++;
      temp_moves &= temp_moves - 1;
    }

    for (int idx = 0; idx < size; idx++) {
      if (timed_out()) { aborted = true; goto end_search; }
      int i = rated_moves[idx].move;
      uint64 nP, nO; make_move(P, O, i, &nP, &nO);
      int score = -solve_timed(nO, nP, -beta, -alpha, NULL);
      if (score == TIMED_OUT_SCORE) { aborted = true; goto end_search; }
      if (score > best_score) { best_score = score; local_best_move = i; }
      if (score > alpha) alpha = score;
      if (alpha >= beta) break;
    }
  }

end_search:
  if (aborted && local_best_move == -1) return TIMED_OUT_SCORE;
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move) *best_move = local_best_move;
  return best_score;
}

/* ================================================================== */
/*  导出接口                                                          */
/* ================================================================== */

void c_init_search() {
  for (int i = 0; i < 64; i++) { zobrist_P[i] = next_random64(); zobrist_O[i] = next_random64(); }
  memset(transposition_table, 0, sizeof(transposition_table));
  for (int pos = 0; pos < 64; pos++) {
    uint64_t bit = 1ULL << pos;
    square_weights[pos] = 0;
    for (int i = 0; i < 8; i++) {
      if (MASKS[i] & bit) { square_weights[pos] = WEIGHTS[i]; break; }
    }
  }
  memset(killer_moves, 0, sizeof(killer_moves));
  memset(history_table, 0, sizeof(history_table));
  DLOG(1, "=== Full Engine (v3) Initialized (debug level=%d) ===\n",
       g_v3_debug_level);
}

int c_get_best_move(int depth, uint64 player_bb, uint64 opponent_bb,
                    int *move) {
  if (move) *move = -1;

  g_timeout_occurred = 0;
  g_v3_nodes = 0; g_v3_cutoffs = 0; g_v3_tt_hits = 0; g_v3_tt_stores = 0;
  g_v3_futility_skips = 0; g_v3_lmr_reductions = 0; g_v3_iid_calls = 0;
  g_v3_start_time = clock();

  int occupied_squares = popcount(player_bb | opponent_bb);
  int empty_squares = 64 - occupied_squares;

  DLOG(1,
       "\n========== Full Engine Search ==========\n"
       "Max depth: %d  |  Empty: %d  |  Player: %llu  |  Opponent: %llu\n",
       depth, empty_squares,
       (unsigned long long)player_bb, (unsigned long long)opponent_bb);

  static bool tt_cleared_for_endgame = false;

  if (empty_squares <= 12) {
    if (!tt_cleared_for_endgame) {
      memset(transposition_table, 0, sizeof(transposition_table));
      tt_cleared_for_endgame = true;
      DLOG(1, "[endgame mode] TT cleared for endgame search\n");
    }
    int result = solve(player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, move);

    clock_t elapsed = clock() - g_v3_start_time;
    double time_ms = (double)elapsed / CLOCKS_PER_SEC * 1000.0;
    DLOG(1,
         "========== Search Complete ==========\n"
         "Best move: %d (%c%c)  |  Score: %d\n"
         "Nodes: %lld  |  Time: %.2f ms\n"
         "TT hits: %lld / TT stores: %lld (%.1f%%)\n",
         move ? *move : -1,
         move && *move >= 0 ? 'A' + (*move % 8) : '?',
         move && *move >= 0 ? '1' + (*move / 8) : '?',
         result, g_v3_nodes, time_ms,
         g_v3_tt_hits, g_v3_tt_stores,
         g_v3_nodes > 0 ? (double)g_v3_tt_hits / g_v3_nodes * 100.0 : 0.0);
    return result;
  } else {
    tt_cleared_for_endgame = false;
  }

  int score = LOSS_SCORE;
  int temp_move = -1;

  /* 迭代加深 + 渴望窗口 */
  for (int d = 1; d <= depth; d++) {
    int current_best_move = -1;

    DLOG(1, "[ID] depth=%d ...\n", d);

    if (d >= 3) {
      int margin = 50;
      int alpha = score - margin;
      int beta  = score + margin;

      while (1) {
        if (alpha < LOSS_SCORE) alpha = LOSS_SCORE;
        if (beta  > WIN_SCORE)  beta  = WIN_SCORE;

        current_best_move = -1;
        int val = negamax(d, player_bb, opponent_bb, alpha, beta, &current_best_move);

        if (val <= alpha) {
          if (alpha == LOSS_SCORE) { score = val; break; }
          DLOG(1, "  [ID d=%d] fail-low, widen window (alpha=%d->%d)\n",
               d, alpha, val - margin * 2);
          margin *= 2; alpha = val - margin; beta = val + margin;
        } else if (val >= beta) {
          if (beta == WIN_SCORE) { score = val; break; }
          DLOG(1, "  [ID d=%d] fail-high, widen window (beta=%d->%d)\n",
               d, beta, val + margin * 2);
          margin *= 2; beta = val + margin; alpha = val - margin;
        } else {
          score = val; break;
        }
      }
    } else {
      score = negamax(d, player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, &current_best_move);
    }

    if (current_best_move != -1) temp_move = current_best_move;
    DLOG(1, "  [ID d=%d] best_move=%c%c score=%d\n", d,
         temp_move >= 0 ? 'A' + (temp_move % 8) : '?',
         temp_move >= 0 ? '1' + (temp_move / 8) : '?', score);
  }

  clock_t elapsed = clock() - g_v3_start_time;
  double time_ms = (double)elapsed / CLOCKS_PER_SEC * 1000.0;
  double nps = (time_ms > 0) ? g_v3_nodes / (time_ms / 1000.0) : 0;

  DLOG(1,
       "========== Search Complete ==========\n"
       "Best move: %d (%c%c)  |  Score: %d\n"
       "Nodes: %lld  |  Cutoffs: %lld\n"
       "TT hits: %lld / stores: %lld (hit rate %.1f%%)\n"
       "Futility skips: %lld  |  LMR reductions: %lld  |  IID calls: %lld\n"
       "Time: %.2f ms  |  NPS: %.0f nodes/sec\n"
       "======================================\n",
       temp_move,
       temp_move >= 0 ? 'A' + (temp_move % 8) : '?',
       temp_move >= 0 ? '1' + (temp_move / 8) : '?',
       score, g_v3_nodes, g_v3_cutoffs,
       g_v3_tt_hits, g_v3_tt_stores,
       g_v3_nodes > 0 ? (double)g_v3_tt_hits / g_v3_nodes * 100.0 : 0.0,
       g_v3_futility_skips, g_v3_lmr_reductions, g_v3_iid_calls,
       time_ms, nps);

  if (move) *move = temp_move;
  return score;
}

int c_get_best_move_timed(int time_limit_ms,
                          uint64 player_bb, uint64 opponent_bb,
                          int *move, int *depth_searched) {
  if (move) *move = -1;
  if (depth_searched) *depth_searched = 0;

  g_timeout_occurred = 0;
  g_v3_nodes = 0; g_v3_cutoffs = 0; g_v3_tt_hits = 0; g_v3_tt_stores = 0;
  g_v3_futility_skips = 0; g_v3_lmr_reductions = 0; g_v3_iid_calls = 0;
  g_v3_start_time = clock();

  int occupied_squares = popcount(player_bb | opponent_bb);
  int empty_squares = 64 - occupied_squares;

  DLOG(1,
       "\n========== Full Engine Timed Search ==========\n"
       "Time limit: %d ms  |  Empty: %d\n",
       time_limit_ms, empty_squares);

  static bool tt_cleared_for_endgame = false;

  if (empty_squares <= 12) {
    if (!tt_cleared_for_endgame) {
      memset(transposition_table, 0, sizeof(transposition_table));
      tt_cleared_for_endgame = true;
    }
    timed_deadline = 0;
    int result = solve_timed(player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, move);
    if (depth_searched) *depth_searched = empty_squares;
    return result;
  } else {
    tt_cleared_for_endgame = false;
  }

  clock_t start = clock();
  if (time_limit_ms > 0)
    timed_deadline = start + (clock_t)((double)time_limit_ms / 1000.0 * CLOCKS_PER_SEC);
  else
    timed_deadline = 0;

  int score = LOSS_SCORE, temp_move = -1;
  int max_depth = empty_squares;
  if (max_depth > 20) max_depth = 20;

  for (int d = 1; d <= max_depth; d++) {
    int current_best_move = -1;
    DLOG(1, "[ID timed] depth=%d ...\n", d);

    if (d >= 3) {
      int margin = 50;
      int alpha = score - margin, beta = score + margin;

      while (1) {
        if (alpha < LOSS_SCORE) alpha = LOSS_SCORE;
        if (beta  > WIN_SCORE)  beta  = WIN_SCORE;

        current_best_move = -1;
        int val = negamax_timed(d, player_bb, opponent_bb, alpha, beta, &current_best_move);

        if (val == TIMED_OUT_SCORE) goto done;

        if (val <= alpha) {
          if (alpha == LOSS_SCORE) { score = val; break; }
          margin *= 2; alpha = val - margin; beta = val + margin;
        } else if (val >= beta) {
          if (beta == WIN_SCORE) { score = val; break; }
          margin *= 2; beta = val + margin; alpha = val - margin;
        } else { score = val; break; }
      }
    } else {
      int val = negamax_timed(d, player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, &current_best_move);
      if (val == TIMED_OUT_SCORE) goto done;
      score = val;
    }

    if (current_best_move != -1) temp_move = current_best_move;
    if (depth_searched) *depth_searched = d;
    DLOG(1, "  [ID d=%d] best_move=%c%c score=%d\n", d,
         temp_move >= 0 ? 'A' + (temp_move % 8) : '?',
         temp_move >= 0 ? '1' + (temp_move / 8) : '?', score);
  }

done:
  timed_deadline = 0;

  clock_t elapsed = clock() - g_v3_start_time;
  double time_ms = (double)elapsed / CLOCKS_PER_SEC * 1000.0;

  DLOG(1,
       "========== Timed Search Complete ==========\n"
       "Best move: %d (%c%c)  |  Score: %d  |  Depth reached: %d\n"
       "Nodes: %lld  |  TT hits: %lld\n"
       "Time: %.2f ms\n"
       "============================================\n",
       temp_move,
       temp_move >= 0 ? 'A' + (temp_move % 8) : '?',
       temp_move >= 0 ? '1' + (temp_move / 8) : '?',
       score, depth_searched ? *depth_searched : 0,
       g_v3_nodes, g_v3_tt_hits, time_ms);

  if (move) *move = temp_move;
  return score;
}
