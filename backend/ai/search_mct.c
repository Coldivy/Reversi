/**
 * search_mct.c — 黑白棋（Reversi/Othello）MCTS 搜索引擎
 *
 * 编译: gcc -shared -O2 -o search_mct.dll search_mct.c
 *       gcc -shared -O2 -fPIC -o search_mct.so search_mct.c (Linux)
 *
 * 接口:
 *   void mcts_search(int board[8][8], int player, int *out_x, int *out_y, int iterations)
 *   board: 8x8数组，0=空 1=黑 2=白
 *   player: 1=黑 2=白
 *   out_x/out_y: 输出落子坐标
 *   iterations: MCTS迭代次数
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint64_t uint64;

/* ================================================================== */
/*  常量                                                              */
/* ================================================================== */

#define TT_SIZE 65536

/* 位棋盘边界掩码 */
#define NOT_A 0xfefefefefefefefeULL
#define NOT_H 0x7f7f7f7f7f7f7f7fULL
#define FULL  0xffffffffffffffffULL

/* UCB1 探索常数 */
#define UCB_C 1.414

/* ================================================================== */
/*  位置权重表（启发式）                                              */
/* ================================================================== */

static const int POS_W[64] = {
    100, -20,  10,   5,   5,  10, -20, 100,
    -20, -50,  -2,  -2,  -2,  -2, -50, -20,
     10,  -2,  -1,  -1,  -1,  -1,  -2,  10,
      5,  -2,  -1,  -1,  -1,  -1,  -2,   5,
      5,  -2,  -1,  -1,  -1,  -1,  -2,   5,
     10,  -2,  -1,  -1,  -1,  -1,  -2,  10,
    -20, -50,  -2,  -2,  -2,  -2, -50, -20,
    100, -20,  10,   5,   5,  10, -20, 100
};

/* ================================================================== */
/*  位运算工具                                                        */
/* ================================================================== */

static inline int popcount(uint64 x) {
  return __builtin_popcountll(x);
}

static inline int bit_to_pos(uint64 x) {
  return __builtin_ctzll(x);
}

/* ================================================================== */
/*  棋盘转换                                                          */
/* ================================================================== */

/* 8x8 数组 → 位棋盘 */
static void grid_to_bits(int grid[8][8], uint64 *black, uint64 *white) {
  *black = 0;
  *white = 0;
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      int idx = i * 8 + j;
      if (grid[i][j] == 1)
        *black |= (1ULL << idx);
      else if (grid[i][j] == 2)
        *white |= (1ULL << idx);
    }
  }
}

/* 位棋盘 → 8x8 数组 */
static void bits_to_grid(uint64 black, uint64 white, int grid[8][8]) {
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++)
      grid[i][j] = 0;
  for (int i = 0; i < 64; i++) {
    int r = i / 8, c = i % 8;
    if ((black >> i) & 1ULL)  grid[r][c] = 1;
    else if ((white >> i) & 1ULL) grid[r][c] = 2;
  }
}

/* ================================================================== */
/*  位棋盘核心操作                                                    */
/* ================================================================== */

