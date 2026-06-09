/**
 * choose.ts — 开局配置面板
 *
 * 启动时从后端获取引擎列表（GET /api/engines），
 * 动态渲染引擎下拉和参数输入行。加新引擎无需改此文件。
 *
 * 支持 spec 特性：
 *   • type="select"  → 下拉选择器（含 options 列表）
 *   • show_if        → 条件显示（当 other_key == required_value 时可见）
 *
 * 空 URL 自动隐藏引擎 UI（人类玩家模式）。
 */

import type { EngineConfig, EngineSpec, EngineParamSpec } from "../api/ai";

// ======================================================================
//  引擎数据加载
// ======================================================================

async function loadEngineSpecs(baseUrl?: string): Promise<EngineSpec[]> {
  const enginesUrl = baseUrl
    ? baseUrl.replace(/\/(ai-move-timelimit|ai-move)(\?.*)?$/, "/engines")
    : "/api/engines";
  const resp = await fetch(enginesUrl);
  if (!resp.ok) throw new Error(`获取引擎列表失败: ${resp.status}`);
  return resp.json();
}

// ======================================================================
//  UI 构建
// ======================================================================

function renderParamControl(p: EngineParamSpec): string {
  if (p.type === "select" && p.options) {
    return p.options
      .map(
        (opt) =>
          `<option value="${opt.value}"${opt.value === String(p.default) ? " selected" : ""}>${opt.label}</option>`,
      )
      .join("");
  }
  return `<input type="number" class="param-value" data-param-key="${p.key}"
            value="${p.default}" min="${p.min ?? ""}" max="${p.max ?? ""}"
            title="${p.label} (${p.min ?? "-∞"} – ${p.max ?? "∞"})">`;
}

function makeParamRows(spec: EngineSpec): string {
  return spec.params
    .map((p) => {
      const showIfAttr = p.show_if
        ? ` data-show-if='${JSON.stringify(p.show_if)}'`
        : "";
      return `
    <div class="param-row" data-param-key="${p.key}"${showIfAttr}>
      <label class="param-label">${p.label}</label>
      ${p.type === "select" ? `<select class="param-value param-select" data-param-key="${p.key}">${renderParamControl(p)}</select>`
        : renderParamControl(p)}
      <span class="param-unit"></span>
    </div>`;
    })
    .join("");
}

function engineOption(spec: EngineSpec, selected: boolean): string {
  return `<option value="${spec.name}"${selected ? " selected" : ""}>
    ${spec.label}</option>`;
}

// ======================================================================
//  show_if 可见性管理
// ======================================================================

function updateParamVisibility(sideEl: HTMLElement): void {
  const selectValues: Record<string, string> = {};
  sideEl.querySelectorAll<HTMLSelectElement>(".param-select").forEach((sel) => {
    selectValues[sel.dataset.paramKey!] = sel.value;
  });

  sideEl.querySelectorAll<HTMLElement>("[data-show-if]").forEach((row) => {
    const condition = JSON.parse(row.dataset.showIf!) as Record<string, string>;
    let visible = true;
    for (const [key, required] of Object.entries(condition)) {
      if (selectValues[key] !== required) { visible = false; break; }
    }
    row.style.display = visible ? "" : "none";
  });
}

// ======================================================================
//  配置读取
// ======================================================================

function readSideConfig(sideEl: HTMLElement): EngineConfig | null {
  const urlInput = sideEl.querySelector<HTMLInputElement>(".side-url")!;
  const url = urlInput.value.trim();
  if (!url) return null;

  const engineSelect = sideEl.querySelector<HTMLSelectElement>(".engine-type")!;
  const engine = engineSelect.value;

  const params: Record<string, number | string> = {};
  sideEl.querySelectorAll<HTMLInputElement>(".param-value").forEach((inp) => {
    const key = inp.dataset.paramKey!;
    params[key] = parseInt(inp.value, 10) || 0;
  });
  sideEl.querySelectorAll<HTMLSelectElement>(".param-select").forEach((sel) => {
    const key = sel.dataset.paramKey!;
    const val = parseInt(sel.value, 10);
    params[key] = isNaN(val) ? sel.value : val;
  });

  return { url, engine, params };
}

