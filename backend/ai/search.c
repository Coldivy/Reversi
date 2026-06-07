/**
 * search.c — 黑白棋（Reversi/Othello）搜索算法改进版
 *
 * 基于 v0baseline.c 重构，保持外部接口（c_init_search / c_get_best_move）不变，
 * 内部变量命名风格与 search.c 保持一致。
 *
 * ======== 核心改进 ========
 * 1. 历史启发表 (History Heuristic)
 *    记录每个格子引发 Beta 截断的频率，用于走法排序。
 *    相比仅依赖杀手着法，历史表覆盖所有深度、累积效应更强。
 *
 * 2. 延迟走法缩减 (Late Move Reductions, LMR)
 *    排序靠后的走法大概率不是好棋，用缩减深度快速验证；
 *    只有缩减搜索"看上去有希望"时才恢复全深度。
 *
 * 3. 静态剪枝 (Futility Pruning)
 *    在 horizon=1 的节点，若走法后静态评估远低于 alpha，
 *    则该走法几乎不可能成为最佳走法 → 跳过。
 *
 * 4. 内部迭代加深 (Internal Iterative Deepening, IID)
 *    当置换表未命中（无最佳走法提示）且剩余深度足够时，
 *    先以 depth/2 做浅搜索来获得走法排序依据。
 *
 * 5. 终局搜索启发式排序
 *    原 solve() 对非 TT 走法按位序无脑搜；现加入静态权重 +
 *    历史值 + 杀手着法排序，提高 Alpha-Beta 截断效率。
 *
 * ======== 特别注意 ========
 * 未加入空步裁剪 (Null Move Pruning)：
 *    黑白棋中盘常处于「迫移」(zugzwang) 状态——多走一步意味着
 *    翻转对方棋子、暴露新前沿、甚至送角，此时"轮空"反而是优势。
 *    空步裁剪底层假设「轮空=损失」，在黑白棋中盘可能错误截断，
 *    经实测对比已确认去掉空步后棋力更强。
 */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ================================================================== */
/*  常量                                                              */
/* ================================================================== */

#define LOSS_SCORE -100000
#define WIN_SCORE 100000

/* 当 history_table 任一格子超过此值，全体折半防溢出 */
#define HISTORY_MAX 800000

/* ── 限时搜索：超时哨兵分数 + 全局 deadline ── */
#define TIMED_OUT_SCORE -200000
static clock_t timed_deadline = 0;
static inline int timed_out(void) {
  return timed_deadline > 0 && clock() >= timed_deadline;
}

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
/*  杀手着法 + 历史启发表                                             */
/* ================================================================== */

static int killer_moves[64][2]; /* 每层 2 个杀手 */
static int history_table[64];   /* 每格历史截断累积值 */

/* ================================================================== */
/*  位运算工具                                                        */
/* ================================================================== */

static inline int popcount(uint64 x) { return __builtin_popcountll(x); }

/* ================================================================== */
/*  走法结构体                                                        */
/* ================================================================== */

