#!/usr/bin/env python3
"""
benchmark.py — 黑白棋 AI 搜索引擎对比测试工具
================================================

用法:
    python benchmark.py                          # 对比 search.dll vs search_new.dll
    python benchmark.py a.dll b.dll c.dll        # 对比任意多个 .dll
    python benchmark.py --depth 10               # 指定最大搜索深度
    python benchmark.py --repeat 3               # 每项重复次数（取平均值）

输出:
    - 每种局面下的搜索时间、节点数、TT命中率、Beta截断率等
    - 多 DLL 横向对比表
"""

import ctypes
import os
import sys
import time
import argparse
from typing import Optional, Tuple, List, Dict, Any

# 强制 UTF-8 输出（Windows GBK 终端兼容）
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


# ======================================================================
#  测试局面定义  (8×8 网格 → bitboard)
# ======================================================================

def grid_to_bb(grid: List[List[int]]) -> Tuple[int, int]:
    """将 8x8 数组转为 (black_bb, white_bb)。 1=黑, -1=白, 0=空。"""
    b, w = 0, 0
    for r in range(8):
        for c in range(8):
            idx = r * 8 + c
            if grid[r][c] == 1:
                b |= (1 << idx)
            elif grid[r][c] == -1:
                w |= (1 << idx)
    return b, w


# ── 局面 0：标准开局 ──
INITIAL = [
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, -1, 1, 0, 0, 0],
    [0, 0, 0, 1, -1, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
]

# ── 局面 1：早期（约 8 手后）──
EARLY = [
    [0,  0,  0,  0,  0,  0,  0,  0],
    [0,  0,  0,  0,  0,  0,  0,  0],
    [0,  0,  0,  0,  1,  0,  0,  0],
    [0,  0,  0, -1, -1, -1,  0,  0],
    [0,  0,  0,  1, -1, -1,  0,  0],
    [0,  0,  0,  0,  1,  0,  0,  0],
    [0,  0,  0,  0,  0,  0,  0,  0],
    [0,  0,  0,  0,  0,  0,  0,  0],
]

# ── 局面 2：中盘（约 30 手后）──
MIDGAME = [
    [0,  0,  0,  0,  0,  0,  0,  0],
    [0,  1,  1,  1,  1,  0,  0,  0],
    [0,  1, -1, -1, -1,  1,  0,  0],
    [0,  1, -1, -1, -1,  1,  0,  0],
    [0,  1, -1, -1,  1,  1,  0,  0],
    [0,  0,  1, -1,  1,  0,  0,  0],
    [0,  0,  0,  1,  0,  0,  0,  0],
    [0,  0,  0,  0,  0,  0,  0,  0],
]

# ── 局面 3：后半盘（约 40 手后，空格 ≈ 24，空步裁剪有效果）──
#  0 = 空, 1 = 黑, -1 = 白  (黑先)
LATE_MID = [
    [ 0,  0,  0,  0,  0,  0,  0,  0],
    [ 0,  1,  1,  1,  1,  1,  0,  0],
    [ 0,  1, -1, -1, -1,  1,  0,  0],
    [ 0,  1, -1, -1, -1, -1,  1,  0],
    [ 0,  1, -1, -1, -1, -1,  1,  0],
    [ 0,  0,  1, -1, -1, -1,  0,  0],
    [ 0,  0,  0,  1,  0,  0,  0,  0],
    [ 0,  0,  0,  0,  0,  0,  0,  0],
]

# ── 局面 4：近终局（约 52 手后，空格 = 14，接近 solve 阈值）──
ENDGAME = [
    [-1, -1, -1, -1, -1, -1, -1, -1],
    [-1, -1, -1,  1, -1, -1, -1, -1],
    [-1, -1, -1, -1, -1, -1, -1, -1],
    [-1, -1, -1, -1, -1, -1, -1, -1],
    [-1, -1, -1, -1, -1, -1, -1, -1],
    [-1, -1,  1,  0,  0, -1, -1, -1],
    [ 0,  1, -1, -1,  0,  0, -1, -1],
    [ 0,  0,  0,  1,  0,  0,  0, -1],
]

