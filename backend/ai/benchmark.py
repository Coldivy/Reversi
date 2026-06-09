#!/usr/bin/env python3
"""
benchmark.py -- 5 Othello engine benchmark

Models:
  1. v0_minimax      -- Pure Minimax (no Alpha-Beta)
  2. v1_alphabeta    -- Alpha-Beta pruning (no TT)
  3. v2_alphabeta_tt -- Alpha-Beta + Transposition Table
  4. v3_full_debug   -- Full engine with debug output
  5. v4_full         -- Full engine (original search.dll, no debug)

Usage:
  python benchmark.py              # Full benchmark
  python benchmark.py --quick      # Quick (lower depths)
  python benchmark.py --demo       # Demo mode with debug output
  python benchmark.py --compare    # Head-to-head matches
"""

import ctypes
import os
import sys
import time
import platform
from typing import Dict, List, Optional, Tuple

# =====================================================================
#  Test positions
# =====================================================================

TEST_POSITIONS = [
    {
        "name": "Opening",
        "P": 0x0000000810000000,
        "O": 0x0000001008000000,
        "desc": "Standard Othello opening (4 pieces)",
    },
    {
        "name": "EarlyMid",
        "P": 0x0000000818280000,
        "O": 0x0000001000080000,
        "desc": "Early midgame (~6 pieces)",
    },
    {
        "name": "Midgame1",
        "P": 0x0000102818040000,
        "O": 0x0000001008200000,
        "desc": "Midgame (~14 pieces)",
    },
    {
        "name": "Midgame2",
        "P": 0x003844181C080000,
        "O": 0x0004002040100000,
        "desc": "Midgame (~22 pieces)",
    },
    {
        "name": "LateMid",
        "P": 0x006014381C040000,
        "O": 0x0008002048120000,
        "desc": "Late midgame (~32 pieces)",
    },
    {
        "name": "Endgame",
        "P": 0x1C34143C04020000,
        "O": 0x027A223142120000,
        "desc": "Endgame (~45 pieces)",
    },
]

# =====================================================================
#  Depth configuration -- uniform across all engines
# =====================================================================

ALL_DEPTHS = list(range(1, 11))  # 1-10 for all engines
BENCH_DEPTHS = {
    "v0_minimax":      ALL_DEPTHS,
    "v1_alphabeta":    ALL_DEPTHS,
    "v2_alphabeta_tt": ALL_DEPTHS,
    "v3_full_debug":   ALL_DEPTHS,
    "v4_full":         list(range(1, 13)),  # v4 can go deeper
}

QUICK_DEPTHS = {
    "v0_minimax":      [2, 4, 6],
    "v1_alphabeta":    [2, 4, 6, 8],
    "v2_alphabeta_tt": [2, 4, 6, 8],
    "v3_full_debug":   [2, 4, 6, 8],
    "v4_full":         [2, 4, 6, 8, 10],
}

# Timeout per search call (seconds)
SEARCH_TIMEOUT_S = 10.0
SEARCH_TIMEOUT_MS = int(SEARCH_TIMEOUT_S * 1000)

# MCTS iteration levels (analogous to depths for Negamax engines)
MCTS_ITER_LEVELS = [100, 500, 1000, 5000, 10000, 20000, 50000, 100000]

# Repeats per test point for all engines
NEGAMAX_REPEATS = 10
MCTS_REPEATS = 10

# Timed head-to-head time limits
TIMED_COMPARE_MS = [100, 1000, 2000]

Board8x8 = (ctypes.c_int * 8) * 8


# =====================================================================
#  Engine wrapper
# =====================================================================

def _get_lib_dir():
    return os.path.dirname(os.path.abspath(__file__))


def _load_lib(name: str) -> ctypes.CDLL:
    system = platform.system()
    ext = ".dll" if system == "Windows" else ".so"
    path = os.path.join(_get_lib_dir(), name + ext)
    if not os.path.exists(path):
        raise FileNotFoundError(f"Library not found: {path}")

    lib = ctypes.CDLL(path)

    lib.c_init_search.argtypes = []
    lib.c_init_search.restype = None

    lib.c_get_best_move.argtypes = [
        ctypes.c_int, ctypes.c_uint64, ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_int),
    ]
    lib.c_get_best_move.restype = ctypes.c_int

    lib.c_get_best_move_timed.argtypes = [
        ctypes.c_int, ctypes.c_uint64, ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
    ]
    lib.c_get_best_move_timed.restype = ctypes.c_int

    return lib


class Engine:
    def __init__(self, name: str, lib_name: str,
                 debug_var_name: Optional[str] = None):
        self.name = name
        self.lib_name = lib_name
        self.lib = _load_lib(lib_name)
        self._debug_var = None
        self._set_time_limit_fn = None
        self._timeout_flag = None

        # Debug level control
        if debug_var_name:
            try:
                self._debug_var = ctypes.c_int.in_dll(self.lib, debug_var_name)
            except (ValueError, AttributeError):
                pass

        # c_set_time_limit function
        try:
            self._set_time_limit_fn = self.lib.c_set_time_limit
            self._set_time_limit_fn.argtypes = [ctypes.c_int]
            self._set_time_limit_fn.restype = None
        except AttributeError:
            pass

        # g_timeout_occurred flag
        try:
            self._timeout_flag = ctypes.c_int.in_dll(
                self.lib, "g_timeout_occurred")
        except (ValueError, AttributeError):
            pass

        self.init()

    def init(self):
        self.lib.c_init_search()

    def set_debug(self, level: int):
        if self._debug_var is not None:
            self._debug_var.value = level

    def set_time_limit(self, ms: int):
        if self._set_time_limit_fn is not None:
            self._set_time_limit_fn(ms)

    def get_timeout_flag(self) -> bool:
        if self._timeout_flag is not None:
            return bool(self._timeout_flag.value)
        return False

    def reset_timeout_flag(self):
        if self._timeout_flag is not None:
            self._timeout_flag.value = 0

    def get_best_move(self, depth: int, player_bb: int,
                      opponent_bb: int) -> Tuple[int, int]:
        best_move = ctypes.c_int(-1)
        score = self.lib.c_get_best_move(
            ctypes.c_int(depth),
            ctypes.c_uint64(player_bb),
            ctypes.c_uint64(opponent_bb),
            ctypes.byref(best_move),
        )
        return best_move.value, int(score)

    def get_best_move_timed(self, time_ms: int, player_bb: int,
                            opponent_bb: int) -> Tuple[int, int, int]:
        best_move = ctypes.c_int(-1)
        depth_searched = ctypes.c_int(0)
        score = self.lib.c_get_best_move_timed(
            ctypes.c_int(time_ms),
            ctypes.c_uint64(player_bb),
            ctypes.c_uint64(opponent_bb),
            ctypes.byref(best_move),
            ctypes.byref(depth_searched),
        )
        return best_move.value, int(score), depth_searched.value


