/**
 * v0_baseline_debug.c — 原版 search.c 算法 + 调试计数器
 *
 * 算法层面与 search.c 完全一致：
 *   - 置换表 (TT)
 *   - PVS (主变例搜索)
 *   - 杀手着法 (2个/层)
 *   - 渴望窗口 + 迭代加深
 *   - 终局完美搜索 solve()
 *
 * 新增：调试计数器（仅用于 benchmark 对照，不参与算法）
 *
 * 下一版将在此基础上加入：v1_history_debug.c — 历史启发表
 */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOSS_SCORE -100000
#define WIN_SCORE  100000

typedef uint64_t uint64;

/* ================================================================== */
/*  调试计数器                                                        */
/* ================================================================== */

typedef struct {
  long long nodes;
  long long tt_hits;
  long long beta_cuts;
  long long null_cuts;         /* v0 无此特性，恒为 0 */
  long long futility_skips;    /* v0 无此特性，恒为 0 */
  long long lmr_attempts;      /* v0 无此特性，恒为 0 */
  long long lmr_researches;    /* v0 无此特性，恒为 0 */
  long long iid_searches;      /* v0 无此特性，恒为 0 */
  long long eval_calls;
  long long tt_stores;
} DebugStats;

static DebugStats stats;

#define STATS_INC(field) (stats.field++)

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

static int killer_moves[64][2]; /* 每层 2 个杀手，与原版一致 */

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
/*  置换表                                                            */
/* ================================================================== */

#define TT_EXACT 0
#define TT_ALPHA 1
#define TT_BETA  2

