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
        "desc": "Standard Othello opening",
    },
    {
        "name": "Midgame1",
        "P": 0x0000000818280000,
        "O": 0x0000001000080000,
        "desc": "After 4 moves",
    },
    {
        "name": "Midgame2",
        "P": 0x0000102818040000,
        "O": 0x0000001008200000,
        "desc": "After ~10 moves",
    },
    {
        "name": "Midgame3",
        "P": 0x003844181C080000,
        "O": 0x0004002040100000,
        "desc": "After ~20 moves",
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
                  ) -> List[Dict]:
    results = []

    for eng_key, engine in engines.items():
        if debug_off:
            engine.set_debug(0)

        depths = depths_map.get(eng_key, ALL_DEPTHS)
        engine.init()

        for pos in positions:
            for depth in depths:
                result = {
                    "engine": eng_key,
                    "engine_name": engine.name,
                    "depth": depth,
                    "position": pos["name"],
                    "move": -1,
                    "score": 0,
                    "time_s": 0.0,
                    "timed_out": False,
                    "success": False,
                    "error": None,
                }

                try:
                    move, score, elapsed_s, timed_out = search_with_timeout(
                        engine, depth, pos["P"], pos["O"])
                    result["move"] = move
                    result["score"] = score
                    result["time_s"] = elapsed_s
                    result["timed_out"] = timed_out
                    result["success"] = True
                except Exception as e:
                    result["error"] = str(e)

                results.append(result)

                # Progress
                status = "TO" if result["timed_out"] else ("OK" if result["success"] else "FAIL")
                move_str = move_to_algebraic(result["move"])
                time_str = fmt_time(result["time_s"], result["timed_out"])
                print(
                    f"  {status:4s} [{eng_key:20s}] d={depth:2d} "
                    f"pos={pos['name']:10s} "
                    f"move={move_str:4s} score={score:+5d} "
                    f"time={time_str:>12s}"
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
    """Move agreement with v4_full (the reference)."""
    print("\n" + "=" * 100)
    print(" Move Agreement vs v4_full (reference)")
    print("=" * 100)

    from collections import defaultdict

    groups = defaultdict(dict)
    for r in results:
        if r["success"]:
            groups[(r["position"], r["depth"])][r["engine"]] = r["move"]

    eng_keys = ["v0_minimax", "v1_alphabeta", "v2_alphabeta_tt",
                 "v3_full_debug"]

    total = 0
    agreement = {ek: 0 for ek in eng_keys}

    print(f"\n{'Position':<10s} {'d':>2s}  {'v0':>5s} {'v1':>5s} {'v2':>5s} {'v3':>5s}")
    print("-" * 40)

    for (pos_name, depth), moves in sorted(groups.items()):
        ref_move = moves.get("v4_full")
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
    print(f"\nAgreement rate (same move as v4_full):")
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
    P = 0x0000000810000000
    O = 0x0000001008000000
    passed = False

    for move_num in range(64):
        black_to_move = (move_num % 2 == 0)
        engine = engine_black if black_to_move else engine_white
        depth = depth_black if black_to_move else depth_white

        moves = (_get_legal_moves_simple(P, O) if black_to_move
                 else _get_legal_moves_simple(O, P))

        if moves == 0:
            opponent_moves = (_get_legal_moves_simple(O, P) if black_to_move
                              else _get_legal_moves_simple(P, O))
            if opponent_moves == 0 or passed:
                break
            passed = True
            P, O = O, P
            continue
        passed = False

        try:
            if black_to_move:
                move, score = engine.get_best_move(depth, P, O)
            else:
                move, score = engine.get_best_move(depth, P, O)
        except Exception:
            break

        if move < 0 or move > 63:
            break

        newP, newO = _make_move_simple(P, O, move)
        P, O = newP, newO

        if verbose and move_num < 10:
            print(f"  move {move_num+1}: {move_to_algebraic(move)}")

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
    results = run_benchmark(engines, TEST_POSITIONS, depths_map, debug_off=True)

    # Print reports
    print_per_depth_speed_table(results)
    print_move_agreement(results)

    if args.compare:
        run_head_to_head(engines, games_per_match=args.games, depth=args.depth)

    # Summary stats
    total_ok = sum(1 for r in results if r["success"] and not r["timed_out"])
    total_to = sum(1 for r in results if r["timed_out"])
    total_fail = sum(1 for r in results if not r["success"])
    print(f"\nTotal: {len(results)} searches -- "
          f"{total_ok} OK, {total_to} timeout, {total_fail} failed")

    print("\n[OK] Benchmark complete!")


if __name__ == "__main__":
    main()