# ══════════════════════════════════════════════════════════════════════
#  局面汇总
# ══════════════════════════════════════════════════════════════════════

POSITIONS = [
    ("开局 (4子)",     *grid_to_bb(INITIAL),  10),
    ("早期 (8手)",     *grid_to_bb(EARLY),     10),
    ("中盘 (30手)",    *grid_to_bb(MIDGAME),    8),
    ("后半盘 (44手)",  *grid_to_bb(LATE_MID),   6),
    ("近终局 (52手)",  *grid_to_bb(ENDGAME),    4),
]
# (名称, player_bb, opponent_bb, 默认深度)


# ======================================================================
#  DLL 加载器
# ======================================================================

class SearchLib:
    """封装一个 search*.dll，自动探测是否有调试导出函数。"""

    def __init__(self, path: str):
        self.name = os.path.basename(path)
        self.lib = ctypes.CDLL(path)

        # ── 标准接口（所有 DLL 都有）──
        self.lib.c_init_search.argtypes = []
        self.lib.c_init_search.restype = None

        self.lib.c_get_best_move.argtypes = [
            ctypes.c_int,                # depth
            ctypes.c_uint64,             # player_bb
            ctypes.c_uint64,             # opponent_bb
            ctypes.POINTER(ctypes.c_int) # *move
        ]
        self.lib.c_get_best_move.restype = ctypes.c_int

        # ── 调试接口（可选）──
        self.has_debug = self._probe_debug()

    def _probe_debug(self) -> bool:
        """探测是否存在 c_get_stats / c_reset_stats。"""
        try:
            # DebugStats 结构体布局 (10 个 long long)
            # nodes, tt_hits, beta_cuts, null_cuts, futility_skips,
            # lmr_attempts, lmr_researches, iid_searches, eval_calls, tt_stores
            self.lib.c_get_stats.argtypes = []
            self.lib.c_get_stats.restype = ctypes.c_void_p

            self.lib.c_reset_stats.argtypes = []
            self.lib.c_reset_stats.restype = None
            return True
        except AttributeError:
            return False

    def init(self):
        self.lib.c_init_search()

    def search(self, depth: int, p_bb: int, o_bb: int) -> Tuple[Optional[int], int]:
        """返回 (move, score)。"""
        move = ctypes.c_int(-1)
        score = self.lib.c_get_best_move(
            depth,
            ctypes.c_uint64(p_bb),
            ctypes.c_uint64(o_bb),
            ctypes.byref(move)
        )
        m = move.value
        return (m if m != -1 else None), int(score)

    def reset_stats(self):
        if self.has_debug:
            self.lib.c_reset_stats()

    def get_stats(self) -> Optional[Dict[str, int]]:
        """读取调试计数器。无调试接口时返回 None。"""
        if not self.has_debug:
            return None

        ptr = self.lib.c_get_stats()
        if not ptr:
            return None

        # 按结构体字段顺序读取 10 个 long long
        fields = [
            "nodes", "tt_hits", "beta_cuts", "null_cuts",
            "futility_skips", "lmr_attempts", "lmr_researches",
            "iid_searches", "eval_calls", "tt_stores",
        ]
        out = {}
        for i, key in enumerate(fields):
            out[key] = ctypes.cast(
                ptr + i * 8, ctypes.POINTER(ctypes.c_longlong)
            ).contents.value
        return out


# ======================================================================
#  格式化工具
# ======================================================================

def idx_to_rc(idx: int) -> str:
    """0-63 → "A1".."H8"。"""
    col = "ABCDEFGH"[idx % 8]
    row = idx // 8 + 1
    return f"{col}{row}"


def fmt_time(seconds: float) -> str:
    if seconds < 0.001:
        return f"{seconds*1_000_000:.0f}μs"
    if seconds < 1.0:
        return f"{seconds*1000:.1f}ms"
    return f"{seconds:.2f}s"


def fmt_rate(num: int, denom: int) -> str:
    if denom == 0:
        return "  -   "
    return f"{num/denom*100:5.1f}%"