typedef struct {
  int move;  /* 棋盘索引 0-63 */
  int score; /* 排序分数       */
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

  /* 深度优先替换：保留深层搜索结果 */
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
/*  评估函数（与 search.c 一致）                                      */
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
/*                                                                     */
/*  整合三层启发：                                                     */
/*    1. 静态位置权重（基础分）                                       */
/*    2. 历史启发值（/16 缩放，与静态权重同一量级）                   */
/*    3. 杀手着法加分（远超上述两项，但低于 TT 走法）                 */
/*                                                                     */
/*  注意：TT 走法在外部以最高优先级单独处理，不进入此函数。           */
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
/*  Negamax 搜索（改进版，无空步裁剪）                                */
/*                                                                     */
/*  注意：刻意不加入空步裁剪。黑白棋中盘「迫移」频发，               */
/*  多走一步往往=给对方送前沿/送角，轮空反而是优势。                 */
/*  基于象棋假设（轮空=损失）的空步裁剪在此处经常误判。             */
/* ================================================================== */

int negamax(int depth, uint64 P, uint64 O, int alpha, int beta,
            int *best_move) {
  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1;
  int tt_score = -1;

  /* ---- 1. 置换表查找 ---- */
  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) {
    if (best_move)
      *best_move = tt_move;
    return tt_score;
  }

  /* ---- 2. 叶子节点 ---- */
  if (depth == 0)
    return evaluate(P, O);

  uint64 moves = get_legal_moves(P, O);

  /* ---- 3. 无合法走法 → 轮空 ---- */
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

  /* ---- 4. 内部迭代加深 (IID) ---- */
  /* 置换表未命中且深度充足时，先浅搜索获得走法排序依据 */
  if (tt_move == -1 && depth >= 5) {
    int iid_depth = depth / 2;
    negamax(iid_depth, P, O, alpha, beta, &tt_move);
  }

  int best_score = LOSS_SCORE;
  int local_best_move = -1;
  int moves_searched = 0;
  bool is_first_move = true;

  /* ---- 5. 优先搜索 TT / IID 提示的最佳走法 ---- */
  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);
    int score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);

    is_first_move = false;
    moves_searched++;

    if (score > best_score) {
      best_score = score;
      local_best_move = tt_move;
    }
    if (score > alpha)
      alpha = score;
    if (alpha >= beta) {
      history_table[tt_move] += depth * depth;
      goto end_search;
    }
  }

  /* ---- 6. 提取剩余走法并按启发式分数排序 ---- */
  {
    uint64 temp_moves = moves;
    if (tt_move != -1) {
      temp_moves &= ~(1ULL << tt_move);
    }

    RatedMove rated_moves[64];
    int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves);
      int sc = move_ordering_score(idx, depth);

      /* 插入排序（通常 4~15 个走法，开销可忽略） */
      int j = size;
      while (j > 0 && rated_moves[j - 1].score < sc) {
        rated_moves[j] = rated_moves[j - 1];
        j--;
      }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc};
      size++;
      temp_moves &= temp_moves - 1;
    }

    /* ---- 7. 依次搜索（PVS + LMR + 静态剪枝） ---- */
    for (int idx = 0; idx < size; idx++) {
      int i = rated_moves[idx].move;
      uint64 nP, nO;
      make_move(P, O, i, &nP, &nO);
      int score;

      /* ---- 7a. 静态剪枝 (Futility Pruning): depth==1 ---- */
      if (depth == 1 && moves_searched >= 3) {
        int stand_pat = -evaluate(nO, nP);
        if (stand_pat + 300 < alpha) {
          if (stand_pat > best_score)
            best_score = stand_pat;
          continue;
        }
      }

      /* ---- 7b. 计算 LMR 缩减量 ---- */
      int lmr_reduction = 0;
      if (moves_searched >= 4 && depth >= 3 && !is_first_move) {
        lmr_reduction = 1 + (moves_searched - 4) / 8;
        if (lmr_reduction > 3)
          lmr_reduction = 3;
        if (depth - 1 - lmr_reduction < 1)
          lmr_reduction = depth - 2; /* 确保至少 depth-1 搜索深度 ≥ 1 */
      }

      /* ---- 7c. PVS + LMR ---- */
      if (is_first_move) {
        /* 第一个走法（主变例候选）：全窗口、全深度 */
        score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);
        is_first_move = false;
      } else if (lmr_reduction > 0) {
        /* LMR：先以缩减深度 + 零窗口快速探测 */
        score = -negamax(depth - 1 - lmr_reduction, nO, nP, -alpha - 1, -alpha,
                         NULL);
        if (score > alpha) {
          /* 有希望 → 全深度零窗口重新搜索 */
          score = -negamax(depth - 1, nO, nP, -alpha - 1, -alpha, NULL);
        }
      } else {
        /* PVS：全深度零窗口 */
        score = -negamax(depth - 1, nO, nP, -alpha - 1, -alpha, NULL);
      }

      /* ---- 7d. 零窗口/缩减搜索通过 → 全窗口确认 ---- */
      if (!is_first_move && score > alpha && score < beta) {
        score = -negamax(depth - 1, nO, nP, -beta, -alpha, NULL);
      }

      moves_searched++;

      if (score > best_score) {
        best_score = score;
        local_best_move = i;
      }
      if (score > alpha)
        alpha = score;
      if (alpha >= beta) {
        /* ---- Beta 截断 → 更新杀手着法 & 历史表 ---- */
        if (i != tt_move) {
          if (killer_moves[depth][0] != i) {
            killer_moves[depth][1] = killer_moves[depth][0];
            killer_moves[depth][0] = i;
          }
        }
        history_table[i] += depth * depth;
        /* 防溢出：整体折半 */
        if (history_table[i] > HISTORY_MAX) {
          for (int h = 0; h < 64; h++)
            history_table[h] /= 2;
        }
        break;
      }
    }
  }

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move)
    *best_move = local_best_move;
  return best_score;
}