/* 计算合法走法（返回位棋盘） */
static uint64 get_legal_moves(uint64 player, uint64 opponent) {
  uint64 empty = ~(player | opponent) & FULL;
  uint64 moves = 0;
  uint64 t;

  /* 左 */
  t = (player << 1) & NOT_A & opponent;
  t |= (t << 1) & NOT_A & opponent;
  t |= (t << 1) & NOT_A & opponent;
  t |= (t << 1) & NOT_A & opponent;
  t |= (t << 1) & NOT_A & opponent;
  t |= (t << 1) & NOT_A & opponent;
  moves |= (t << 1) & NOT_A & empty;

  /* 右 */
  t = (player >> 1) & NOT_H & opponent;
  t |= (t >> 1) & NOT_H & opponent;
  t |= (t >> 1) & NOT_H & opponent;
  t |= (t >> 1) & NOT_H & opponent;
  t |= (t >> 1) & NOT_H & opponent;
  t |= (t >> 1) & NOT_H & opponent;
  moves |= (t >> 1) & NOT_H & empty;

  /* 上 */
  t = (player << 8) & opponent;
  t |= (t << 8) & opponent;
  t |= (t << 8) & opponent;
  t |= (t << 8) & opponent;
  t |= (t << 8) & opponent;
  t |= (t << 8) & opponent;
  moves |= (t << 8) & empty;

  /* 下 */
  t = (player >> 8) & opponent;
  t |= (t >> 8) & opponent;
  t |= (t >> 8) & opponent;
  t |= (t >> 8) & opponent;
  t |= (t >> 8) & opponent;
  t |= (t >> 8) & opponent;
  moves |= (t >> 8) & empty;

  /* 左上 */
  t = (player << 9) & NOT_A & opponent;
  t |= (t << 9) & NOT_A & opponent;
  t |= (t << 9) & NOT_A & opponent;
  t |= (t << 9) & NOT_A & opponent;
  t |= (t << 9) & NOT_A & opponent;
  t |= (t << 9) & NOT_A & opponent;
  moves |= (t << 9) & NOT_A & empty;

  /* 右上 */
  t = (player << 7) & NOT_H & opponent;
  t |= (t << 7) & NOT_H & opponent;
  t |= (t << 7) & NOT_H & opponent;
  t |= (t << 7) & NOT_H & opponent;
  t |= (t << 7) & NOT_H & opponent;
  t |= (t << 7) & NOT_H & opponent;
  moves |= (t << 7) & NOT_H & empty;

  /* 左下 */
  t = (player >> 7) & NOT_A & opponent;
  t |= (t >> 7) & NOT_A & opponent;
  t |= (t >> 7) & NOT_A & opponent;
  t |= (t >> 7) & NOT_A & opponent;
  t |= (t >> 7) & NOT_A & opponent;
  t |= (t >> 7) & NOT_A & opponent;
  moves |= (t >> 7) & NOT_A & empty;

  /* 右下 */
  t = (player >> 9) & NOT_H & opponent;
  t |= (t >> 9) & NOT_H & opponent;
  t |= (t >> 9) & NOT_H & opponent;
  t |= (t >> 9) & NOT_H & opponent;
  t |= (t >> 9) & NOT_H & opponent;
  t |= (t >> 9) & NOT_H & opponent;
  moves |= (t >> 9) & NOT_H & empty;

  return moves;
}