# ======================================================================
#  核心 benchmark
# ======================================================================

def run_benchmark(libs: List[SearchLib],
                  positions: List[Tuple[str, int, int, int]],
                  repeat: int = 1) -> List[Dict[str, Any]]:
    """对所有 lib × 所有局面 × 各深度 执行搜索，采集数据。"""

    results: List[Dict[str, Any]] = []

    for pos_name, p_bb, o_bb, max_depth in positions:
        occupied = (p_bb | o_bb).bit_count()
        empty = 64 - occupied

        # 这个局面的有效搜索深度（不超过 max_depth）
        depths = []
        if empty <= 12:
            # 终局只测 solve（自动全深）
            depths = [0]  # 0 表示不设深度限制，由 solve 自动完成
        else:
            depths = [d for d in [4, 6, 8, 10, 12, 14] if d <= max_depth]

        for depth in depths:
            for lib in libs:
                # 多次重复取平均
                times = []
                scores = []
                moves = []
                stats_list = []

                for _ in range(repeat):
                    lib.init()
                    lib.reset_stats()

                    t0 = time.perf_counter()
                    move, score = lib.search(depth, p_bb, o_bb)
                    elapsed = time.perf_counter() - t0

                    times.append(elapsed)
                    scores.append(score)
                    moves.append(move)
                    stats_list.append(lib.get_stats())

                avg_time = sum(times) / len(times)
                # 所有重复应该返回相同结果
                final_move = moves[0]
                final_score = scores[0]

                # 聚合调试统计
                agg_stats = None
                if stats_list[0] is not None:
                    agg_stats = {}
                    for key in stats_list[0]:
                        agg_stats[key] = int(sum(s[key] for s in stats_list) / len(stats_list))

                results.append({
                    "position": pos_name,
                    "depth": depth,
                    "lib": lib.name,
                    "time": avg_time,
                    "move": final_move,
                    "score": final_score,
                    "stats": agg_stats,
                    "empty": empty,
                })

    return results


# ======================================================================
#  输出
# ======================================================================