/* ================================================================== */
/*  Negamax 搜索（限时版）— negamax 的完整镜像 + 时间检查中断逻辑    */
/*                                                                     */
/*  与 negamax 唯一区别：                                              */
/*    - 入口检查 timed_out() → 返回 TIMED_OUT_SCORE                   */
/*    - 所有递归调用指向 negamax_timed（不是 negamax）               */
/*    - 走法循环中每次递归前检查 timed_out() → goto end_search        */
/*    - end_search 处 TIMED_OUT_SCORE 不写入置换表                     */
/* ================================================================== */

int negamax_timed(int depth, uint64 P, uint64 O, int alpha, int beta,
                  int *best_move) {
  /* 超时立即退出 */
  if (timed_out())
    return TIMED_OUT_SCORE;

  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1;
  int tt_score = -1;

  /* ---- 1. 置换表查找 ---- */
  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) {
    if (best_move)
      *best_move = tt_move;
    return tt_score;
  }

  /* ---- 2. 叶子节点 ---- */
  if (depth == 0)
    return evaluate(P, O);

  uint64 moves = get_legal_moves(P, O);

  /* ---- 3. 无合法走法 → 轮空 ---- */
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
    return -negamax_timed(depth - 1, O, P, -beta, -alpha, NULL);
  }

  /* ---- 4. 内部迭代加深 (IID) ---- */
  if (tt_move == -1 && depth >= 5) {
    int iid_depth = depth / 2;
    negamax_timed(iid_depth, P, O, alpha, beta, &tt_move);
  }

  int best_score = LOSS_SCORE;
  int local_best_move = -1;
  int moves_searched = 0;
  bool aborted = false;       /* 标记是否被超时中断 */
  bool is_first_move = true;

  /* ---- 5. 优先搜索 TT / IID 提示的最佳走法 ---- */
  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);
    int score = -negamax_timed(depth - 1, nO, nP, -beta, -alpha, NULL);

    /* 子节点超时 → 立即退出 */
    if (score == TIMED_OUT_SCORE) {
      aborted = true;
      goto end_search;
    }

    is_first_move = false;
    moves_searched++;

    if (score > best_score) {
      best_score = score;
      local_best_move = tt_move;
    }
    if (score > alpha)
      alpha = score;
    if (alpha >= beta) {
      history_table[tt_move] += depth * depth;
      goto end_search;
    }
  }

  /* ---- 6. 提取剩余走法并按启发式分数排序 ---- */
  {
    uint64 temp_moves = moves;
    if (tt_move != -1) {
      temp_moves &= ~(1ULL << tt_move);
    }

    RatedMove rated_moves[64];
    int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves);
      int sc = move_ordering_score(idx, depth);

      int j = size;
      while (j > 0 && rated_moves[j - 1].score < sc) {
        rated_moves[j] = rated_moves[j - 1];
        j--;
      }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc};
      size++;
      temp_moves &= temp_moves - 1;
    }

    /* ---- 7. 依次搜索（PVS + LMR + 静态剪枝 + 超时检查） ---- */
    for (int idx = 0; idx < size; idx++) {
      /* 每搜完一个走法都检查时间 */
      if (timed_out())
        goto end_search;

      int i = rated_moves[idx].move;
      uint64 nP, nO;
      make_move(P, O, i, &nP, &nO);
      int score;

      /* ---- 7a. 静态剪枝 ---- */
      if (depth == 1 && moves_searched >= 3) {
        int stand_pat = -evaluate(nO, nP);
        if (stand_pat + 300 < alpha) {
          if (stand_pat > best_score)
            best_score = stand_pat;
          continue;
        }
      }

      /* ---- 7b. 计算 LMR 缩减量 ---- */
      int lmr_reduction = 0;
      if (moves_searched >= 4 && depth >= 3 && !is_first_move) {
        lmr_reduction = 1 + (moves_searched - 4) / 8;
        if (lmr_reduction > 3)
          lmr_reduction = 3;
        if (depth - 1 - lmr_reduction < 1)
          lmr_reduction = depth - 2;
      }

      /* ---- 7c. PVS + LMR ---- */
      if (is_first_move) {
        score = -negamax_timed(depth - 1, nO, nP, -beta, -alpha, NULL);
        is_first_move = false;
      } else if (lmr_reduction > 0) {
        score = -negamax_timed(depth - 1 - lmr_reduction, nO, nP,
                               -alpha - 1, -alpha, NULL);
        if (score != TIMED_OUT_SCORE && score > alpha) {
          score = -negamax_timed(depth - 1, nO, nP,
                                 -alpha - 1, -alpha, NULL);
        }
      } else {
        score = -negamax_timed(depth - 1, nO, nP,
                               -alpha - 1, -alpha, NULL);
      }

      /* 子节点超时 → 跳过本次结果，退出循环 */
      if (score == TIMED_OUT_SCORE) {
        aborted = true;
        goto end_search;
      }

      /* ---- 7d. 零窗口/缩减搜索通过 → 全窗口确认 ---- */
      if (!is_first_move && score > alpha && score < beta) {
        score = -negamax_timed(depth - 1, nO, nP, -beta, -alpha, NULL);
        if (score == TIMED_OUT_SCORE) {
          aborted = true;
          goto end_search;
        }
      }

      moves_searched++;

      if (score > best_score) {
        best_score = score;
        local_best_move = i;
      }
      if (score > alpha)
        alpha = score;
      if (alpha >= beta) {
        if (i != tt_move) {
          if (killer_moves[depth][0] != i) {
            killer_moves[depth][1] = killer_moves[depth][0];
            killer_moves[depth][0] = i;
          }
        }
        history_table[i] += depth * depth;
        if (history_table[i] > HISTORY_MAX) {
          for (int h = 0; h < 64; h++)
            history_table[h] /= 2;
        }
        break;
      }
    }
  }