/* 计算翻转 */
static uint64 get_flips(uint64 player, uint64 opponent, int pos) {
  uint64 m = 1ULL << pos;
  uint64 flips = 0;
  uint64 t;

  /* 左 */
  t = (m << 1) & NOT_A & opponent;
  t |= (t << 1) & NOT_A & opponent;
  t |= (t << 1) & NOT_A & opponent;
  t |= (t << 1) & NOT_A & opponent;
  t |= (t << 1) & NOT_A & opponent;
  t |= (t << 1) & NOT_A & opponent;
  if ((t << 1) & NOT_A & player) flips |= t;

  /* 右 */
  t = (m >> 1) & NOT_H & opponent;
  t |= (t >> 1) & NOT_H & opponent;
  t |= (t >> 1) & NOT_H & opponent;
  t |= (t >> 1) & NOT_H & opponent;
  t |= (t >> 1) & NOT_H & opponent;
  t |= (t >> 1) & NOT_H & opponent;
  if ((t >> 1) & NOT_H & player) flips |= t;

  /* 上 */
  t = (m << 8) & opponent;
  t |= (t << 8) & opponent;
  t |= (t << 8) & opponent;
  t |= (t << 8) & opponent;
  t |= (t << 8) & opponent;
  t |= (t << 8) & opponent;
  if ((t << 8) & player) flips |= t;

  /* 下 */
  t = (m >> 8) & opponent;
  t |= (t >> 8) & opponent;
  t |= (t >> 8) & opponent;
  t |= (t >> 8) & opponent;
  t |= (t >> 8) & opponent;
  t |= (t >> 8) & opponent;
  if ((t >> 8) & player) flips |= t;

  /* 左上 */
  t = (m << 9) & NOT_A & opponent;
  t |= (t << 9) & NOT_A & opponent;
  t |= (t << 9) & NOT_A & opponent;
  t |= (t << 9) & NOT_A & opponent;
  t |= (t << 9) & NOT_A & opponent;
  t |= (t << 9) & NOT_A & opponent;
  if ((t << 9) & NOT_A & player) flips |= t;

  /* 右上 */
  t = (m << 7) & NOT_H & opponent;
  t |= (t << 7) & NOT_H & opponent;
  t |= (t << 7) & NOT_H & opponent;
  t |= (t << 7) & NOT_H & opponent;
  t |= (t << 7) & NOT_H & opponent;
  t |= (t << 7) & NOT_H & opponent;
  if ((t << 7) & NOT_H & player) flips |= t;

  /* 左下 */
  t = (m >> 7) & NOT_A & opponent;
  t |= (t >> 7) & NOT_A & opponent;
  t |= (t >> 7) & NOT_A & opponent;
  t |= (t >> 7) & NOT_A & opponent;
  t |= (t >> 7) & NOT_A & opponent;
  t |= (t >> 7) & NOT_A & opponent;
  if ((t >> 7) & NOT_A & player) flips |= t;

  /* 右下 */
  t = (m >> 9) & NOT_H & opponent;
  t |= (t >> 9) & NOT_H & opponent;
  t |= (t >> 9) & NOT_H & opponent;
  t |= (t >> 9) & NOT_H & opponent;
  t |= (t >> 9) & NOT_H & opponent;
  t |= (t >> 9) & NOT_H & opponent;
  if ((t >> 9) & NOT_H & player) flips |= t;

  return flips;
}

/* 执行落子 */
static void do_move(uint64 *player, uint64 *opponent, int pos) {
  uint64 f = get_flips(*player, *opponent, pos);
  *player |= (1ULL << pos) | f;
  *opponent &= ~f;
}

/* 检查游戏是否结束 */
static bool is_game_over(uint64 black, uint64 white) {
  return (get_legal_moves(black, white) == 0 &&
          get_legal_moves(white, black) == 0);
}

/* ================================================================== */
/*  MCTS 节点                                                         */
/* ================================================================== */

typedef struct MCTSNode {
  uint64 p;               /* 己方位棋盘 */
  uint64 o;               /* 对方位棋盘 */
  int player;             /* 当前玩家 1=黑 2=白 */
  struct MCTSNode *parent;
  int move;               /* 到达此节点的落子位置 */
  struct MCTSNode **children;
  int child_count;
  int child_cap;
  int visits;
  int wins;
  int *untried;           /* 未扩展的走法 */
  int untried_count;
} MCTSNode;

/* 创建节点 */
static MCTSNode *node_create(uint64 p, uint64 o, int player,
                             MCTSNode *parent, int move) {
  MCTSNode *n = (MCTSNode *)calloc(1, sizeof(MCTSNode));
  n->p = p;
  n->o = o;
  n->player = player;
  n->parent = parent;
  n->move = move;
  n->children = NULL;
  n->child_count = 0;
  n->child_cap = 0;
  n->visits = 0;
  n->wins = 0;

  /* 预计算合法走法 */
  uint64 legal = get_legal_moves(p, o);
  n->untried_count = popcount(legal);
  n->untried = (int *)malloc(n->untried_count * sizeof(int));
  int idx = 0;
  while (legal) {
    uint64 lsb = legal & -legal;
    n->untried[idx++] = bit_to_pos(lsb);
    legal ^= lsb;
  }
  return n;
}

/* 释放节点树 */
static void node_free(MCTSNode *n) {
  if (!n) return;
  for (int i = 0; i < n->child_count; i++)
    node_free(n->children[i]);
  free(n->children);
  free(n->untried);
  free(n);
}

/* 添加子节点 */
static void node_add_child(MCTSNode *parent, MCTSNode *child) {
  if (parent->child_count >= parent->child_cap) {
    parent->child_cap = parent->child_cap ? parent->child_cap * 2 : 4;
    parent->children = (MCTSNode **)realloc(
        parent->children, parent->child_cap * sizeof(MCTSNode *));
  }
  parent->children[parent->child_count++] = child;
}