typedef struct {
  uint64 key;
  int score;
  int best_move;
  int depth;
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
bool is_initialized = false;

uint64 get_zobrist_key(uint64 P, uint64 O) {
  uint64 key = 0;
  uint64 t = P;
  while (t) { int i = __builtin_ctzll(t); key ^= zobrist_P[i]; t &= t - 1; }
  t = O;
  while (t) { int i = __builtin_ctzll(t); key ^= zobrist_O[i]; t &= t - 1; }
  return key;
}

/* ================================================================== */
/*  置换表存取                                        (原版)          */
/* ================================================================== */

bool tt_lookup(uint64 key, int depth, int alpha, int beta,
               int *score, int *best_move) {
  uint64 index = key & TT_MASK;
  TTEntry *entry = &transposition_table[index];
  if (entry->key == key) {
    *best_move = entry->best_move;
    if (entry->depth >= depth) {
      if (entry->flag == TT_EXACT) { *score = entry->score; return true; }
      if (entry->flag == TT_ALPHA && entry->score <= alpha) {
        *score = entry->score; return true;
      }
      if (entry->flag == TT_BETA && entry->score >= beta) {
        *score = entry->score; return true;
      }
    }
  }
  return false;
}

void tt_store(uint64 key, int depth, int score, int alpha, int beta,
              int best_move) {
  STATS_INC(tt_stores);
  uint64 index = key & TT_MASK;
  TTEntry *entry = &transposition_table[index];
  if (entry->key == 0 || depth >= entry->depth || entry->key != key) {
    uint8_t flag = TT_EXACT;
    if (score <= alpha)      flag = TT_ALPHA;
    else if (score >= beta)  flag = TT_BETA;
    *entry = (TTEntry){.key = key, .score = score,
                       .best_move = best_move, .flag = flag, .depth = depth};
  }
}

/* ================================================================== */
/*  走法生成 & 执行                                    (原版)          */
/* ================================================================== */

uint64 get_legal_moves(uint64 P, uint64 O) {
  uint64 mask = O & 0x7E7E7E7E7E7E7E7EULL;
  uint64 moves = 0, t;

#define DIRECTION(SHIFT, MASK)                                                 \
  t = (P SHIFT) & (MASK);                                                      \
  for (int _i = 0; _i < 5; ++_i) t |= (t SHIFT) & (MASK);                     \
  moves |= (t SHIFT);

  DIRECTION(<< 1, mask)  DIRECTION(>> 1, mask)
  DIRECTION(<< 8, O)     DIRECTION(>> 8, O)
  DIRECTION(<< 7, mask)  DIRECTION(>> 9, mask)
  DIRECTION(<< 9, mask)  DIRECTION(>> 7, mask)

  return moves & ~(P | O);
}

void make_move(uint64 P, uint64 O, int move, uint64 *newP, uint64 *newO) {
  uint64 m = 1ULL << move;
  uint64 flipped = 0;
  uint64 mask = O & 0x7E7E7E7E7E7E7E7EULL;

#define FLIP_DIR(SHIFT, MASK)                                                  \
  { uint64 t = (m SHIFT) & (MASK);                                             \
    for (int _i = 0; _i < 5; ++_i) t |= (t SHIFT) & (MASK);                   \
    if ((t SHIFT) & P) flipped |= t; }

  FLIP_DIR(<< 1, mask)  FLIP_DIR(>> 1, mask)
  FLIP_DIR(<< 8, O)     FLIP_DIR(>> 8, O)
  FLIP_DIR(<< 7, mask)  FLIP_DIR(>> 9, mask)
  FLIP_DIR(<< 9, mask)  FLIP_DIR(>> 7, mask)

  *newP = P | m | flipped;
  *newO = O & ~flipped;
}

/* ================================================================== */
/*  评估函数                                           (原版)         */
/* ================================================================== */

int evaluate(uint64 P, uint64 O) {
  STATS_INC(eval_calls);
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
/*  Negamax 搜索                                        (原版)         */
/*                                                                     */
/*  走法排序：TT走法 → 杀手着法(2个/层) → 静态位置权重                */
/*  搜索策略：PVS（主变例搜索）                                        */
/* ================================================================== */

int negamax(int depth, uint64 P, uint64 O,
            int alpha, int beta, int *best_move) {
  STATS_INC(nodes);
  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1, tt_score = -1;

  /* 置换表查找 */
  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) {
    STATS_INC(tt_hits);
    if (best_move) *best_move = tt_move;
    return tt_score;
  }

  /* 叶子节点 */
  if (depth == 0)
    return evaluate(P, O);

  uint64 moves = get_legal_moves(P, O);

  /* 无合法走法 → 轮空 */
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) {
      int p_cnt = popcount(P), o_cnt = popcount(O);
      if (p_cnt > o_cnt) return WIN_SCORE  + (p_cnt - o_cnt);
      if (p_cnt < o_cnt) return LOSS_SCORE + (p_cnt - o_cnt);
      return 0;
    }
    return -negamax(depth - 1, O, P, -beta, -alpha, NULL);
  }

  int best_score = LOSS_SCORE;
  int local_best_move = -1;
  bool is_first_move = true;

  /* 优先搜索 TT 走法 */
  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);
    int score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);
    is_first_move = false;

    if (score > best_score) { best_score = score; local_best_move = tt_move; }
    if (score > alpha) alpha = score;
    if (alpha >= beta) goto end_search;
  }

  /* 提取剩余走法并按 杀手+静态权重 排序 */
  {
    uint64 temp_moves = moves;
    if (tt_move != -1) temp_moves &= ~(1ULL << tt_move);

    RatedMove rated_moves[64];
    int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves);
      /* ---- 原版排序：静态权重 + 杀手加分 ---- */
      int sc = square_weights[idx];
      if (idx == killer_moves[depth][0])        sc += 10000;
      else if (idx == killer_moves[depth][1])   sc += 5000;

      int j = size;
      while (j > 0 && rated_moves[j - 1].score < sc) {
        rated_moves[j] = rated_moves[j - 1]; j--;
      }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc};
      size++;
      temp_moves &= temp_moves - 1;
    }

    /* 依次搜索（PVS） */
    for (int idx = 0; idx < size; idx++) {
      int i = rated_moves[idx].move;
      uint64 nP, nO;
      make_move(P, O, i, &nP, &nO);
      int score;

      if (is_first_move) {
        score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);
        is_first_move = false;
      } else {
        score = -negamax(depth - 1, nO, nP, -alpha - 1, -alpha, NULL);
        if (score > alpha && score < beta) {
          score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);
        }
      }

      if (score > best_score) { best_score = score; local_best_move = i; }
      if (score > alpha) alpha = score;
      if (alpha >= beta) {
        /* 记录杀手着法 */
        if (i != tt_move) {
          if (killer_moves[depth][0] != i) {
            killer_moves[depth][1] = killer_moves[depth][0];
            killer_moves[depth][0] = i;
          }
        }
        STATS_INC(beta_cuts);
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
/*  终局完美搜索 solve                                  (原版)         */
/*                                                                     */
/*  非 TT 走法不排序，直接按位序遍历                                   */
/* ================================================================== */

int evaluate_exact_score(uint64 P, uint64 O) {
  int p_cnt = popcount(P), o_cnt = popcount(O);
  if (p_cnt == 0) return -64;
  if (o_cnt == 0) return 64;
  return (int)(p_cnt - o_cnt);
}

int solve(uint64 P, uint64 O, int alpha, int beta, int *best_move) {
  STATS_INC(nodes);
  int occupied = popcount(P | O);
  int depth = 64 - occupied;
  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1, tt_score = -1;

  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) {
    STATS_INC(tt_hits);
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

  /* 优先 TT move */
  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);
    int score = -solve(nO, nP, -beta, -alpha, NULL);
    if (score > best_score) { best_score = score; local_best_move = tt_move; }
    if (score > alpha) alpha = score;
    if (alpha >= beta) { STATS_INC(beta_cuts); goto end_search; }
  }

  /* 剩余走法：按位序遍历（不排序） —— 原版行为 */
  {
    uint64 temp_moves = moves;
    if (tt_move != -1) temp_moves &= ~(1ULL << tt_move);

    while (temp_moves) {
      int i = __builtin_ctzll(temp_moves);
      uint64 nP, nO;
      make_move(P, O, i, &nP, &nO);
      int score = -solve(nO, nP, -beta, -alpha, NULL);

      if (score > best_score) { best_score = score; local_best_move = i; }
      if (score > alpha) alpha = score;
      if (alpha >= beta) { STATS_INC(beta_cuts); break; }
      temp_moves &= temp_moves - 1;
    }
  }

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move) *best_move = local_best_move;
  return best_score;
}

