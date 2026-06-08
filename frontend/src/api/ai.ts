/**
 * ai.ts — AI API 通信层
 *
 * 泛化设计：
 *   EngineConfig 只包含 url / engine / params 三个字段。
 *   不做任何引擎特定假设 — 所有参数通过 params: Record<string, number> 传递。
 *   后端根据 engine 字段查找适配器，由适配器自行解释 params 键名。
 */

// ======================================================================
//  类型（无引擎特定硬编码）
// ======================================================================

/** 单个 AI 引擎的完整配置 */
export interface EngineConfig {
  /** API 地址（如 http://127.0.0.1:8000/api/ai-move） */
  url: string;
  /** 引擎类型名（对应后端的适配器注册名，如 "negamax" / "mcts"） */
  engine: string;
  /** 引擎参数 — 键名由各引擎 spec.params[].key 定义 */
  params: Record<string, number | string>;
}

export interface Point {
  r: number;
  c: number;
}

export interface AIMoveResult extends Point {
  /** 搜索深度 / 迭代次数等额外信息 */
  depth?: number;
}

// ======================================================================
//  后端引擎元信息类型（GET /api/engines 响应）
// ======================================================================

export interface EngineParamOption {
  value: string;
  label: string;
}

export interface EngineParamSpec {
  key: string;
  type: string;             // "int" | "select"
  default: number | string;
  label: string;
  min?: number;
  max?: number;
  /** 仅 type="select" — 下拉选项列表 */
  options?: EngineParamOption[];
  /** 仅当 other_key 的值 == required_value 时显示该参数 */
  show_if?: Record<string, string>;
}

export interface EngineSpec {
  name: string;
  label: string;
  params: EngineParamSpec[];
}

// ======================================================================
//  引擎列表（前端启动时 fetch，缓存于此）
// ======================================================================

let _engineSpecCache: EngineSpec[] | null = null;

/** 获取后端已注册引擎列表（含参数 schema）。结果缓存于内存。 */
export async function fetchEngineSpecs(
  baseUrl: string,
): Promise<EngineSpec[]> {
  if (_engineSpecCache) return _engineSpecCache;

  const url = baseUrl.replace(/\/ai-move.*/, "/engines");
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`获取引擎列表失败，状态码: ${response.status}`);
  }
  _engineSpecCache = await response.json();
  return _engineSpecCache!;
}

/** 清除缓存（开发调试用）。 */
export function clearEngineSpecCache(): void {
  _engineSpecCache = null;
}

// ======================================================================
//  统一 AI 走法请求
// ======================================================================

/**
 * 向 AI 后端请求下一步落子。
 *
 * 请求体:
 *   { board, player, engine_type: config.engine, params: config.params }
 *
 * 各引擎的参数通过 config.params 传递，后端适配器自行解释键名。
 */
export async function fetchAIMove(
  config: EngineConfig,
  grid: number[][],
  aiPlayerValue: number,
): Promise<AIMoveResult> {
  const body = {
    board: grid,
    player: aiPlayerValue,
    engine_type: config.engine,
    params: config.params,
  };

  try {
    const response = await fetch(config.url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });

    if (!response.ok) {
      throw new Error(`后端请求失败，状态码: ${response.status}`);
    }

    const data = await response.json();

    if (typeof data.r !== "number" || typeof data.c !== "number") {
      throw new Error("后端返回的数据格式不正确，期望 {r, c}");
    }

    return { r: data.r, c: data.c, depth: data.depth };
  } catch (error) {
    console.error("AI 接口通信报错:", error);
    throw error;
  }
}

// ======================================================================
//  重置 AI 引擎
// ======================================================================

/**
 * 告知后端重置 AI 引擎状态（清空置换表等）。
 * 自动将 config.url 中的 /ai-move* 替换为 /init。
 */
export async function fetchResetAI(config: EngineConfig): Promise<void> {
  const initUrl = config.url
    .replace("/ai-move-timelimit", "/init")
    .replace("/ai-move", "/init");

  try {
    const response = await fetch(initUrl, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ engine_type: config.engine }),
    });

    if (!response.ok) {
      console.warn(`通知 AI 引擎重置失败 (${config.engine})`);
    } else {
      console.log(`AI 引擎重置成功 (${config.engine})`);
    }
  } catch (error) {
    console.error("重置 AI 引擎通信异常:", error);
  }
}
