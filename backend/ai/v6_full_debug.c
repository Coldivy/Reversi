/**
 * v6_full_debug.c — v5 + solve() 启发式走法排序
 *
 * 新增改动（最后一个优化）：
 *   + solve() 中非 TT 走法也按静态权重 + 历史表 + 杀手着法排序
 *     不再按位序遍历（原版行为）
 *
 * 效果：终局穷举搜索的 Alpha-Beta 截断效率大幅提升
 *
 * 这是完整的 search_new 算法。
 */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOSS_SCORE -100000
#define WIN_SCORE  100000
#define HISTORY_MAX 800000

typedef uint64_t uint64;

typedef struct {
  long long nodes;       long long tt_hits;     long long beta_cuts;
  long long null_cuts;   long long futility_skips;
  long long lmr_attempts; long long lmr_researches;
  long long iid_searches;
  long long eval_calls;  long long tt_stores;
} DebugStats;

static DebugStats stats;
#define STATS_INC(field) (stats.field++)

static const uint64 MASKS[] = {
    0x8100000000000081, 0x0042000000004200, 0x4281000000008142,
    0x2400810000810024, 0x1800248181240018, 0x003C424242423C00,
    0x0000184242180000, 0x0000001818000000,
};
static const int WEIGHTS[] = {100, -50, -20, 10, 5, -2, 1, 0};
static int square_weights[64];
static int killer_moves[64][2];
static int history_table[64];

static inline int popcount(uint64 x) { return __builtin_popcountll(x); }
typedef struct { int move; int score; } RatedMove;

#define TT_EXACT 0
#define TT_ALPHA 1
#define TT_BETA  2
typedef struct { uint64 key; int score; int best_move; int depth; uint8_t flag; } TTEntry;
#define TT_SIZE (1 << 20)
#define TT_MASK (TT_SIZE - 1)
TTEntry transposition_table[TT_SIZE];

static uint64_t xorshift64_state = 88172645463325252ULL;
uint64_t next_random64() { xorshift64_state ^= xorshift64_state << 13; xorshift64_state ^= xorshift64_state >> 7; xorshift64_state ^= xorshift64_state << 17; return xorshift64_state; }
uint64 zobrist_P[64], zobrist_O[64];

uint64 get_zobrist_key(uint64 P, uint64 O) {
  uint64 key = 0, t = P;
  while (t) { int i = __builtin_ctzll(t); key ^= zobrist_P[i]; t &= t - 1; }
  t = O; while (t) { int i = __builtin_ctzll(t); key ^= zobrist_O[i]; t &= t - 1; }
  return key;
}

bool tt_lookup(uint64 key, int depth, int alpha, int beta, int *score, int *best_move) {
  uint64 index = key & TT_MASK; TTEntry *entry = &transposition_table[index];
  if (entry->key == key) {
    *best_move = entry->best_move;
    if (entry->depth >= depth) {
      if (entry->flag == TT_EXACT) { *score = entry->score; return true; }
      if (entry->flag == TT_ALPHA && entry->score <= alpha) { *score = entry->score; return true; }
      if (entry->flag == TT_BETA && entry->score >= beta)  { *score = entry->score; return true; }
    }
  }
  return false;
}

void tt_store(uint64 key, int depth, int score, int alpha, int beta, int best_move) {
  STATS_INC(tt_stores);
  uint64 index = key & TT_MASK; TTEntry *entry = &transposition_table[index];
  if (entry->key == 0 || depth >= entry->depth || entry->key != key) {
    uint8_t flag = TT_EXACT; if (score <= alpha) flag = TT_ALPHA; else if (score >= beta) flag = TT_BETA;
    *entry = (TTEntry){.key = key, .score = score, .best_move = best_move, .flag = flag, .depth = depth};
  }
}

uint64 get_legal_moves(uint64 P, uint64 O) {
  uint64 mask = O & 0x7E7E7E7E7E7E7E7EULL, moves = 0, t;
#define DIRECTION(SHIFT, MASK) t = (P SHIFT) & (MASK); for (int _i = 0; _i < 5; ++_i) t |= (t SHIFT) & (MASK); moves |= (t SHIFT);
  DIRECTION(<< 1, mask)  DIRECTION(>> 1, mask)  DIRECTION(<< 8, O)     DIRECTION(>> 8, O)
  DIRECTION(<< 7, mask)  DIRECTION(>> 9, mask)  DIRECTION(<< 9, mask)  DIRECTION(>> 7, mask)
  return moves & ~(P | O);
}

void make_move(uint64 P, uint64 O, int move, uint64 *newP, uint64 *newO) {
  uint64 m = 1ULL << move, flipped = 0, mask = O & 0x7E7E7E7E7E7E7E7EULL;
#define FLIP_DIR(SHIFT, MASK) { uint64 t = (m SHIFT) & (MASK); for (int _i = 0; _i < 5; ++_i) t |= (t SHIFT) & (MASK); if ((t SHIFT) & P) flipped |= t; }
  FLIP_DIR(<< 1, mask)  FLIP_DIR(>> 1, mask)  FLIP_DIR(<< 8, O)     FLIP_DIR(>> 8, O)
  FLIP_DIR(<< 7, mask)  FLIP_DIR(>> 9, mask)  FLIP_DIR(<< 9, mask)  FLIP_DIR(>> 7, mask)
  *newP = P | m | flipped;  *newO = O & ~flipped;
}

