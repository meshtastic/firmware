// Minimal ANSI SGR → HTML converter for log panes. Firmware debug output and
// pytest both emit color codes (e.g. \x1b[34m for DEBUG). We render the common
// foreground colors + bold and DROP every other escape sequence. Text content
// is HTML-escaped first, so the only markup that reaches the DOM is our own
// <span> tags — safe to use with v-html.

const CSI = /\x1b\[([0-9;]*)([A-Za-z])/g;

// Tuned for a dark background (pure black/white pushed toward slate).
const FG: Record<number, string> = {
  30: "#64748b",
  31: "#f87171",
  32: "#34d399",
  33: "#fbbf24",
  34: "#60a5fa",
  35: "#c084fc",
  36: "#22d3ee",
  37: "#e5e7eb",
  90: "#94a3b8",
  91: "#fca5a5",
  92: "#6ee7b7",
  93: "#fde68a",
  94: "#93c5fd",
  95: "#d8b4fe",
  96: "#67e8f9",
  97: "#f8fafc",
};

function escapeHtml(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function span(text: string, color: string | null, bold: boolean): string {
  const styles: string[] = [];
  if (color) styles.push(`color:${color}`);
  if (bold) styles.push("font-weight:600");
  const attr = styles.length ? ` style="${styles.join(";")}"` : "";
  return `<span${attr}>${escapeHtml(text)}</span>`;
}

const _cache = new Map<string, string>();

export function ansiToHtml(line: string): string {
  const hit = _cache.get(line);
  if (hit !== undefined) return hit;

  let out = "";
  let idx = 0;
  let color: string | null = null;
  let bold = false;
  CSI.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = CSI.exec(line)) !== null) {
    if (m.index > idx) out += span(line.slice(idx, m.index), color, bold);
    idx = CSI.lastIndex;
    if (m[2] === "m") {
      const codes = m[1] === "" ? [0] : m[1].split(";").map((c) => Number(c));
      for (const c of codes) {
        if (c === 0) {
          color = null;
          bold = false;
        } else if (c === 1) bold = true;
        else if (c === 22) bold = false;
        else if (c === 39) color = null;
        else if (FG[c] !== undefined) color = FG[c];
      }
    }
    // Non-SGR sequences (cursor moves, clear-line, …) are dropped.
  }
  if (idx < line.length) out += span(line.slice(idx), color, bold);

  // Bound the cache so a long-running session doesn't grow unbounded.
  if (_cache.size > 6000) _cache.clear();
  _cache.set(line, out);
  return out;
}