def print_results(results: List[Dict[str, Any]]):
    """打印表格化结果。"""

    # 按局面分组
    from collections import defaultdict
    groups = defaultdict(list)
    for r in results:
        groups[r["position"]].append(r)

    lib_names = sorted(set(r["lib"] for r in results))
    has_debug = any(r["stats"] is not None for r in results)

    # 列宽
    COL_W = 10

    print()
    print("=" * 110)
    print("  黑白棋 AI 搜索引擎 Benchmark")
    print("=" * 110)

    for pos_name, entries in groups.items():
        # 按 depth 分组
        by_depth = defaultdict(list)
        for e in entries:
            by_depth[e["depth"]].append(e)

        print(f"\n{'─' * 110}")
        print(f"  【{pos_name}】  空格数: {entries[0]['empty']}")
        print(f"{'─' * 110}")

        for depth, row in sorted(by_depth.items()):
            if depth == 0:
                label = "solve"
            else:
                label = f"depth={depth}"

            print(f"\n  ▸ {label}")

            # 表头
            header = f"  {'指标':<20s}"
            for lib_name in lib_names:
                header += f" {lib_name:>{COL_W}s}"
            print(header)
            print(f"  {'-' * (20 + len(lib_names) * (COL_W + 1))}")

            # 按库顺序提取数据
            lib_data = {}
            for e in row:
                lib_data[e["lib"]] = e

            def vals():
                for ln in lib_names:
                    yield lib_data.get(ln)

            # ── 耗时 ──
            line = f"  {'耗时':<18s}"
            for v in vals():
                if v:
                    line += f" {fmt_time(v['time']):>{COL_W}s}"
                else:
                    line += f" {'-':>{COL_W}s}"
            print(line)

            # ── 落子 ──
            line = f"  {'落子':<18s}"
            for v in vals():
                if v and v["move"] is not None:
                    line += f" {idx_to_rc(v['move']):>{COL_W}s}"
                elif v:
                    line += f" {'PASS':>{COL_W}s}"
                else:
                    line += f" {'-':>{COL_W}s}"
            print(line)

            # ── 估分 ──
            line = f"  {'估分':<18s}"
            for v in vals():
                if v:
                    line += f" {v['score']:>{COL_W}d}"
                else:
                    line += f" {'-':>{COL_W}s}"
            print(line)

            # ── 调试指标 ──
            if has_debug:
                print()  # 空行分隔

                # 节点数
                line = f"  {'节点数':<18s}"
                for v in vals():
                    if v and v["stats"]:
                        n = v["stats"]["nodes"]
                        if n >= 1_000_000:
                            line += f" {n/1_000_000:.1f}M{'':>{COL_W-5}s}"
                        elif n >= 1_000:
                            line += f" {n/1_000:.1f}K{'':>{COL_W-5}s}"
                        else:
                            line += f" {n:>{COL_W}d}"
                    else:
                        line += f" {'-':>{COL_W}s}"
                print(line)

                # 节点速度
                line = f"  {'节点/秒':<18s}"
                for v in vals():
                    if v and v["stats"] and v["time"] > 0:
                        nps = v["stats"]["nodes"] / v["time"]
                        if nps >= 1_000_000:
                            line += f" {nps/1_000_000:.1f}M/s{'':>{COL_W-6}s}"
                        elif nps >= 1_000:
                            line += f" {nps/1_000:.0f}K/s{'':>{COL_W-6}s}"
                        else:
                            line += f" {nps:>{COL_W}.0f}"
                    else:
                        line += f" {'-':>{COL_W}s}"
                print(line)

                # TT 命中率
                line = f"  {'TT命中率':<18s}"
                for v in vals():
                    if v and v["stats"]:
                        line += f" {fmt_rate(v['stats']['tt_hits'], v['stats']['nodes']):>{COL_W}s}"
                    else:
                        line += f" {'-':>{COL_W}s}"
                print(line)

                # Beta 截断率
                line = f"  {'Beta截断率':<18s}"
                for v in vals():
                    if v and v["stats"]:
                        line += f" {fmt_rate(v['stats']['beta_cuts'], v['stats']['nodes']):>{COL_W}s}"
                    else:
                        line += f" {'-':>{COL_W}s}"
                print(line)

                # 空步裁剪率
                line = f"  {'空步裁剪':<18s}"
                for v in vals():
                    if v and v["stats"]:
                        line += f" {v['stats']['null_cuts']:>{COL_W}d}"
                    else:
                        line += f" {'-':>{COL_W}s}"
                print(line)

                # 静态剪枝跳过数
                line = f"  {'静态剪枝':<18s}"
                for v in vals():
                    if v and v["stats"]:
                        line += f" {v['stats']['futility_skips']:>{COL_W}d}"
                    else:
                        line += f" {'-':>{COL_W}s}"
                print(line)

                # LMR：尝试 vs 重搜
                line = f"  {'LMR尝试/重搜':<18s}"
                for v in vals():
                    if v and v["stats"]:
                        a = v["stats"]["lmr_attempts"]
                        r = v["stats"]["lmr_researches"]
                        line += f" {a}/{r}{'':>{COL_W-len(str(a))-len(str(r))-1}s}"
                    else:
                        line += f" {'-':>{COL_W}s}"
                print(line)

                # IID 触发次数
                line = f"  {'IID触发':<18s}"
                for v in vals():
                    if v and v["stats"]:
                        line += f" {v['stats']['iid_searches']:>{COL_W}d}"
                    else:
                        line += f" {'-':>{COL_W}s}"
                print(line)

                # 叶子评估数
                line = f"  {'叶子评估':<18s}"
                for v in vals():
                    if v and v["stats"]:
                        line += f" {v['stats']['eval_calls']:>{COL_W}d}"
                    else:
                        line += f" {'-':>{COL_W}s}"
                print(line)

    # ══════════════════════════════════
    #  总结
    # ══════════════════════════════════
    print(f"\n{'═' * 110}")
    print(f"  总结")
    print(f"{'═' * 110}")

    # 按库汇总平均耗时
    lib_times = defaultdict(list)
    for r in results:
        lib_times[r["lib"]].append(r["time"])

    print(f"\n  {'库文件':<25s} {'平均耗时':>12s} {'总测试数':>10s}")
    print(f"  {'-' * 47}")
    # 以第一个库为基准计算加速比
    base_key = list(lib_times.keys())[0]
    base_avg = sum(lib_times[base_key]) / len(lib_times[base_key]) if lib_times[base_key] else 0

    for ln in lib_names:
        times_list = lib_times.get(ln, [])
        if not times_list:
            continue
        avg = sum(times_list) / len(times_list)
        speedup = base_avg / avg if avg > 0 else 0
        print(f"  {ln:<25s} {fmt_time(avg):>12s} {len(times_list):>10d}  "
              f"(加速比: {speedup:.2f}×)")

    print()
    print("  说明: 带内核指标的库会多输出一行内部统计。")
    print()