int evaluate(uint64 P, uint64 O) {
  STATS_INC(eval_calls);
  int score = 0;
  for (int i = 0; i < 8; i++) { score += popcount(P & MASKS[i]) * WEIGHTS[i]; score -= popcount(O & MASKS[i]) * WEIGHTS[i]; }
  uint64 pm = get_legal_moves(P, O), om = get_legal_moves(O, P);
  score += (popcount(pm) - popcount(om)) * 5;
  return score;
}

static inline int move_ordering_score(int move, int depth) {
  int score = square_weights[move] + history_table[move] / 16;
  if (move == killer_moves[depth][0]) score += 20000; else if (move == killer_moves[depth][1]) score += 10000;
  return score;
}

int negamax(int depth, uint64 P, uint64 O,
            int alpha, int beta, int *best_move, bool allow_null) {
  STATS_INC(nodes);
  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1, tt_score = -1;

  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) { STATS_INC(tt_hits); if (best_move) *best_move = tt_move; return tt_score; }
  if (depth == 0) return evaluate(P, O);

  uint64 moves = get_legal_moves(P, O);
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) {
      int p_cnt = popcount(P), o_cnt = popcount(O);
      if (p_cnt > o_cnt) return WIN_SCORE  + (p_cnt - o_cnt);
      if (p_cnt < o_cnt) return LOSS_SCORE + (p_cnt - o_cnt);
      return 0;
    }
    return -negamax(depth - 1, O, P, -beta, -alpha, NULL, allow_null);
  }

  if (allow_null && depth >= 4) {
    int R = 3 + depth / 4, null_depth = depth - 1 - R;
    if (null_depth >= 1 && (64 - popcount(P | O)) > 12) {
      int null_score = -negamax(null_depth, O, P, -beta, -beta + 1, NULL, false);
      if (null_score >= beta) { STATS_INC(null_cuts); return null_score; }
    }
  }

  if (tt_move == -1 && depth >= 5) { STATS_INC(iid_searches); int iid_depth = depth / 2; negamax(iid_depth, P, O, alpha, beta, &tt_move, false); }

  int best_score = LOSS_SCORE, local_best_move = -1, moves_searched = 0;
  bool is_first_move = true;

  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO; make_move(P, O, tt_move, &nP, &nO);
    int score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL, true);
    is_first_move = false; moves_searched++;
    if (score > best_score) { best_score = score; local_best_move = tt_move; }
    if (score > alpha) alpha = score;
    if (alpha >= beta) { history_table[tt_move] += depth * depth; goto end_search; }
  }

  {
    uint64 temp_moves = moves; if (tt_move != -1) temp_moves &= ~(1ULL << tt_move);
    RatedMove rated_moves[64]; int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves), sc = move_ordering_score(idx, depth);
      int j = size; while (j > 0 && rated_moves[j - 1].score < sc) { rated_moves[j] = rated_moves[j - 1]; j--; }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc}; size++; temp_moves &= temp_moves - 1;
    }

    for (int idx = 0; idx < size; idx++) {
      int i = rated_moves[idx].move; uint64 nP, nO; make_move(P, O, i, &nP, &nO); int score;
      if (depth == 1 && moves_searched >= 3) {
        int stand_pat = -evaluate(nO, nP);
        if (stand_pat + 300 < alpha) { STATS_INC(futility_skips); if (stand_pat > best_score) best_score = stand_pat; continue; }
      }
      int lmr_reduction = 0;
      if (moves_searched >= 4 && depth >= 3 && !is_first_move) {
        lmr_reduction = 1 + (moves_searched - 4) / 8;
        if (lmr_reduction > 3) lmr_reduction = 3;
        if (depth - 1 - lmr_reduction < 1) lmr_reduction = depth - 2;
      }
      if (is_first_move) { score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL, true); is_first_move = false; }
      else if (lmr_reduction > 0) {
        STATS_INC(lmr_attempts); score = -negamax(depth - 1 - lmr_reduction, nO, nP, -alpha - 1, -alpha, NULL, true);
        if (score > alpha) { STATS_INC(lmr_researches); score = -negamax(depth - 1, nO, nP, -alpha - 1, -alpha, NULL, true); }
      } else score = -negamax(depth - 1, nO, nP, -alpha - 1, -alpha, NULL, true);
      if (!is_first_move && score > alpha && score < beta) score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL, true);
      moves_searched++;
      if (score > best_score) { best_score = score; local_best_move = i; }
      if (score > alpha) alpha = score;
      if (alpha >= beta) {
        if (i != tt_move) { if (killer_moves[depth][0] != i) { killer_moves[depth][1] = killer_moves[depth][0]; killer_moves[depth][0] = i; } }
        history_table[i] += depth * depth; if (history_table[i] > HISTORY_MAX) for (int h = 0; h < 64; h++) history_table[h] /= 2;
        STATS_INC(beta_cuts); break;
      }
    }
  }

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move) *best_move = local_best_move; return best_score;
}