# =====================================================================
#  MCTS Engine wrapper
# =====================================================================

class MCTSEngine:
    """Wraps search_mct.dll (MCTS engine) for benchmarking.

    Unlike the Negamax-based engines, MCTS:
      • Uses mcts_search(board[8][8], player, *x, *y, iterations, *win_rate_pct) instead of c_get_best_move
      • Is stochastic — same position may yield different moves
      • Uses 2D board arrays (0=empty, 1=black, 2=white) instead of bitboards
    """

    def __init__(self, lib_name: str = "search_mct"):
        system = platform.system()
        ext = ".dll" if system == "Windows" else ".so"
        lib_path = os.path.join(_get_lib_dir(), lib_name + ext)
        if not os.path.exists(lib_path):
            raise FileNotFoundError(f"MCTS library not found: {lib_path}")

        self.lib_name = lib_name
        self.lib = ctypes.CDLL(lib_path)

        # mcts_init_seed()
        self.lib.mcts_init_seed.argtypes = []
        self.lib.mcts_init_seed.restype = None

        # mcts_search(int board[8][8], int player, int *out_x, int *out_y, int iterations, int *out_win_rate_pct)
        self.lib.mcts_search.argtypes = [
            Board8x8,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
        ]
        self.lib.mcts_search.restype = None

        self.init()

    def init(self):
        self.lib.mcts_init_seed()

    def search(self, iterations: int, P: int, O: int) -> Tuple[int, int]:
        """Run MCTS search. P/O are bitboards as stored in TEST_POSITIONS.
        Returns (move_index, win_rate_pct).
        """
        board = self._bitboards_to_array(P, O)
        occupied = (P | O).bit_count()
        player = 1 if occupied % 2 == 0 else 2  # 1=black, 2=white

        out_x = ctypes.c_int(-1)
        out_y = ctypes.c_int(-1)
        win_rate = ctypes.c_int(0)

        self.lib.mcts_search(
            board,
            ctypes.c_int(player),
            ctypes.byref(out_x),
            ctypes.byref(out_y),
            ctypes.c_int(iterations),
            ctypes.byref(win_rate),
        )

        if out_x.value < 0:
            return -1, 0
        return out_x.value * 8 + out_y.value, win_rate.value

    def search_timed(self, time_limit_ms: int, P: int, O: int,
                     player: int = 0
                     ) -> Tuple[int, int, int]:
        """Timed MCTS search. Returns (move_index, win_rate_pct, iterations_done).

        player: 1=black, 2=white. 0 means auto-detect via board parity.
        """
        board = self._bitboards_to_array(P, O)
        if player == 0:
            occupied = (P | O).bit_count()
            player = 1 if occupied % 2 == 0 else 2

        out_x = ctypes.c_int(-1)
        out_y = ctypes.c_int(-1)
        win_rate = ctypes.c_int(0)
        iters_done = ctypes.c_int(0)

        self.lib.mcts_search_timed(
            board,
            ctypes.c_int(player),
            ctypes.byref(out_x),
            ctypes.byref(out_y),
            ctypes.c_int(time_limit_ms),
            ctypes.byref(win_rate),
            ctypes.byref(iters_done),
        )

        if out_x.value < 0:
            return -1, 0, iters_done.value
        return out_x.value * 8 + out_y.value, win_rate.value, iters_done.value

    @staticmethod
    def _bitboards_to_array(P: int, O: int) -> Board8x8:
        """Convert black/white bitboards to 8x8 array (0=empty, 1=black, 2=white)."""
        board = Board8x8()
        for r in range(8):
            row = board[r]
            for c in range(8):
                idx = r * 8 + c
                if (P >> idx) & 1:
                    row[c] = 1
                elif (O >> idx) & 1:
                    row[c] = 2
                else:
                    row[c] = 0
        return board


def create_all_engines() -> Dict[str, Engine]:
    engines = {}
    engines["v0_minimax"] = Engine(
        "Pure Minimax", "v0_minimax_debug", "g_v0_debug_level")
    engines["v1_alphabeta"] = Engine(
        "Alpha-Beta", "v1_alphabeta_debug", "g_v1_debug_level")
    engines["v2_alphabeta_tt"] = Engine(
        "Alpha-Beta+TT", "v2_alphabeta_tt_debug", "g_v2_debug_level")
    engines["v3_full_debug"] = Engine(
        "Full+Debug", "v3_full_debug", "g_v3_debug_level")
    engines["v4_full"] = Engine(
        "Full (original)", "search", None)
    return engines


# =====================================================================
#  Utility
# =====================================================================