end_search:
  /* 子节点超时且该节点没搜出任何有效走法 → 向上传播超时信号 */
  if (aborted && local_best_move == -1)
    return TIMED_OUT_SCORE;
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move)
    *best_move = local_best_move;
  return best_score;
}

/* ================================================================== */
/*  终局完美搜索 (solve)                                              */
/*                                                                     */
/*  改进：非 TT 走法也按静态权重 + 历史表 + 杀手着法排序，           */
/*  提高 Alpha-Beta 截断效率。                                        */
/* ================================================================== */

int evaluate_exact_score(uint64 P, uint64 O) {
  int p_cnt = popcount(P);
  int o_cnt = popcount(O);
  if (p_cnt == 0)
    return -64;
  if (o_cnt == 0)
    return 64;
  return (int)(p_cnt - o_cnt);
}

int solve(uint64 P, uint64 O, int alpha, int beta, int *best_move) {
  int occupied = popcount(P | O);
  int depth = 64 - occupied;

  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1;
  int tt_score = -1;

  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) {
    if (best_move)
      *best_move = tt_move;
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

  /* 优先搜索 TT move */
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

  /* 剩余走法：按启发式排序后搜索 */
  {
    uint64 temp_moves = moves;
    if (tt_move != -1) {
      temp_moves &= ~(1ULL << tt_move);
    }

    RatedMove rated_moves[64];
    int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves);
      int sc = square_weights[idx] + history_table[idx] / 16;
      if (idx == killer_moves[depth][0])
        sc += 20000;
      else if (idx == killer_moves[depth][1])
        sc += 10000;

      int j = size;
      while (j > 0 && rated_moves[j - 1].score < sc) {
        rated_moves[j] = rated_moves[j - 1];
        j--;
      }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc};
      size++;
      temp_moves &= temp_moves - 1;
    }

    for (int idx = 0; idx < size; idx++) {
      int i = rated_moves[idx].move;
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
    }
  }