// ======================================================================
//  回退 spec（后端不可用时使用）
// ======================================================================

const FALLBACK_SPECS: EngineSpec[] = [
  {
    name: "negamax",
    label: "Negamax (Alpha-Beta)",
    params: [
      { key: "strategy", type: "select", default: "fixed_depth", label: "搜索模式",
        options: [{ value: "fixed_depth", label: "固定深度" }, { value: "time_limit", label: "限时搜索" }] },
      { key: "depth", type: "int", default: 14, min: 1, max: 64, label: "搜索深度",
        show_if: { strategy: "fixed_depth" } },
      { key: "time_limit_ms", type: "int", default: 3000, min: 10, max: 600000, label: "时间上限 (ms)",
        show_if: { strategy: "time_limit" } },
    ],
  },
  {
    name: "mcts",
    label: "MCTS (蒙特卡洛)",
    params: [
      { key: "strategy", type: "select", default: "fixed_iterations", label: "搜索模式",
        options: [
          { value: "fixed_iterations", label: "固定模拟次数" },
          { value: "time_limit", label: "限时搜索" },
        ],
      },
      { key: "iterations", type: "int", default: 20000, min: 100, max: 10000000, label: "模拟次数",
        show_if: { strategy: "fixed_iterations" } },
      { key: "time_limit_ms", type: "int", default: 3000, min: 100, max: 600000, label: "时间上限 (ms)",
        show_if: { strategy: "time_limit" } },
    ],
  },
];

// ======================================================================
//  侧面板 UI 辅助
// ======================================================================

function setSideEngineVisible(sideEl: HTMLElement, visible: boolean) {
  const engineSelect = sideEl.querySelector<HTMLSelectElement>(".engine-type")!;
  const paramGroup = sideEl.querySelector<HTMLDivElement>(".param-group")!;
  engineSelect.style.display = visible ? "" : "none";
  paramGroup.style.display = visible ? "" : "none";
}

function rebuildSideEngineUI(sideId: string, newSpecs: EngineSpec[], sideSpecs: Record<string, EngineSpec[]>) {
  const sideEl = document.getElementById(sideId)!;
  const engineSelect = sideEl.querySelector<HTMLSelectElement>(".engine-type")!;
  const paramGroup = sideEl.querySelector<HTMLDivElement>(".param-group")!;
  const defaultSpec = newSpecs[0];

  sideSpecs[sideId] = newSpecs;
  engineSelect.innerHTML = newSpecs
    .map((s) => engineOption(s, s.name === defaultSpec.name))
    .join("");
  paramGroup.innerHTML = makeParamRows(defaultSpec);
  updateParamVisibility(sideEl);

  sideEl.querySelectorAll<HTMLSelectElement>(".param-select").forEach((ps) => {
    ps.addEventListener("change", () => updateParamVisibility(sideEl));
  });
}

/**
 * 自动探测某一侧 URL 并更新 UI（blur / 初始加载 / 手动探测共用）。
 */