def move_to_algebraic(move: int) -> str:
    if move < 0 or move > 63:
        return "??"
    return "ABCDEFGH"[move % 8] + "12345678"[move // 8]


def popcount(x: int) -> int:
    return x.bit_count()


def board_to_string(player_bb: int, opponent_bb: int) -> str:
    lines = ["  A B C D E F G H"]
    for r in range(8):
        row = [f"{r+1} "]
        for c in range(8):
            mask = 1 << (r * 8 + c)
            if player_bb & mask:
                row.append("X")
            elif opponent_bb & mask:
                row.append("O")
            else:
                row.append(".")
        lines.append(" ".join(row))
    return "\n".join(lines)


# =====================================================================
#  Benchmark core
# =====================================================================

def search_with_timeout(engine: Engine, depth: int, P: int, O: int,
                        timeout_ms: int = SEARCH_TIMEOUT_MS
                        ) -> Tuple[int, int, float, bool]:
    """Call engine.get_best_move with deadline.
    Returns (move, score, elapsed_seconds, timed_out).
    """
    has_timeout = engine._set_time_limit_fn is not None

    if has_timeout:
        engine.reset_timeout_flag()
        engine.set_time_limit(timeout_ms)

    t0 = time.perf_counter()
    try:
        move, score = engine.get_best_move(depth, P, O)
    finally:
        if has_timeout:
            engine.set_time_limit(0)

    t1 = time.perf_counter()
    elapsed_s = t1 - t0
    timed_out = engine.get_timeout_flag() if has_timeout else False

    # If elapsed >= timeout, treat as timeout even if C flag wasn't set
    if elapsed_s >= SEARCH_TIMEOUT_S * 0.99:
        timed_out = True

    return move, score, elapsed_s, timed_out


def fmt_time(elapsed_s: float, timed_out: bool) -> str:
    """Format elapsed time with appropriate precision.
    < 1ms  -> xxx.xxx us
    < 1s   -> xxx.xxx ms
    >= 1s  -> x.xxxxxx s
    timeout -> >=10.0s
    """
    if timed_out:
        return f">={SEARCH_TIMEOUT_S:.1f}s"
    if elapsed_s < 0.001:
        return f"{elapsed_s * 1_000_000:.1f}us"
    elif elapsed_s < 1.0:
        return f"{elapsed_s * 1000:.3f}ms"
    else:
        return f"{elapsed_s:.6f}s"


def run_benchmark(engines: Dict[str, Engine],
                  positions: List[Dict],
                  depths_map: Dict[str, List[int]],
                  debug_off: bool = True,
                  repeats: int = 1,
                  ) -> List[Dict]:
    """Benchmark Negamax engines.

    Each (position, depth) pair is run `repeats` times with engine.init()
    called before each position to clear TT between different boards.
    Results are aggregated: avg time, most common move, avg score.
    """
    results = []

    for eng_key, engine in engines.items():
        if debug_off:
            engine.set_debug(0)

        depths = depths_map.get(eng_key, ALL_DEPTHS)

        for pos in positions:
            for depth in depths:
                moves_seen: Dict[int, int] = {}
                scores = []
                times = []
                timed_outs = 0
                errors = 0

                for run_idx in range(repeats):
                    engine.init()   # clear TT between each run

                    try:
                        move, score, elapsed_s, timed_out = search_with_timeout(
                            engine, depth, pos["P"], pos["O"])
                        moves_seen[move] = moves_seen.get(move, 0) + 1
                        scores.append(score)
                        times.append(elapsed_s)
                        if timed_out:
                            timed_outs += 1
                    except Exception as e:
                        errors += 1
                        if run_idx == 0 and repeats > 1:
                            continue  # silent retry
                        break

                most_common_move = max(moves_seen, key=moves_seen.get) if moves_seen else -1
                avg_time = sum(times) / len(times) if times else 0.0
                avg_score = sum(scores) / len(scores) if scores else 0

                result = {
                    "engine": eng_key,
                    "engine_name": engine.name,
                    "depth": depth,
                    "position": pos["name"],
                    "move": most_common_move,
                    "score": int(round(avg_score)),
                    "time_s": avg_time,
                    "timed_out": timed_outs > repeats // 2,
                    "success": errors < repeats,
                    "error": None,
                }
                results.append(result)

                # Compact progress: show avg time & agreement
                status = "TO" if result["timed_out"] else ("OK" if result["success"] else "FAIL")
                move_str = move_to_algebraic(result["move"])
                time_str = fmt_time(avg_time, result["timed_out"])
                agr = f" {moves_seen[most_common_move]}/{repeats}" if repeats > 1 else ""
                print(
                    f"  {status:4s} [{eng_key:20s}] d={depth:2d} "
                    f"pos={pos['name']:10s} "
                    f"move={move_str:4s} score={avg_score:+5.0f} "
                    f"time={time_str:>12s}{agr}"
                )

    return results


# =====================================================================
#  Report generation
# =====================================================================

def print_summary_table(results: List[Dict]):
    """Per-depth comparison table: all engines at each depth."""
    print("\n" + "=" * 140)
    print(" Summary: Per-Depth Search Time & Move Comparison")
    print("=" * 140)

    from collections import defaultdict
    groups = defaultdict(dict)

    for r in results:
        groups[(r["position"], r["depth"])][r["engine"]] = r

    eng_keys = ["v0_minimax", "v1_alphabeta", "v2_alphabeta_tt",
                 "v3_full_debug", "v4_full"]
    eng_abbrev = {"v0_minimax": "v0", "v1_alphabeta": "v1",
                   "v2_alphabeta_tt": "v2", "v3_full_debug": "v3",
                   "v4_full": "v4"}

    # Header
    print(f"\n{'Pos':<10s} {'d':>2s}  ", end="")
    for ek in eng_keys:
        print(f"  {eng_abbrev[ek]:>28s}", end="")
    print()
    print("-" * 140)

    last_pos = None
    for (pos_name, depth), row in sorted(groups.items()):
        if pos_name != last_pos:
            pass  # don't add extra blank lines
        last_pos = pos_name

        cells = []
        for ek in eng_keys:
            r = row.get(ek)
            if r and r["success"]:
                move_s = move_to_algebraic(r["move"])
                time_s = fmt_time(r["time_s"], r["timed_out"])
                cells.append(
                    f"{time_s:>12s} {move_s:3s} {r['score']:+5d}")
            else:
                cells.append(f"{'--':>12s} {'--':3s} {'--':>5s}")

        print(f"{pos_name:<10s} {depth:2d}  " + "  ".join(cells))

    print("-" * 140)


def print_per_depth_speed_table(results: List[Dict]):
    """Average search time per engine at each depth (across all positions)."""
    print("\n" + "=" * 160)
    print(" Average Search Time per Depth (all positions)")
    print("=" * 160)

    from collections import defaultdict

    # Aggregate: (engine, depth) -> [time_s, ...]
    agg = defaultdict(list)
    for r in results:
        if r["success"] and not r["timed_out"]:
            agg[(r["engine"], r["depth"])].append(r["time_s"])
        elif r["timed_out"]:
            agg[(r["engine"], r["depth"])].append(SEARCH_TIMEOUT_S)

    eng_keys = ["v0_minimax", "v1_alphabeta", "v2_alphabeta_tt",
                 "v3_full_debug", "v4_full"]
    eng_abbrev = {"v0_minimax": "v0_minimax", "v1_alphabeta": "v1_alphabeta",
                   "v2_alphabeta_tt": "v2_alphabeta_tt",
                   "v3_full_debug": "v3_full", "v4_full": "v4_full"}
    all_depths = sorted(set(r["depth"] for r in results))

    # Header row
    header = f"{'Engine':<20s}"
    for d in all_depths:
        header += f"  {'d=' + str(d):>14s}"
    print(header)
    print("-" * 160)

    for ek in eng_keys:
        row_s = [f"{eng_abbrev.get(ek, ek):<20s}"]
        for d in all_depths:
            times = agg.get((ek, d), [])
            if not times:
                row_s.append(f"{'--':>16s}")
                continue
            # Count timeouts
            n_timeout = sum(1 for t in times if t >= SEARCH_TIMEOUT_S * 0.99)
            avg = sum(times) / len(times)
            if n_timeout == len(times):
                # All timed out
                row_s.append(f"{'>=' + str(int(SEARCH_TIMEOUT_S)) + 's':>16s}")
            elif n_timeout > 0:
                # Some timed out
                s = fmt_time(avg, False) + f" ({n_timeout}TO)"
                row_s.append(f"{s:>16s}")
            else:
                s = fmt_time(avg, False)
                row_s.append(f"{s:>16s}")
        print("".join(row_s))

    print("-" * 160)
    print(f"  (TO = timed out at >= {SEARCH_TIMEOUT_S:.0f}s per call)")


def print_move_agreement(results: List[Dict]):
    """Move agreement with v1_alphabeta (sound Alpha-Beta, no LMR degradation)."""
    print("\n" + "=" * 100)
    print(" Move Agreement vs v1_alphabeta (sound reference)")
    print("=" * 100)

    from collections import defaultdict

    groups = defaultdict(dict)
    for r in results:
        if r["success"]:
            groups[(r["position"], r["depth"])][r["engine"]] = r["move"]

    eng_keys = ["v0_minimax", "v2_alphabeta_tt",
                 "v3_full_debug", "v4_full"]

    total = 0
    agreement = {ek: 0 for ek in eng_keys}

    print(f"\n{'Position':<10s} {'d':>2s}  {'v0':>5s} {'v2':>5s} {'v3':>5s} {'v4':>5s}")
    print("-" * 40)

    for (pos_name, depth), moves in sorted(groups.items()):
        ref_move = moves.get("v1_alphabeta")
        if ref_move is None:
            continue
        total += 1
        row_s = [f"{pos_name:<10s} {depth:2d}"]
        for ek in eng_keys:
            m = moves.get(ek)
            if m is not None and m == ref_move:
                row_s.append("  OK ")
                agreement[ek] += 1
            else:
                row_s.append("  -- ")
        print("".join(row_s))

    print("-" * 40)
    print(f"\nAgreement rate (same move as v1_alphabeta, the sound Alpha-Beta):")
    for ek in eng_keys:
        rate = agreement[ek] / total * 100 if total > 0 else 0
        print(f"  {ek:20s}: {agreement[ek]}/{total} ({rate:.0f}%)")


# =====================================================================
#  Head-to-head matches
# =====================================================================

def _get_legal_moves_simple(P: int, O: int) -> int:
    mask = O & 0x7E7E7E7E7E7E7E7E
    moves = 0
    for shift, m in [
        (1, mask), (-1, mask), (8, O), (-8, O),
        (7, mask), (-9, mask), (9, mask), (-7, mask),
    ]:
        if shift > 0:
            t = (P << shift) & m
        else:
            t = (P >> (-shift)) & m
        for _ in range(5):
            if shift > 0:
                t |= (t << shift) & m
            else:
                t |= (t >> (-shift)) & m
        if shift > 0:
            moves |= (t << shift)
        else:
            moves |= (t >> (-shift))
    return moves & ~(P | O)


def _make_move_simple(P: int, O: int, move: int) -> Tuple[int, int]:
    m = 1 << move
    mask = O & 0x7E7E7E7E7E7E7E7E
    flipped = 0
    for shift, msk in [
        (1, mask), (-1, mask), (8, O), (-8, O),
        (7, mask), (-9, mask), (9, mask), (-7, mask),
    ]:
        if shift > 0:
            t = (m << shift) & msk
        else:
            t = (m >> (-shift)) & msk
        for _ in range(5):
            if shift > 0:
                t |= (t << shift) & msk
            else:
                t |= (t >> (-shift)) & msk
        if shift > 0 and ((t << shift) & P):
            flipped |= t
        elif shift < 0 and ((t >> (-shift)) & P):
            flipped |= t
    return P | m | flipped, O & ~flipped


def play_game(engine_black: Engine, engine_white: Engine,
              depth_black: int = 6, depth_white: int = 6,
              verbose: bool = False) -> Tuple[int, int, str]:
    """Play a fixed-depth game. Uses state-machine turn logic (matching
    frontend GameEngine.makeMove) so pass/skip is handled correctly.

    P = black bitboard, O = white bitboard (constant — never swapped).
    black_to_move = boolean state variable (no move-num parity).
    """
    P = 0x0000000810000000
    O = 0x0000001008000000
    black_to_move = True

    for _ in range(64):  # safety cap
        engine = engine_black if black_to_move else engine_white
        depth = depth_black if black_to_move else depth_white
        my_bb = P if black_to_move else O
        opp_bb = O if black_to_move else P

        moves = _get_legal_moves_simple(my_bb, opp_bb)

        if moves == 0:
            # Current player has no moves — check if opponent can move
            opp_moves = _get_legal_moves_simple(opp_bb, my_bb)
            if opp_moves == 0:
                # Neither can move → game over
                break
            # Opponent can move → skip current player's turn
            black_to_move = not black_to_move
            continue

        try:
            move, _score = engine.get_best_move(depth, my_bb, opp_bb)
        except Exception:
            break

        if move < 0 or move > 63:
            break

        # Apply move: _make_move_simple takes (moving_side, opponent)
        # and returns (new_moving_side, new_opponent)
        new_my, new_opp = _make_move_simple(my_bb, opp_bb, move)
        if black_to_move:
            P, O = new_my, new_opp
        else:
            O, P = new_my, new_opp   # my_bb was O (white), result is (new_O, new_P)

        if verbose:
            side = "B" if black_to_move else "W"
            print(f"  move: {side}->{move_to_algebraic(move)}")

        black_to_move = not black_to_move

    b_cnt = popcount(P)
    w_cnt = popcount(O)
    if b_cnt > w_cnt:
        result = f"Black wins {b_cnt}:{w_cnt}"
    elif w_cnt > b_cnt:
        result = f"White wins {b_cnt}:{w_cnt}"
    else:
        result = f"Draw {b_cnt}:{w_cnt}"
    return b_cnt, w_cnt, result


def run_head_to_head(engines: Dict[str, Engine],
                     games_per_match: int = 2,
                     depth: int = 6):
    print("\n" + "=" * 80)
    print(f" Head-to-Head ({games_per_match} games each, depth={depth})")
    print("=" * 80)

    eng_keys = ["v0_minimax", "v1_alphabeta", "v2_alphabeta_tt",
                 "v3_full_debug", "v4_full"]

    for ek, eng in engines.items():
        eng.set_debug(0)

    results = {}
    for i, ek1 in enumerate(eng_keys):
        for ek2 in eng_keys[i + 1:]:
            eng1, eng2 = engines[ek1], engines[ek2]
            wins1, wins2, draws = 0, 0, 0

            for g in range(games_per_match):
                eng1.init()
                eng2.init()

                if g % 2 == 0:
                    b_cnt, w_cnt, _ = play_game(eng1, eng2, depth, depth)
                    if b_cnt > w_cnt:
                        wins1 += 1
                    elif w_cnt > b_cnt:
                        wins2 += 1
                    else:
                        draws += 1
                else:
                    b_cnt, w_cnt, _ = play_game(eng2, eng1, depth, depth)
                    if b_cnt > w_cnt:
                        wins2 += 1
                    elif w_cnt > b_cnt:
                        wins1 += 1
                    else:
                        draws += 1

                print(f"  {ek1} vs {ek2} game {g+1}: "
                      f"({wins1}:{wins2}) draws={draws}")

            results[(ek1, ek2)] = (wins1, wins2, draws)

    print(f"\n{'Results':-^60}")
    for (ek1, ek2), (w1, w2, d) in results.items():
        print(f"  {ek1:20s} vs {ek2:20s}: {w1}:{w2} (draws: {d})")

    return results


# =====================================================================
#  Timed head-to-head (Negamax v4 vs MCTS)
# =====================================================================

def play_game_timed(engine_black,
                    engine_white,
                    time_black: int = 100,
                    time_white: int = 100,
                    black_is_mcts: bool = False,
                    white_is_mcts: bool = False,
                    verbose: bool = False
                    ) -> Tuple[int, int, str, int, int]:
    """Play a timed game. State-machine turn logic (matches frontend GameEngine).

    engine_black / engine_white can be either Engine (Negamax) or MCTSEngine.
    black_is_mcts / white_is_mcts tells which type each side is.

    Returns: (black_count, white_count, result_str, iters_black, iters_white)
    """
    P = 0x0000000810000000
    O = 0x0000001008000000
    black_to_move = True
    iters_black_total = 0
    iters_white_total = 0

    for _ in range(64):
        is_mcts = black_is_mcts if black_to_move else white_is_mcts
        time_limit = time_black if black_to_move else time_white
        my_bb = P if black_to_move else O
        opp_bb = O if black_to_move else P

        moves = _get_legal_moves_simple(my_bb, opp_bb)

        if moves == 0:
            opp_moves = _get_legal_moves_simple(opp_bb, my_bb)
            if opp_moves == 0:
                break
            black_to_move = not black_to_move
            continue

        try:
            if is_mcts:
                engine = engine_black if black_to_move else engine_white
                # MCTS needs true (black, white) for board encoding, not (mover, opp).
                # Use black_to_move state to determine player (not parity — parity
                # would be wrong after a skip-turn when the board hasn't changed).
                dll_player = 1 if black_to_move else 2
                move, _wr, iters = engine.search_timed(time_limit, P, O, dll_player)
                if black_to_move:
                    iters_black_total += iters
                else:
                    iters_white_total += iters
            else:
                engine = engine_black if black_to_move else engine_white
                move, _score, _depth = engine.get_best_move_timed(
                    time_limit, my_bb, opp_bb)
        except Exception:
            break

        if move < 0 or move > 63:
            break

        new_my, new_opp = _make_move_simple(my_bb, opp_bb, move)
        if black_to_move:
            P, O = new_my, new_opp
        else:
            O, P = new_my, new_opp

        if verbose:
            side = "B" if black_to_move else "W"
            eng = "MCTS" if is_mcts else "AB"
            print(f"  move: [{eng}|{side}] {move_to_algebraic(move)}")

        black_to_move = not black_to_move

    b_cnt = popcount(P)
    w_cnt = popcount(O)
    if b_cnt > w_cnt:
        result = f"Black wins {b_cnt}:{w_cnt}"
    elif w_cnt > b_cnt:
        result = f"White wins {b_cnt}:{w_cnt}"
    else:
        result = f"Draw {b_cnt}:{w_cnt}"
    return b_cnt, w_cnt, result, iters_black_total, iters_white_total


def run_timed_head_to_head(neg_engine: Engine, mcts_engine: MCTSEngine,
                           time_limits: List[int] = TIMED_COMPARE_MS,
                           games_per_match: int = 4):
    """Timed head-to-head: Negamax v4_full vs MCTS, at multiple time limits.

    Each time-limit plays `games_per_match` games, alternating colours.
    """
    print("\n" + "=" * 90)
    print(f" Timed Head-to-Head: Negamax v4_full vs MCTS")
    print(f" ({games_per_match} games per time limit, alternating colours)")
    print("=" * 90)
    print()
    print(f"{'Time/ms':>8s}  {'AB_Black':>12s}  {'MCTS_White':>12s}  "
          f"{'MCTS_Black':>12s}  {'AB_White':>12s}  "
          f"{'AB_Wins':>8s}  {'MCTS_Wins':>8s}  {'Draws':>6s}")
    print("-" * 90)

    all_results = []

    for tl in time_limits:
        ab_wins = 0   # Negamax wins (regardless of colour)
        mcts_wins = 0
        draws = 0

        for g in range(games_per_match):
            neg_engine.init()
            mcts_engine.init()

            if g % 2 == 0:
                # Negamax = Black, MCTS = White
                b_cnt, w_cnt, _, _, _ = play_game_timed(
                    neg_engine, mcts_engine, tl, tl,
                    black_is_mcts=False, white_is_mcts=True)
                if b_cnt > w_cnt:
                    ab_wins += 1
                elif w_cnt > b_cnt:
                    mcts_wins += 1
                else:
                    draws += 1
            else:
                # MCTS = Black, Negamax = White
                b_cnt, w_cnt, _, _, _ = play_game_timed(
                    mcts_engine, neg_engine, tl, tl,
                    black_is_mcts=True, white_is_mcts=False)
                if b_cnt > w_cnt:
                    mcts_wins += 1
                elif w_cnt > b_cnt:
                    ab_wins += 1
                else:
                    draws += 1

            print(f"  {tl:>6d}ms  game {g+1}: (AB={ab_wins}, MCTS={mcts_wins}, D={draws})")

        all_results.append({
            "time_limit_ms": tl,
            "negamax_wins": ab_wins,
            "mcts_wins": mcts_wins,
            "draws": draws,
            "games": games_per_match,
        })

    print("-" * 90)
    print(f"\n{'Results':-^60}")
    print(f"\n{'Time':>8s}  {'Negamax':>10s}  {'MCTS':>10s}  {'Draws':>6s}  {'Winner':>10s}")
    print("-" * 60)
    for r in all_results:
        winner = "Negamax" if r["negamax_wins"] > r["mcts_wins"] else \
                 "MCTS" if r["mcts_wins"] > r["negamax_wins"] else "Draw"
        print(f"  {r['time_limit_ms']:>4d}ms  "
              f"{r['negamax_wins']:>8d}/{r['games']}  "
              f"{r['mcts_wins']:>8d}/{r['games']}  "
              f"{r['draws']:>6d}  {winner:>10s}")

    return all_results


# =====================================================================
#  MCTS benchmark
# =====================================================================

def run_mcts_benchmark(engine: MCTSEngine,
                       positions: List[Dict],
                       iter_levels: List[int] = MCTS_ITER_LEVELS,
                       repeats: int = MCTS_REPEATS,
                       ) -> List[Dict]:
    """Benchmark MCTS at various iteration counts.

    Each (position, iteration) combination is run `repeats` times
    to measure stochastic consistency.
    """
    results = []

    for pos in positions:
        for iters in iter_levels:
            moves_seen: Dict[int, int] = {}
            times = []

            for run_idx in range(repeats):
                engine.init()
                t0 = time.perf_counter()
                move, score = engine.search(iters, pos["P"], pos["O"])
                t1 = time.perf_counter()
                elapsed_s = t1 - t0

                moves_seen[move] = moves_seen.get(move, 0) + 1
                times.append(elapsed_s)

                print(
                    f"  [mcts] iters={iters:6d} "
                    f"pos={pos['name']:10s} "
                    f"run={run_idx+1}/{repeats} "
                    f"move={move_to_algebraic(move):4s} "
                    f"time={fmt_time(elapsed_s, False)}"
                )

            # Determine most common move
            most_common_move = max(moves_seen, key=moves_seen.get)  # type: ignore[arg-type]
            agreement = moves_seen[most_common_move] / repeats

            results.append({
                "position": pos["name"],
                "iterations": iters,
                "move": most_common_move,
                "move_counts": dict(moves_seen),
                "agreement": agreement,
                "time_avg_s": sum(times) / len(times),
                "time_min_s": min(times),
                "time_max_s": max(times),
                "success": True,
            })

    return results


def print_mcts_time_table(mcts_results: List[Dict]):
    """Print MCTS search time per iteration level."""
    print("\n" + "=" * 120)
    print(" MCTS: Search Time per Iteration Count (averaged over positions + runs)")
    print("=" * 120)

    from collections import defaultdict
    agg: Dict[int, List[float]] = defaultdict(list)

    for r in mcts_results:
        agg[r["iterations"]].append(r["time_avg_s"])

    print(f"\n{'Iterations':>12s}  {'Avg Time':>12s}  {'Time/1k Iters':>16s}  Notes")
    print("-" * 80)

    for iters in sorted(agg):
        times = agg[iters]
        avg_s = sum(times) / len(times)
        per_1k = avg_s / (iters / 1000) if iters > 0 else 0
        note = ""
        if avg_s > 1.0:
            note = "  (slow — consider reducing)"
        elif avg_s > 0.1:
            note = "  (moderate)"
        print(f"  {iters:>8d}  "
              f"{fmt_time(avg_s, False):>12s}  "
              f"{fmt_time(per_1k, False):>16s}"
              f"{note}")

    print("-" * 80)
    print(f"  (averaged over {len(TEST_POSITIONS)} positions × {MCTS_REPEATS} runs each)")


def print_mcts_move_table(mcts_results: List[Dict]):
    """Print MCTS move choices at each iteration level."""
    print("\n" + "=" * 140)
    print(f" MCTS: Move Choices & Consistency (most common move over {MCTS_REPEATS} runs)")
    print("=" * 140)

    from collections import defaultdict
    by_pos = defaultdict(dict)
    for r in mcts_results:
        by_pos[r["position"]][r["iterations"]] = r

    all_iters = sorted(set(r["iterations"] for r in mcts_results))

    # Header
    header = f"\n{'Position':<10s}"
    for it in all_iters:
        header += f"  {'iters=' + str(it):>22s}"
    print(header)
    print("-" * 140)

    for pos_name in sorted(by_pos):
        row = f"{pos_name:<10s}"
        for it in all_iters:
            r = by_pos[pos_name].get(it)
            if r:
                move_s = move_to_algebraic(r["move"])
                agr = f"{r['agreement']:.0%}"
                time_s = fmt_time(r["time_avg_s"], False)
                row += f"  {move_s:>4s} {time_s:>10s} {agr:>4s}"
            else:
                row += f"  {'--':>22s}"
        print(row)

    print("-" * 140)
    print(f"  Format: move time agreement%")
    print(f"  agreement = portion of {MCTS_REPEATS} runs that chose the most-common move")


def print_mcts_vs_negamax(mcts_results: List[Dict], negamax_results: List[Dict]):
    """Compare MCTS move choices to Negamax v1 (sound Alpha-Beta) as reference."""
    print("\n" + "=" * 120)
    print(" MCTS vs Negamax v1_alphabeta Move Comparison")
    print("=" * 120)

    # Build lookup: (position, depth) -> v1 move for negamax results
    v1_moves: Dict[str, int] = {}
    for r in negamax_results:
        if r["engine"] == "v1_alphabeta" and r["success"]:
            v1_moves[(r["position"], r["depth"])] = r["move"]

    from collections import defaultdict
    by_pos = defaultdict(dict)
    for r in mcts_results:
        by_pos[r["position"]][r["iterations"]] = r

    all_iters = sorted(set(r["iterations"] for r in mcts_results))

    # Use v1's d=8 results as baseline comparison (sound Alpha-Beta)
    compare_depth = 8

    print(f"\n  Comparing MCTS moves to Negamax v1 (d={compare_depth})")
    print()
    header = f"{'Position':<10s}  {'v1_d8':>6s}"
    for it in all_iters:
        header += f"  {'iters='+str(it):>18s}"
    print(header)
    print("-" * 120)

    match_counts = defaultdict(int)
    total_tests = 0

    for pos_name in sorted(by_pos):
        v1_move = v1_moves.get((pos_name, compare_depth), -1)
        v1_str = move_to_algebraic(v1_move) if v1_move >= 0 else "??"
        row = f"{pos_name:<10s}  {v1_str:>6s}"
        for it in all_iters:
            r = by_pos[pos_name].get(it)
            if r:
                match = "✓" if r["move"] == v1_move else "—"
                row += f"  {move_to_algebraic(r['move']):>4s} {match:>4s} {r['agreement']:.0%}"
                if r["move"] == v1_move:
                    match_counts[it] = match_counts.get(it, 0) + 1
                total_tests += 0  # noop
            else:
                row += f"  {'--':>18s}"
        print(row)

    # Per iteration summary
    print()
    for it in all_iters:
        count = match_counts.get(it, 0)
        pct = count / len(TEST_POSITIONS) * 100
        print(f"  iters={it:>6d}: matched v1_d8 in {count}/{len(TEST_POSITIONS)} "
              f"positions ({pct:.0f}%)")


# =====================================================================
#  Timed depth benchmark — how many depths can each engine reach in a
#  given time budget?  (Used for §7.4 comparison table.)
# =====================================================================

def run_timed_depth_benchmark(engines: Dict[str, Engine],
                              positions: List[Dict],
                              time_limits_ms: List[int] = [100, 1000, 2000],
                              runs_per_limit: int = 30,
                              ) -> Dict[str, Dict[int, float]]:
    """Measure average depth reached by v1 and v4 under timed search.

    Each (engine, time_limit) pair is tested `runs_per_limit` times,
    cycling through `positions`. Calls c_get_best_move_timed and records
    the returned depth_reached value.

    Returns: {engine_key: {time_limit_ms: avg_depth}}
    """
    print("\n" + "=" * 80)
    print(" Timed Depth Benchmark")
    print("=" * 80)
    print(f"  {runs_per_limit} runs per time limit, cycling {len(positions)} positions\n")

    results: Dict[str, Dict[int, float]] = {}

    for eng_key in ["v1_alphabeta", "v4_full"]:
        if eng_key not in engines:
            continue
        engine = engines[eng_key]
        results[eng_key] = {}
        engine.set_debug(0)

        print(f"  --- {engine.name} ---")
        for tl in time_limits_ms:
            depths = []
            for run in range(runs_per_limit):
                engine.init()
                pos = positions[run % len(positions)]
                _, _, depth = engine.get_best_move_timed(tl, pos["P"], pos["O"])
                depths.append(depth)
            avg_d = sum(depths) / len(depths)
            results[eng_key][tl] = avg_d
            print(f"    {tl:>4d}ms: avg depth = {avg_d:.1f}  "
                  f"(min={min(depths)}, max={max(depths)})")
        print()

    return results


# =====================================================================
#  Demo mode
# =====================================================================

def run_demo():
    print("=" * 60)
    print(" Demo Mode: Debug Output from Each Engine")
    print("=" * 60)

    pos = TEST_POSITIONS[0]
    print(f"\nPosition: {pos['name']}")
    print(board_to_string(pos["P"], pos["O"]))

    engines_config = [
        ("v0_minimax", "v0_minimax_debug", "g_v0_debug_level", 3),
        ("v1_alphabeta", "v1_alphabeta_debug", "g_v1_debug_level", 5),
        ("v2_alphabeta_tt", "v2_alphabeta_tt_debug", "g_v2_debug_level", 5),
        ("v3_full_debug", "v3_full_debug", "g_v3_debug_level", 5),
    ]

    for name, lib_name, debug_var, depth in engines_config:
        print(f"\n{'-' * 60}")
        print(f" Engine: {name} (depth={depth})")
        print(f"{'-' * 60}")
        sys.stderr.flush()
        sys.stdout.flush()

        eng = Engine(name, lib_name, debug_var)
        eng.set_debug(1)
        eng.init()

        try:
            move, score = eng.get_best_move(depth, pos["P"], pos["O"])
            print(f"\n  --> Best move: {move_to_algebraic(move)}, score={score}")
        except Exception as e:
            print(f"  Error: {e}")

        sys.stderr.flush()
        sys.stdout.flush()


# =====================================================================
#  Main
# =====================================================================

def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="Othello Engine Benchmark -- 5 models")
    parser.add_argument("--quick", action="store_true",
                        help="Quick test mode (fewer depths)")
    parser.add_argument("--demo", action="store_true",
                        help="Demo mode -- show debug output")
    parser.add_argument("--compare", action="store_true",
                        help="Include head-to-head matches")
    parser.add_argument("--depth", type=int, default=6,
                        help="Match search depth (default 6)")
    parser.add_argument("--games", type=int, default=2,
                        help="Games per matchup (default 2)")
    parser.add_argument("--mcts", action="store_true",
                        help="Include MCTS engine benchmark")
    parser.add_argument("--mcts-only", action="store_true",
                        help="Run only MCTS benchmark (skip Negamax engines)")
    parser.add_argument("--timed-compare", action="store_true",
                        help="Timed head-to-head: Negamax v4 vs MCTS at multiple time limits")
    parser.add_argument("--timed-games", type=int, default=10,
                        help="Games per time limit in timed compare (default 10)")
    parser.add_argument("--timed-depth", action="store_true",
                        help="Measure avg depth reached per engine at each time limit")
    args = parser.parse_args()

    print("=" * 60)
    print(" Othello Engine Benchmark -- 5 Models")
    print("=" * 60)
    print(f"\nTimeout per search: {SEARCH_TIMEOUT_S:.0f}s")
    print(f"\nModels:")
    print("  1. v0_minimax      -- Pure Minimax (no Alpha-Beta)")
    print("  2. v1_alphabeta    -- Alpha-Beta pruning (no TT)")
    print("  3. v2_alphabeta_tt -- Alpha-Beta + Transposition Table")
    print("  4. v3_full_debug   -- Full engine + Debug output")
    print("  5. v4_full         -- Full engine (original search.dll)")
    print()

    if args.demo:
        run_demo()
        return

    # ── Negamax benchmark ──
    engines: Dict[str, Engine] = {}
    results: List[Dict] = []
    # Skip full bench if only timed-depth is requested
    skip_full_bench = args.timed_depth and not (args.mcts or args.mcts_only)
    if not args.mcts_only and not skip_full_bench:
        print("Loading engines...")
        engines = create_all_engines()
        for ek, eng in engines.items():
            has_to = eng._set_time_limit_fn is not None
            has_dbg = eng._debug_var is not None
            print(f"  [OK] {eng.name} (timeout={has_to}, debug={has_dbg})")

        depths_map = QUICK_DEPTHS if args.quick else BENCH_DEPTHS
        mode = "Quick" if args.quick else "Full"
        print(f"\nMode: {mode} test")

        print(f"\nTest positions: {len(TEST_POSITIONS)}")
        for pos in TEST_POSITIONS:
            print(f"  - {pos['name']}: {pos['desc']}")

        print(f"\nStarting benchmark (timeout={SEARCH_TIMEOUT_S:.0f}s per call)...\n")
        results = run_benchmark(engines, TEST_POSITIONS, depths_map, debug_off=True, repeats=NEGAMAX_REPEATS)

        # Print reports
        print_per_depth_speed_table(results)
        print_move_agreement(results)

        if args.compare:
            run_head_to_head(engines, games_per_match=args.games, depth=args.depth)

    # ── Timed depth benchmark ──
    if args.timed_depth:
        # Only load v1 + v4; skip the full bench
        if skip_full_bench:
            engines = create_all_engines()
        if engines:
            run_timed_depth_benchmark(engines, TEST_POSITIONS)

    # ── MCTS benchmark ──
    mcts_results = None
    mcts_engine = None
    if args.mcts or args.mcts_only:
        print("\n" + "=" * 60)
        print(" MCTS Engine Benchmark")
        print("=" * 60)
        print(f"\nIteration levels: {MCTS_ITER_LEVELS}")
        print(f"Repeats per test: {MCTS_REPEATS}")
        print(f"Test positions: {len(TEST_POSITIONS)}")

        try:
            mcts_engine = MCTSEngine("search_mct")
            print(f"  [OK] MCTS engine loaded ({mcts_engine.lib_name})\n")
        except FileNotFoundError as e:
            print(f"  [FAIL] {e}\n  Skipping MCTS benchmark.\n")
            mcts_engine = None

        if mcts_engine:
            mcts_results = run_mcts_benchmark(
                mcts_engine, TEST_POSITIONS, MCTS_ITER_LEVELS, MCTS_REPEATS)
            print_mcts_time_table(mcts_results)
            print_mcts_move_table(mcts_results)
            if not args.mcts_only and results:
                print_mcts_vs_negamax(mcts_results, results)

    # Summary stats
    if results:
        total_ok = sum(1 for r in results if r["success"] and not r["timed_out"])
        total_to = sum(1 for r in results if r["timed_out"])
        total_fail = sum(1 for r in results if not r["success"])
        print(f"\nTotal (Negamax): {len(results)} searches -- "
              f"{total_ok} OK, {total_to} timeout, {total_fail} failed")

    if mcts_results:
        mcts_ok = sum(1 for r in mcts_results if r["success"])
        print(f"Total (MCTS): {len(mcts_results)} searches -- {mcts_ok} OK")

    # ── Timed Head-to-Head ──
    if args.timed_compare:
        if "v4_full" not in engines:
            engines = create_all_engines()
        if not mcts_engine:
            try:
                mcts_engine = MCTSEngine("search_mct")
                print(f"  [OK] MCTS engine loaded for timed compare\n")
            except FileNotFoundError as e:
                print(f"  [FAIL] {e}\n")
                mcts_engine = None

        if mcts_engine and "v4_full" in engines:
            run_timed_head_to_head(
                engines["v4_full"], mcts_engine,
                time_limits=TIMED_COMPARE_MS,
                games_per_match=args.timed_games)

    print("\n[OK] Benchmark complete!")


if __name__ == "__main__":
    main()