/* UCB1 值 */
static double ucb1(MCTSNode *n) {
  if (n->visits == 0) return 1e300;
  return (double)n->wins / n->visits +
         UCB_C * sqrt(log((double)n->parent->visits) / n->visits);
}

/* 随机数工具 */
static inline int rand_int(int max) {
  return rand() % max;
}

static inline double rand_d(void) {
  return (double)rand() / RAND_MAX;
}

/* ================================================================== */
/*  置换表（用于模拟阶段缓存）                                        */
/* ================================================================== */

typedef struct {
  uint64 key;
  int visits;
  int wins;
} TTEntry;

static TTEntry tt[TT_SIZE];

static inline uint64 hash_state(uint64 p, uint64 o) {
  return p ^ (o << 1) ^ (o >> 1);
}

/* ================================================================== */
/*  随机模拟                                                          */
/* ================================================================== */

static int simulate(uint64 p, uint64 o, int player) {
  /* 置换表查缓存 */
  uint64 h = hash_state(p, o) % TT_SIZE;
  if (tt[h].key == (p ^ o) && tt[h].visits > 10) {
    return (tt[h].wins * 2 > tt[h].visits) ? 1 : 0;
  }

  uint64 cp = p, co = o;
  int cp_player = player;

  for (int step = 0; step < 100; step++) {
    if (is_game_over(cp_player == 1 ? cp : co,
                     cp_player == 1 ? co : cp))
      break;

    uint64 legal = get_legal_moves(cp, co);
    if (legal) {
      int moves[64];
      int n = 0;
      while (legal) {
        uint64 lsb = legal & -legal;
        moves[n++] = bit_to_pos(lsb);
        legal ^= lsb;
      }
      /* 30% 概率启发式选角 */
      int move;
      if (rand_d() < 0.3) {
        int best = 0;
        for (int i = 1; i < n; i++)
          if (POS_W[moves[i]] > POS_W[moves[best]]) best = i;
        move = moves[best];
      } else {
        move = moves[rand_int(n)];
      }
      do_move(&cp, &co, move);
      uint64 tmp = cp; cp = co; co = tmp;
    } else {
      uint64 tmp = cp; cp = co; co = tmp;
    }
  }

  int p_cnt = popcount(cp);
  int o_cnt = popcount(co);
  int result = (p_cnt > o_cnt) ? 1 : 0;

  /* 更新置换表 */
  if (tt[h].key == (p ^ o)) {
    tt[h].visits++;
    tt[h].wins += result;
  } else {
    tt[h].key = p ^ o;
    tt[h].visits = 1;
    tt[h].wins = result;
  }

  return result;
}

/* ================================================================== */
/*  MCTS 搜索核心                                                     */
/* ================================================================== */

static int mcts_search_bits(uint64 black, uint64 white,
                            int player, int iterations) {
  uint64 own = (player == 1) ? black : white;
  uint64 opp = (player == 1) ? white : black;
  MCTSNode *root = node_create(own, opp, player, NULL, -1);

  if (root->untried_count == 0) {
    node_free(root);
    return -1;
  }

  for (int i = 0; i < iterations; i++) {
    /* ---- Selection ---- */
    MCTSNode *node = root;
    while (node->untried_count == 0 && node->child_count > 0) {
      MCTSNode *best = node->children[0];
      double best_v = ucb1(best);
      for (int j = 1; j < node->child_count; j++) {
        double v = ucb1(node->children[j]);
        if (v > best_v) {
          best_v = v;
          best = node->children[j];
        }
      }
      node = best;
    }

    /* ---- Expansion ---- */
    if (node->untried_count > 0) {
      /* 启发式：选权重最高的 */
      int best_idx = 0;
      for (int j = 1; j < node->untried_count; j++)
        if (POS_W[node->untried[j]] > POS_W[node->untried[best_idx]])
          best_idx = j;
      int move = node->untried[best_idx];
      /* 移除已选走法 */
      node->untried[best_idx] = node->untried[node->untried_count - 1];
      node->untried_count--;

      uint64 np = node->p, no = node->o;
      do_move(&np, &no, move);
      MCTSNode *child = node_create(no, np, 3 - node->player,
                                    node, move);
      node_add_child(node, child);
      node = child;
    }

    /* ---- Simulation ---- */
    int result = simulate(node->p, node->o, node->player);

    /* ---- Backpropagation ---- */
    while (node) {
      node->visits++;
      node->wins += result;
      result = 1 - result;
      node = node->parent;
    }
  }

  /* 选访问次数最多的子节点 */
  MCTSNode *best = root->children[0];
  for (int i = 1; i < root->child_count; i++)
    if (root->children[i]->visits > best->visits)
      best = root->children[i];

  int move = best->move;
  node_free(root);
  return move;
}