# ======================================================================
#  主入口
# ======================================================================

def main():
    parser = argparse.ArgumentParser(
        description="黑白棋 AI 搜索算法 Benchmark",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python benchmark.py
  python benchmark.py search.dll search_new.dll
  python benchmark.py search_new_debug.dll --depth 8
  python benchmark.py search.dll search_new.dll --repeat 3
        """
    )
    parser.add_argument(
        "dlls", nargs="*",
        help="要测试的 .dll 文件 (支持相对/绝对路径，默认 search.dll search_new.dll)"
    )
    parser.add_argument(
        "--depth", type=int, default=None,
        help="强制覆盖每个局面的最大搜索深度"
    )
    parser.add_argument(
        "--repeat", type=int, default=1,
        help="每个测试重复次数取平均值 (默认 1)"
    )
    args = parser.parse_args()

    # ── 找到工作目录 ──
    script_dir = os.path.dirname(os.path.abspath(__file__))

    # ── 确定要测试的库 ──
    if args.dlls:
        dll_paths = []
        for p in args.dlls:
            # 先尝试作为绝对路径/相对当前目录的路径
            candidate = p if os.path.isabs(p) else os.path.abspath(p)
            if os.path.exists(candidate):
                dll_paths.append(candidate)
            else:
                # 再尝试拼接脚本目录
                candidate = os.path.join(script_dir, p)
                if os.path.exists(candidate):
                    dll_paths.append(candidate)
                else:
                    print(f"[错误] 找不到库文件: {p}")
                    print(f"  尝试过: {os.path.abspath(p)}")
                    print(f"  尝试过: {candidate}")
                    sys.exit(1)
    else:
        # 默认：测试脚本所在目录下所有可用的 search*.dll
        dll_paths = []
        for name in ["search.dll", "search_new.dll", "search_new_debug.dll"]:
            p = os.path.join(script_dir, name)
            if os.path.exists(p):
                dll_paths.append(p)
            else:
                print(f"[提示] 未找到 {name}，跳过")
        if not dll_paths:
            print("[错误] 未指定 DLL 且脚本目录下找不到任何 search*.dll")
            print(f"  脚本目录: {script_dir}")
            sys.exit(1)

    # ── 加载 ──
    libs = []
    for p in dll_paths:
        try:
            lib = SearchLib(p)
            libs.append(lib)
            debug_tag = " [含调试接口]" if lib.has_debug else ""
            print(f"[加载] {lib.name}{debug_tag}")
        except Exception as e:
            print(f"[错误] 加载 {p} 失败: {e}")
            sys.exit(1)

    # ── 准备局面 ──
    positions = list(POSITIONS)
    if args.depth is not None:
        positions = [(n, b, w, args.depth) for n, b, w, _ in positions]

    # ── 预热（每个 lib 先跑一次，清掉冷启动影响）──
    print("\n[预热] 各库初始化 + 浅搜索一次...")
    for lib in libs:
        lib.init()
        lib.search(4, positions[0][1], positions[0][2])
    print("[预热] 完成\n")

    # ── 正式 Benchmark ──
    results = run_benchmark(libs, positions, repeat=args.repeat)
    print_results(results)


if __name__ == "__main__":
    main()
