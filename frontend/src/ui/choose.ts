/**
 * choose.ts — 开局配置面板
 *
 * 启动时从后端获取引擎列表（GET /api/engines），
 * 动态渲染引擎下拉和参数输入行。加新引擎无需改此文件。
 *
 * 支持 spec 特性：
 *   • type="select"  → 下拉选择器（含 options 列表）
 *   • show_if        → 条件显示（当 other_key == required_value 时可见）
 */

import type { EngineConfig, EngineSpec, EngineParamSpec } from "../api/ai";

// ======================================================================
//  引擎数据加载
// ======================================================================

/** 相对路径获取引擎列表 */
async function loadEngineSpecs(): Promise<EngineSpec[]> {
  const resp = await fetch("/api/engines");
  if (!resp.ok) throw new Error(`获取引擎列表失败: ${resp.status}`);
  return resp.json();
}

// ======================================================================
//  UI 构建
// ======================================================================

/** 为一个 param spec 生成输入控件 HTML */
function renderParamControl(p: EngineParamSpec): string {
  if (p.type === "select" && p.options) {
    return p.options
      .map(
        (opt) =>
          `<option value="${opt.value}"${opt.value === String(p.default) ? " selected" : ""}>${opt.label}</option>`,
      )
      .join("");
  }
  /* type === "int" */
  return `<input type="number" class="param-value" data-param-key="${p.key}"
            value="${p.default}" min="${p.min ?? ""}" max="${p.max ?? ""}"
            title="${p.label} (${p.min ?? "-∞"} – ${p.max ?? "∞"})">`;
}

/** 为一个引擎 spec 生成参数输入行 HTML */
function makeParamRows(spec: EngineSpec): string {
  return spec.params
    .map(
      (p) => {
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
      },
    )
    .join("");
}

/** 为一个引擎 spec 生成下拉 option */
function engineOption(spec: EngineSpec, selected: boolean): string {
  return `<option value="${spec.name}"${selected ? " selected" : ""}>
    ${spec.label}</option>`;
}

// ======================================================================
//  show_if 可见性管理
// ======================================================================

/**
 * 根据当前 select 值更新所有带 show_if 条件的参数行的可见性。
 * 任一 select 改变后调用。
 */
function updateParamVisibility(sideEl: HTMLElement): void {
  /* 收集当前 side 所有 select 型 param 的值 */
  const selectValues: Record<string, string> = {};
  sideEl.querySelectorAll<HTMLSelectElement>(".param-select").forEach((sel) => {
    selectValues[sel.dataset.paramKey!] = sel.value;
  });

  /* 遍历带 show_if 的行 */
  sideEl.querySelectorAll<HTMLElement>("[data-show-if]").forEach((row) => {
    const condition = JSON.parse(row.dataset.showIf!) as Record<string, string>;
    let visible = true;
    for (const [key, required] of Object.entries(condition)) {
      if (selectValues[key] !== required) {
        visible = false;
        break;
      }
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

  /* 收集所有参数值：select 型取字符串，number input 取整数 */
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
        options: [
          { value: "fixed_depth", label: "固定深度" },
          { value: "time_limit", label: "限时搜索" },
        ],
      },
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
      { key: "iterations", type: "int", default: 20000, min: 100, max: 10000000, label: "模拟次数" },
    ],
  },
];

// ======================================================================
//  主入口
// ======================================================================

export async function showChoosePanel(
  onChoose: (
    blackConfig: EngineConfig | null,
    whiteConfig: EngineConfig | null,
  ) => void,
) {
  /* ── 先加载引擎列表 ── */
  let specs: EngineSpec[];
  try {
    specs = await loadEngineSpecs();
  } catch (err) {
    console.error("无法获取引擎列表，使用内置回退:", err);
    specs = FALLBACK_SPECS;
  }

  const defaultSpec = specs[0];

  /* ── 构建 DOM ── */
  const panel = document.createElement("div");
  panel.id = "choose-panel";
  panel.innerHTML = `
    <p class="choose-title">⚙ 配置 AI 引擎</p>

    <div class="choose-sides">

      <!-- 黑方 -->
      <div class="choose-side" id="black-side">
        <div class="side-header side-header-black">⚫ 黑方</div>
        <input type="url" class="side-url" placeholder="留空 = 人类玩家"
               value="/api/ai-move">
        <select class="engine-type" data-side="black">
          ${specs.map((s) => engineOption(s, s.name === defaultSpec.name)).join("")}
        </select>
        <div class="param-group">
          ${makeParamRows(defaultSpec)}
        </div>
      </div>

      <!-- 白方 -->
      <div class="choose-side" id="white-side">
        <div class="side-header side-header-white">⚪ 白方</div>
        <input type="url" class="side-url" placeholder="留空 = 人类玩家"
               value="">
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

  /* ── 初始 show_if 可见性 ── */
  document.querySelectorAll<HTMLElement>(".choose-side").forEach((side) => {
    updateParamVisibility(side);
  });

  /* ── 引擎切换 → 重建参数行 ── */
  panel.querySelectorAll<HTMLSelectElement>(".engine-type").forEach((sel) => {
    sel.addEventListener("change", () => {
      const sideEl = sel.closest<HTMLElement>(".choose-side")!;
      const paramGroup = sideEl.querySelector<HTMLDivElement>(".param-group")!;
      const spec = specs.find((s) => s.name === sel.value);
      if (spec) {
        paramGroup.innerHTML = makeParamRows(spec);
        updateParamVisibility(sideEl);

        /* 重新绑定该 side 内 select 的 change 事件 */
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
  const startBtn = document.getElementById("start-btn")!;
  startBtn.addEventListener("click", () => {
    const blackConfig = readSideConfig(document.getElementById("black-side")!);
    const whiteConfig = readSideConfig(document.getElementById("white-side")!);
    onChoose(blackConfig, whiteConfig);
    cleanup();
  });

  function cleanup() {
    overlay.remove();
  }
}