async function autoProbeSide(
  sideEl: HTMLElement,
  sideSpecs: Record<string, EngineSpec[]>,
) {
  const sideId = sideEl.id;
  const urlInput = sideEl.querySelector<HTMLInputElement>(".side-url")!;
  const statusSpan = sideEl.querySelector<HTMLSpanElement>(".url-status")!;

  statusSpan.className = "url-status";
  const raw = urlInput.value.trim();

  /* 空 URL → 人类玩家，成功 */
  if (!raw) {
    setSideEngineVisible(sideEl, false);
    statusSpan.className = "url-status url-ok";
    statusSpan.title = "人类玩家";
    statusSpan.textContent = "✓";
    return;
  }

  /* 有 URL，尝试探测。失败时重新抛出让调用方感知 */
  statusSpan.textContent = "⏳";
  statusSpan.title = "探测中...";

  let remoteSpecs: EngineSpec[];
  try {
    remoteSpecs = await loadEngineSpecs(raw);
  } catch (err) {
    console.error(`探测远程引擎失败 (${sideId}):`, err);
    setSideEngineVisible(sideEl, false);
    statusSpan.className = "url-status url-err";
    statusSpan.title = "探测失败 — 请检查 URL";
    statusSpan.textContent = "✗";
    throw err;
  }

  rebuildSideEngineUI(sideId, remoteSpecs, sideSpecs);
  setSideEngineVisible(sideEl, true);
  statusSpan.className = "url-status url-ok";
  statusSpan.title = `探测成功: ${remoteSpecs.length} 个引擎`;
  statusSpan.textContent = "✓";
}

// ======================================================================
//  主入口
// ======================================================================