/* ================================================================== */
/*  导出接口                                           (原版)         */
/* ================================================================== */

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
      if (MASKS[i] & bit) { square_weights[pos] = WEIGHTS[i]; break; }
    }
  }
  memset(killer_moves, 0, sizeof(killer_moves));
  memset(&stats, 0, sizeof(stats));
}

int c_get_best_move(int depth, uint64 player_bb, uint64 opponent_bb,
                    int *move) {
  if (move) *move = -1;

  int occupied_squares = popcount(player_bb | opponent_bb);
  int empty_squares = 64 - occupied_squares;
  static bool tt_cleared_for_endgame = false;

  if (empty_squares <= 12) {
    if (!tt_cleared_for_endgame) {
      memset(transposition_table, 0, sizeof(transposition_table));
      tt_cleared_for_endgame = true;
    }
    return solve(player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, move);
  } else {
    tt_cleared_for_endgame = false;
  }

  int score = LOSS_SCORE;
  int temp_move = -1;

  for (int d = 1; d <= depth; d++) {
    int current_best_move = -1;

    if (d >= 3) {
      int margin = 50;
      int alpha = score - margin, beta = score + margin;

      while (1) {
        if (alpha < LOSS_SCORE) alpha = LOSS_SCORE;
        if (beta  > WIN_SCORE)  beta  = WIN_SCORE;

        current_best_move = -1;
        int val = negamax(d, player_bb, opponent_bb,
                          alpha, beta, &current_best_move);

        if (val <= alpha) {
          if (alpha == LOSS_SCORE) { score = val; break; }
          margin *= 2; alpha = val - margin; beta = val + margin;
        } else if (val >= beta) {
          if (beta == WIN_SCORE) { score = val; break; }
          margin *= 2; beta = val + margin; alpha = val - margin;
        } else {
          score = val; break;
        }
      }
    } else {
      score = negamax(d, player_bb, opponent_bb,
                      LOSS_SCORE, WIN_SCORE, &current_best_move);
    }

    if (current_best_move != -1) temp_move = current_best_move;
  }

  if (move) *move = temp_move;
  return score;
}

/* ================================================================== */
/*  调试导出接口                                                      */
/* ================================================================== */

DebugStats *c_get_stats(void) { return &stats; }

void c_reset_stats(void) {
  memset(&stats, 0, sizeof(stats));
  memset(killer_moves, 0, sizeof(killer_moves));
}