/* ================================================================== */
/*  终局完美搜索 solve ── ★ v6: 启发式走法排序                        */
/* ================================================================== */

int evaluate_exact_score(uint64 P, uint64 O) {
  int p_cnt = popcount(P), o_cnt = popcount(O);
  if (p_cnt == 0) return -64; if (o_cnt == 0) return 64;
  return (int)(p_cnt - o_cnt);
}

int solve(uint64 P, uint64 O, int alpha, int beta, int *best_move) {
  STATS_INC(nodes);
  int depth = 64 - popcount(P | O), original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1, tt_score = -1;
  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) { STATS_INC(tt_hits); if (best_move) *best_move = tt_move; return tt_score; }
  uint64 moves = get_legal_moves(P, O);
  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) return evaluate_exact_score(P, O);
    return -solve(O, P, -beta, -alpha, NULL);
  }
  int best_score = LOSS_SCORE, local_best_move = -1;
  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO; make_move(P, O, tt_move, &nP, &nO);
    int score = -solve(nO, nP, -beta, -alpha, NULL);
    if (score > best_score) { best_score = score; local_best_move = tt_move; }
    if (score > alpha) alpha = score;
    if (alpha >= beta) { STATS_INC(beta_cuts); goto end_search; }
  }

  /* ═══════════════════════════════════════════════════════════════
   *  ★ v6: solve() 剩余走法按启发式排序
   * ═══════════════════════════════════════════════════════════════ */
  {
    uint64 temp_moves = moves; if (tt_move != -1) temp_moves &= ~(1ULL << tt_move);

    RatedMove rated_moves[64]; int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves);
      int sc = square_weights[idx] + history_table[idx] / 16;
      if (idx == killer_moves[depth][0])        sc += 20000;
      else if (idx == killer_moves[depth][1])   sc += 10000;
      int j = size; while (j > 0 && rated_moves[j - 1].score < sc) { rated_moves[j] = rated_moves[j - 1]; j--; }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc}; size++; temp_moves &= temp_moves - 1;
    }

    for (int idx = 0; idx < size; idx++) {
      int i = rated_moves[idx].move;
      uint64 nP, nO; make_move(P, O, i, &nP, &nO);
      int score = -solve(nO, nP, -beta, -alpha, NULL);
      if (score > best_score) { best_score = score; local_best_move = i; }
      if (score > alpha) alpha = score;
      if (alpha >= beta) { STATS_INC(beta_cuts); break; }
    }
  }

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move) *best_move = local_best_move; return best_score;
}

/* ── 导出接口 ── */
void c_init_search() {
  for (int i = 0; i < 64; i++) { zobrist_P[i] = next_random64(); zobrist_O[i] = next_random64(); }
  memset(transposition_table, 0, sizeof(transposition_table));
  for (int pos = 0; pos < 64; pos++) {
    uint64_t bit = 1ULL << pos; square_weights[pos] = 0;
    for (int i = 0; i < 8; i++) if (MASKS[i] & bit) { square_weights[pos] = WEIGHTS[i]; break; }
  }
  memset(killer_moves, 0, sizeof(killer_moves)); memset(history_table, 0, sizeof(history_table));
  memset(&stats, 0, sizeof(stats));
}

int c_get_best_move(int depth, uint64 player_bb, uint64 opponent_bb, int *move) {
  if (move) *move = -1;
  int empty_squares = 64 - popcount(player_bb | opponent_bb);
  static bool tt_cleared_for_endgame = false;
  if (empty_squares <= 12) {
    if (!tt_cleared_for_endgame) { memset(transposition_table, 0, sizeof(transposition_table)); tt_cleared_for_endgame = true; }
    return solve(player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, move);
  } else { tt_cleared_for_endgame = false; }
  int score = LOSS_SCORE, temp_move = -1;
  for (int d = 1; d <= depth; d++) {
    int current_best_move = -1;
    if (d >= 3) {
      int margin = 50, a = score - margin, b = score + margin;
      while (1) {
        if (a < LOSS_SCORE) a = LOSS_SCORE; if (b > WIN_SCORE) b = WIN_SCORE;
        current_best_move = -1;
        int val = negamax(d, player_bb, opponent_bb, a, b, &current_best_move, true);
        if (val <= a) { if (a == LOSS_SCORE) { score = val; break; } margin *= 2; a = val - margin; b = val + margin; }
        else if (val >= b) { if (b == WIN_SCORE) { score = val; break; } margin *= 2; b = val + margin; a = val - margin; }
        else { score = val; break; }
      }
    } else { score = negamax(d, player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, &current_best_move, true); }
    if (current_best_move != -1) temp_move = current_best_move;
  }
  if (move) *move = temp_move; return score;
}

DebugStats *c_get_stats(void) { return &stats; }
void c_reset_stats(void) { memset(&stats, 0, sizeof(stats)); memset(killer_moves, 0, sizeof(killer_moves)); memset(history_table, 0, sizeof(history_table)); }