/* ================================================================== */
/*  DLL 导出接口                                                      */
/* ================================================================== */

#ifdef _WIN32
  #define DLLEXPORT __declspec(dllexport)
#else
  #define DLLEXPORT __attribute__((visibility("default")))
#endif

/* 初始化随机种子 */
DLLEXPORT void mcts_init_seed(void) {
  srand((unsigned)time(NULL));
}

/*
 * MCTS 搜索接口
 * board: 8x8数组，0=空 1=黑 2=白
 * player: 1=黑 2=白
 * out_x, out_y: 输出坐标
 * iterations: MCTS迭代次数
 */
DLLEXPORT void mcts_search(int board[8][8], int player,
                           int *out_x, int *out_y, int iterations) {
  uint64 black, white;
  grid_to_bits(board, &black, &white);

  int move = mcts_search_bits(black, white, player, iterations);

  if (move < 0) {
    *out_x = -1;
    *out_y = -1;
  } else {
    *out_x = move / 8;
    *out_y = move % 8;
  }
}

/*
 * 获取合法走法列表
 * board: 8x8数组
 * player: 1=黑 2=白
 * out_moves: 输出数组，每个元素是 x*8+y，最多64个
 * 返回: 合法走法数量
 */
DLLEXPORT int get_legal_moves_list(int board[8][8], int player,
                                   int out_moves[64]) {
  uint64 black, white;
  grid_to_bits(board, &black, &white);
  uint64 own = (player == 1) ? black : white;
  uint64 opp = (player == 1) ? white : black;
  uint64 legal = get_legal_moves(own, opp);

  int n = 0;
  while (legal) {
    uint64 lsb = legal & -legal;
    out_moves[n++] = bit_to_pos(lsb);
    legal ^= lsb;
  }
  return n;
}

/*
 * 执行落子
 * board: 输入输出8x8数组
 * player: 1=黑 2=白
 * x, y: 落子坐标
 * 返回: 1=成功 0=失败
 */
DLLEXPORT int make_move_dll(int board[8][8], int player, int x, int y) {
  uint64 black, white;
  grid_to_bits(board, &black, &white);
  uint64 own = (player == 1) ? black : white;
  uint64 opp = (player == 1) ? white : black;

  int pos = x * 8 + y;
  if (get_flips(own, opp, pos) == 0) return 0;

  do_move(&own, &opp, pos);
  if (player == 1) {
    black = own; white = opp;
  } else {
    white = own; black = opp;
  }
  bits_to_grid(black, white, board);
  return 1;
}

/*
 * 检查游戏结束
 * board: 8x8数组
 * 返回: 1=结束 0=未结束
 */
DLLEXPORT int is_game_over_dll(int board[8][8]) {
  uint64 black, white;
  grid_to_bits(board, &black, &white);
  return is_game_over(black, white) ? 1 : 0;
}

/*
 * 获取分数
 * board: 8x8数组
 * out_black, out_white: 输出棋子数
 */
DLLEXPORT void get_score_dll(int board[8][8], int *out_black, int *out_white) {
  uint64 black, white;
  grid_to_bits(board, &black, &white);
  *out_black = popcount(black);
  *out_white = popcount(white);
}