export async function showChoosePanel(
  onChoose: (blackConfig: EngineConfig | null, whiteConfig: EngineConfig | null) => void,
) {
  /* ── 加载本地引擎列表 ── */
  let specs: EngineSpec[];
  try {
    specs = await loadEngineSpecs();
  } catch (err) {
    console.error("无法获取引擎列表，使用内置回退:", err);
    specs = FALLBACK_SPECS;
  }
  const defaultSpec = specs[0];

  /* ── 按 side 存储当前活跃的引擎 spec ── */
  const sideSpecs: Record<string, EngineSpec[]> = {
    "black-side": specs,
    "white-side": specs,
  };

  /* ── 构建 DOM ── */
  const panel = document.createElement("div");
  panel.id = "choose-panel";
  panel.innerHTML = `
    <p class="choose-title">⚙ 配置 AI 引擎</p>

    <div class="choose-sides">

      <div class="choose-side" id="black-side">
        <div class="side-header side-header-black">⚫ 黑方</div>
        <div class="url-row">
          <input type="url" class="side-url" placeholder="留空 = 人类玩家"
                 value="/api/ai-move">
          <button type="button" class="url-probe-btn" title="探测远程引擎">🔍</button>
          <span class="url-status" title="尚未探测"></span>
        </div>
        <select class="engine-type" data-side="black">
          ${specs.map((s) => engineOption(s, s.name === defaultSpec.name)).join("")}
        </select>
        <div class="param-group">
          ${makeParamRows(defaultSpec)}
        </div>
      </div>

      <div class="choose-side" id="white-side">
        <div class="side-header side-header-white">⚪ 白方</div>
        <div class="url-row">
          <input type="url" class="side-url" placeholder="留空 = 人类玩家"
                 value="">
          <button type="button" class="url-probe-btn" title="探测远程引擎">🔍</button>
          <span class="url-status" title="尚未探测"></span>
        </div>
        <select class="engine-type" data-side="white">
          ${specs.map((s) => engineOption(s, s.name === defaultSpec.name)).join("")}
        </select>
        <div class="param-group">
          ${makeParamRows(defaultSpec)}
        </div>
      </div>

    </div>

    <div id="start-btn">开始</div>

    <div id="remarks">
      留空 URL → 该方由人类手动落子<br>
      /api/ai-move → 本机 AI 引擎 API<br>
      <b>加新引擎只需改后端 search.py</b>，前端自动适配。
    </div>
  `;

  const overlay = document.createElement("div");
  overlay.id = "choose-overlay";
  document.body.appendChild(overlay);
  overlay.appendChild(panel);

  /* ── 每侧探测状态: "pending" | "ok" | "err" ── */
  const sideProbeStatus: Record<string, string> = {
    "black-side": "pending",
    "white-side": "pending",
  };

  /* ── 开始按钮状态管理 ── */
  const startBtn = document.getElementById("start-btn")!;
  const START_TEXT = { ok: "开始", pending: "请检查两侧引擎连接", err: "请检查两侧引擎连接" };

  function updateStartButton() {
    const bothOk = sideProbeStatus["black-side"] === "ok"
                && sideProbeStatus["white-side"] === "ok";
    if (bothOk) {
      startBtn.className = "";
      startBtn.textContent = START_TEXT.ok;
    } else {
      startBtn.className = "disabled";
      startBtn.textContent = START_TEXT.pending;
    }
  }

  /* ── 初始 show_if 可见性 ── */
  document.querySelectorAll<HTMLElement>(".choose-side").forEach((side) => {
    updateParamVisibility(side);
  });

  /**
   * 包裹 autoProbeSide，联用更新探状态和按钮。
   */
  async function probeAndUpdate(
    sideEl: HTMLElement,
    sideSpecs: Record<string, EngineSpec[]>,
  ) {
    const sideId = sideEl.id;
    sideProbeStatus[sideId] = "pending";
    updateStartButton();

    try {
      await autoProbeSide(sideEl, sideSpecs);
      /* autoProbeSide 不抛错即表示成功（包括空 URL 也算成功） */
      sideProbeStatus[sideId] = "ok";
    } catch {
      sideProbeStatus[sideId] = "err";
    }
    updateStartButton();
  }

  /* ── 初始：空 URL → 直接 ok；有值 → 自动探测 ── */
  const sideEls = Array.from(document.querySelectorAll<HTMLElement>(".choose-side"));
  for (const side of sideEls) {
    const urlInput = side.querySelector<HTMLInputElement>(".side-url")!;
    if (!urlInput.value.trim()) {
      setSideEngineVisible(side, false);
      sideProbeStatus[side.id] = "ok";
    } else {
      /* 探测完成后统一更新按钮 */
      await probeAndUpdate(side, sideSpecs);
    }
  }
  updateStartButton();

  /* ── URL 输入框失焦时自动探测 ── */
  panel.querySelectorAll<HTMLInputElement>(".side-url").forEach((inp) => {
    inp.addEventListener("blur", () => {
      const sideEl = inp.closest<HTMLElement>(".choose-side")!;
      probeAndUpdate(sideEl, sideSpecs);
    });
  });

  /* ── 手动探测按钮 ── */
  panel.querySelectorAll<HTMLButtonElement>(".url-probe-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      const sideEl = btn.closest<HTMLElement>(".choose-side")!;
      probeAndUpdate(sideEl, sideSpecs);
    });
  });

  /* ── 引擎切换 → 重建参数行 ── */
  panel.querySelectorAll<HTMLSelectElement>(".engine-type").forEach((sel) => {
    sel.addEventListener("change", () => {
      const sideEl = sel.closest<HTMLElement>(".choose-side")!;
      const paramGroup = sideEl.querySelector<HTMLDivElement>(".param-group")!;
      const spec = sideSpecs[sideEl.id].find((s) => s.name === sel.value);
      if (spec) {
        paramGroup.innerHTML = makeParamRows(spec);
        updateParamVisibility(sideEl);
        sideEl.querySelectorAll<HTMLSelectElement>(".param-select").forEach((ps) => {
          ps.addEventListener("change", () => updateParamVisibility(sideEl));
        });
      }
    });
  });

  /* ── select 型参数切换 → 更新可见性 ── */
  panel.querySelectorAll<HTMLSelectElement>(".param-select").forEach((ps) => {
    ps.addEventListener("change", () => {
      const sideEl = ps.closest<HTMLElement>(".choose-side")!;
      updateParamVisibility(sideEl);
    });
  });

  /* ── 开始按钮 ── */
  startBtn.addEventListener("click", () => {
    if (sideProbeStatus["black-side"] !== "ok" || sideProbeStatus["white-side"] !== "ok") {
      return;  /* 防双击 / 未通过探测时忽略 */
    }
    const blackConfig = readSideConfig(document.getElementById("black-side")!);
    const whiteConfig = readSideConfig(document.getElementById("white-side")!);
    onChoose(blackConfig, whiteConfig);
    cleanup();
  });

  function cleanup() {
    overlay.remove();
  }
}