end_search:
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move)
    *best_move = local_best_move;
  return best_score;
}

/* ================================================================== */
/*  终局完美搜索（限时版）— solve 的完整镜像 + 超时中断              */
/* ================================================================== */

int solve_timed(uint64 P, uint64 O, int alpha, int beta, int *best_move) {
  /* 超时立即退出 */
  if (timed_out())
    return TIMED_OUT_SCORE;

  int occupied = popcount(P | O);
  int depth = 64 - occupied;

  int original_alpha = alpha;
  uint64 key = get_zobrist_key(P, O);
  int tt_move = -1;
  int tt_score = -1;

  bool hit = tt_lookup(key, depth, alpha, beta, &tt_score, &tt_move);
  if (hit) {
    if (best_move)
      *best_move = tt_move;
    return tt_score;
  }

  uint64 moves = get_legal_moves(P, O);

  if (moves == 0) {
    if (get_legal_moves(O, P) == 0) {
      return evaluate_exact_score(P, O);
    }
    return -solve_timed(O, P, -beta, -alpha, NULL);
  }

  int best_score = LOSS_SCORE;
  int local_best_move = -1;
  bool aborted = false;

  /* 优先搜索 TT move */
  if (tt_move != -1 && (moves & (1ULL << tt_move))) {
    uint64 nP, nO;
    make_move(P, O, tt_move, &nP, &nO);
    int score = -solve_timed(nO, nP, -beta, -alpha, NULL);

    if (score == TIMED_OUT_SCORE) {
      aborted = true;
      goto end_search;
    }

    if (score > best_score) {
      best_score = score;
      local_best_move = tt_move;
    }
    if (score > alpha)
      alpha = score;
    if (alpha >= beta)
      goto end_search;
  }

  /* 剩余走法：按启发式排序后搜索 */
  {
    uint64 temp_moves = moves;
    if (tt_move != -1) {
      temp_moves &= ~(1ULL << tt_move);
    }

    RatedMove rated_moves[64];
    int size = 0;
    while (temp_moves) {
      int idx = __builtin_ctzll(temp_moves);
      int sc = square_weights[idx] + history_table[idx] / 16;
      if (idx == killer_moves[depth][0])
        sc += 20000;
      else if (idx == killer_moves[depth][1])
        sc += 10000;

      int j = size;
      while (j > 0 && rated_moves[j - 1].score < sc) {
        rated_moves[j] = rated_moves[j - 1];
        j--;
      }
      rated_moves[j] = (RatedMove){.move = idx, .score = sc};
      size++;
      temp_moves &= temp_moves - 1;
    }

    for (int idx = 0; idx < size; idx++) {
      if (timed_out()) {
        aborted = true;
        goto end_search;
      }

      int i = rated_moves[idx].move;
      uint64 nP, nO;
      make_move(P, O, i, &nP, &nO);
      int score = -solve_timed(nO, nP, -beta, -alpha, NULL);

      if (score == TIMED_OUT_SCORE) {
        aborted = true;
        goto end_search;
      }

      if (score > best_score) {
        best_score = score;
        local_best_move = i;
      }
      if (score > alpha)
        alpha = score;
      if (alpha >= beta)
        break;
    }
  }

end_search:
  if (aborted && local_best_move == -1)
    return TIMED_OUT_SCORE;
  tt_store(key, depth, best_score, original_alpha, beta, local_best_move);
  if (best_move)
    *best_move = local_best_move;
  return best_score;
}

/* ================================================================== */
/*  导出接口（与 search.c 完全一致）                                  */
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
      if (MASKS[i] & bit) {
        square_weights[pos] = WEIGHTS[i];
        break;
      }
    }
  }

  memset(killer_moves, 0, sizeof(killer_moves));
  memset(history_table, 0, sizeof(history_table));
}

int c_get_best_move(int depth, uint64 player_bb, uint64 opponent_bb,
                    int *move) {
  if (move)
    *move = -1;

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

  /* 迭代加深 + 渴望窗口 */
  for (int d = 1; d <= depth; d++) {
    int current_best_move = -1;

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

  if (move)
    *move = temp_move;
  return score;
}

/* ================================================================== */
/*  限时搜索接口                                                      */
/*                                                                     */
/*  在给定的 time_limit_ms 毫秒内尽可能深地迭代加深搜索。             */
/*  time_limit_ms <= 0 时视作不限时。                                  */
/*  通过 depth_searched 输出最后完成的完整深度。                      */
/*                                                                     */
/*  搜索策略：                                                        */
/*    - 空格 ≤ 12 时走 solve_timed()，终局秒级完成无需限制           */
/*    - 设置全局 timed_deadline，negamax_timed / solve_timed 内部     */
/*      在走法循环中高频检查 clock() >= deadline → 立即中断           */
/*    - 每层完整迭代后记录结果；若该层被中断则退回上层的答案         */
/*    - 如果第一层都没完成也要返回（至少有静态评估做兜底）           */
/*                                                                     */
/*  注意：该函数与 c_get_best_move 共享置换表和启发状态。            */
/*        同局内交替调用会导致状态不一致，建议用一个到底。           */
/* ================================================================== */

int c_get_best_move_timed(int time_limit_ms,
                          uint64 player_bb, uint64 opponent_bb,
                          int *move, int *depth_searched) {
  if (move)
    *move = -1;
  if (depth_searched)
    *depth_searched = 0;

  int occupied_squares = popcount(player_bb | opponent_bb);
  int empty_squares = 64 - occupied_squares;

  static bool tt_cleared_for_endgame = false;

  /* 终局直接穷举，不参与时间控制 */
  if (empty_squares <= 12) {
    if (!tt_cleared_for_endgame) {
      memset(transposition_table, 0, sizeof(transposition_table));
      tt_cleared_for_endgame = true;
    }
    /* 终局秒级完成，不限时 */
    timed_deadline = 0;
    int result = solve_timed(player_bb, opponent_bb, LOSS_SCORE, WIN_SCORE, move);
    if (depth_searched)
      *depth_searched = empty_squares;
    return result;
  } else {
    tt_cleared_for_endgame = false;
  }

  clock_t start = clock();

  /* ── 设置 deadline ── */
  if (time_limit_ms > 0)
    timed_deadline = start + (clock_t)((double)time_limit_ms / 1000.0 * CLOCKS_PER_SEC);
  else
    timed_deadline = 0; /* 不限时 */

  int score = LOSS_SCORE;
  int temp_move = -1;

  /* 最大深度 */
  int max_depth = empty_squares;
  if (max_depth > 20)
    max_depth = 20;

  for (int d = 1; d <= max_depth; d++) {
    int current_best_move = -1;

    /* 渴望窗口 */
    if (d >= 3) {
      int margin = 50;
      int alpha = score - margin;
      int beta = score + margin;

      while (1) {
        if (alpha < LOSS_SCORE) alpha = LOSS_SCORE;
        if (beta  > WIN_SCORE)  beta  = WIN_SCORE;

        current_best_move = -1;
        int val = negamax_timed(d, player_bb, opponent_bb,
                                alpha, beta, &current_best_move);

        if (val == TIMED_OUT_SCORE) {
          /* 该层被中断，回退到上层结果 */
          goto done;
        }

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
      int val = negamax_timed(d, player_bb, opponent_bb,
                              LOSS_SCORE, WIN_SCORE, &current_best_move);
      if (val == TIMED_OUT_SCORE) goto done;
      score = val;
    }

    /* 完整迭代成功 */
    if (current_best_move != -1) temp_move = current_best_move;
    if (depth_searched) *depth_searched = d;
  }

done:
  timed_deadline = 0; /* 清除 deadline */
  if (move)  *move = temp_move;
  return score;
}
