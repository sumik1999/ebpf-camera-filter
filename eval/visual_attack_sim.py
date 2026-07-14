#!/usr/bin/env python3
import argparse
import csv
import html
import json
import struct
import zlib
from pathlib import Path


CAMERAS = ["10.20.0.11", "10.20.0.12", "10.20.0.13", "10.20.0.14"]


def read_csv(path):
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def load_json(path):
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def find_run(results_dir, run_id=None, case=None):
    runs = [p for p in results_dir.iterdir() if p.is_dir() and (p / "summary.json").exists()]
    if not runs:
        raise SystemExit(f"No run summaries found under {results_dir}")
    if run_id and run_id != "latest":
        run = results_dir / run_id
        if not (run / "summary.json").exists():
            raise SystemExit(f"Run id not found: {run_id}")
        return run
    if case:
        runs = [p for p in runs if load_json(p / "summary.json").get("case") == case]
        if not runs:
            raise SystemExit(f"No runs found for case={case}")
    return max(runs, key=lambda p: p.stat().st_mtime)


def status_for_sample(row):
    if row.get("status") == "timeout":
        return "red"
    try:
        rtt = float(row.get("rtt_ms") or 0)
    except ValueError:
        rtt = 0
    if rtt >= 5.0:
        return "yellow"
    return "green"


def build_frames(telemetry_rows, summary):
    by_time = {}
    for row in telemetry_rows:
        try:
            t = round(float(row.get("time_sec") or 0))
        except ValueError:
            t = 0
        camera = row.get("camera_ip", "")
        by_time.setdefault(t, {})[camera] = status_for_sample(row)

    attack_sent = int(summary.get("attack", {}).get("sent", 0) or 0)
    duration = float(summary.get("attack", {}).get("duration_sec", 10) or 10)
    pps = int(attack_sent / duration) if duration else 0
    frames = []
    max_t = max(by_time.keys()) if by_time else int(duration)
    for t in range(max_t + 1):
        statuses = {camera: by_time.get(t, {}).get(camera, "green") for camera in CAMERAS}
        frames.append({"t": t, "statuses": statuses, "attack_pps": pps})
    return frames


def write_html(path, run_name, summary, frames):
    case = summary.get("case", "unknown")
    attack = summary.get("attack_type", "unknown")
    drop_pct = float(summary.get("estimated_attack_drop_pct", 0.0) or 0.0)
    telemetry_loss = float(summary.get("telemetry", {}).get("loss_pct", 0.0) or 0.0)
    payload = json.dumps(frames)
    camera_nodes = "".join(
        f'<div class="camera" id="cam-{i}"><span>Camera {i}</span><small>{ip}</small></div>'
        for i, ip in enumerate(CAMERAS, start=1)
    )
    doc = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Airport Attack Replay</title>
<style>
body {{ font-family: Segoe UI, Arial, sans-serif; margin: 24px; background: #f7f7f4; color: #202124; }}
.meta {{ margin-bottom: 16px; line-height: 1.5; }}
.stage {{ position: relative; width: 920px; max-width: 100%; height: 520px; border: 1px solid #bbb; background: #fff; overflow: hidden; }}
.node {{ position: absolute; padding: 10px 12px; border: 2px solid #333; background: white; border-radius: 6px; min-width: 110px; text-align: center; z-index: 2; }}
.node small, .camera small {{ display:block; color:#555; margin-top:4px; }}
#apoc {{ left: 50px; top: 70px; border-color: #2770b8; }}
#attacker {{ left: 50px; top: 340px; border-color: #a63a3a; }}
#guard {{ left: 385px; top: 205px; border-color: #333; }}
.camera {{ position: absolute; right: 55px; width: 140px; padding: 9px; border: 2px solid #2f7d32; background: #eaf7eb; border-radius: 6px; text-align:center; z-index: 2; }}
#cam-1 {{ top: 45px; }} #cam-2 {{ top: 150px; }} #cam-3 {{ top: 255px; }} #cam-4 {{ top: 360px; }}
.green {{ border-color:#2f7d32; background:#eaf7eb; }}
.yellow {{ border-color:#b58b00; background:#fff5cf; }}
.red {{ border-color:#b3261e; background:#fde7e7; }}
.link {{ position:absolute; height:4px; transform-origin:left center; background:#55a868; z-index:1; opacity:.8; }}
.attack {{ background:#cc4c4c; height: var(--attack-width, 8px); }}
#time {{ font-size: 20px; font-weight: 650; }}
.legend span {{ display:inline-block; padding:4px 8px; margin-right:8px; border-radius:4px; }}
</style>
</head>
<body>
<h1>Airport Smart-Camera Attack Replay</h1>
<div class="meta">
<div><b>Run:</b> {html.escape(run_name)}</div>
<div><b>Defense:</b> {html.escape(case)} | <b>Attack:</b> {html.escape(attack)} | <b>Telemetry loss:</b> {telemetry_loss:.2f}% | <b>Estimated attack drop:</b> {drop_pct:.2f}%</div>
<div id="time">t=0s</div>
<div class="legend"><span class="green">OK</span><span class="yellow">RTT/jitter high</span><span class="red">timeout/loss</span></div>
</div>
<div class="stage" id="stage">
  <div class="node" id="apoc">APOC<small>10.10.0.2</small></div>
  <div class="node" id="attacker">Attacker<small>10.10.0.3</small></div>
  <div class="node" id="guard">Guard Gateway<small>{html.escape(case)}</small></div>
  {camera_nodes}
</div>
<script>
const frames = {payload};
const stage = document.getElementById('stage');
function center(el) {{
  const s = stage.getBoundingClientRect();
  const r = el.getBoundingClientRect();
  return [r.left - s.left + r.width / 2, r.top - s.top + r.height / 2];
}}
function link(a, b, cls, width) {{
  const [x1,y1] = center(document.getElementById(a));
  const [x2,y2] = center(document.getElementById(b));
  const dx=x2-x1, dy=y2-y1, len=Math.hypot(dx,dy);
  const el=document.createElement('div');
  el.className='link ' + cls;
  el.style.left=x1+'px'; el.style.top=y1+'px'; el.style.width=len+'px';
  el.style.transform='rotate('+Math.atan2(dy,dx)+'rad)';
  el.style.setProperty('--attack-width', width+'px');
  stage.appendChild(el);
}}
function draw(i) {{
  document.querySelectorAll('.link').forEach(e => e.remove());
  const f = frames[i % frames.length];
  document.getElementById('time').textContent = 't=' + f.t + 's | attack ~' + f.attack_pps + ' pps';
  link('apoc','guard','telemetry',4);
  link('attacker','guard','attack',Math.min(22, Math.max(4, f.attack_pps / 160)));
  for (let n=1; n<=4; n++) {{
    const cam = document.getElementById('cam-'+n);
    const ip = Object.keys(f.statuses)[n-1];
    cam.className = 'camera ' + (f.statuses[ip] || 'green');
    link('guard','cam-'+n,'telemetry',4);
  }}
}}
let idx=0; draw(0); setInterval(() => {{ idx=(idx+1)%frames.length; draw(idx); }}, 1000);
</script>
</body>
</html>
"""
    path.write_text(doc, encoding="utf-8")


def write_png(path, summary):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        return write_png_fallback(path, summary)
    per_camera = summary.get("per_camera", {})
    labels = list(per_camera.keys()) or CAMERAS
    losses = [float(per_camera.get(label, {}).get("loss_pct", 0.0) or 0.0) for label in labels]
    rtts = [float(per_camera.get(label, {}).get("avg_rtt_ms", 0.0) or 0.0) for label in labels]
    fig, ax1 = plt.subplots(figsize=(8, 4.5))
    ax1.bar(labels, losses, color="#d95f02", label="Loss %")
    ax1.set_ylabel("Telemetry loss %")
    ax1.set_ylim(0, max(5, max(losses, default=0) + 1))
    ax2 = ax1.twinx()
    ax2.plot(labels, rtts, color="#1b9e77", marker="o", label="Avg RTT ms")
    ax2.set_ylabel("Avg RTT ms")
    plt.title(f"{summary.get('case', 'unknown')} / {summary.get('attack_type', 'unknown')}")
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)
    return True


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_png_fallback(path, summary):
    width, height = 900, 460
    bg = (255, 255, 255)
    pixels = [bytearray(bg * width) for _ in range(height)]

    def rect(x0, y0, x1, y1, color):
        x0, x1 = max(0, x0), min(width, x1)
        y0, y1 = max(0, y0), min(height, y1)
        for y in range(y0, y1):
            row = pixels[y]
            for x in range(x0, x1):
                off = x * 3
                row[off:off + 3] = bytes(color)

    def border(x0, y0, x1, y1, color):
        rect(x0, y0, x1, y0 + 2, color)
        rect(x0, y1 - 2, x1, y1, color)
        rect(x0, y0, x0 + 2, y1, color)
        rect(x1 - 2, y0, x1, y1, color)

    per_camera = summary.get("per_camera", {})
    losses = [float(per_camera.get(ip, {}).get("loss_pct", 0.0) or 0.0) for ip in CAMERAS]
    rtts = [float(per_camera.get(ip, {}).get("avg_rtt_ms", 0.0) or 0.0) for ip in CAMERAS]
    max_loss = max(5.0, max(losses, default=0.0))
    max_rtt = max(1.0, max(rtts, default=0.0))

    border(40, 40, 860, 410, (40, 40, 40))
    for i, loss in enumerate(losses):
        x = 95 + i * 190
        bar_h = int((loss / max_loss) * 260)
        color = (217, 95, 2) if loss > 0 else (82, 174, 90)
        rect(x, 360 - bar_h, x + 70, 360, color)
        border(x, 360 - bar_h, x + 70, 360, (60, 60, 60))
    for i, rtt in enumerate(rtts):
        x = 195 + i * 190
        bar_h = int((rtt / max_rtt) * 260)
        rect(x, 360 - bar_h, x + 40, 360, (27, 158, 119))

    raw = b"".join(b"\x00" + bytes(row) for row in pixels)
    data = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(raw, 9))
        + png_chunk(b"IEND", b"")
    )
    path.write_bytes(data)
    return True


def main():
    parser = argparse.ArgumentParser(description="Generate lightweight visual replay from saved experiment logs")
    parser.add_argument("--results-dir", default="eval/results")
    parser.add_argument("--run-id", default="latest")
    parser.add_argument("--case", default="")
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    summary_csv = read_csv(results_dir / "summary.csv")
    run = find_run(results_dir, args.run_id, args.case or None)
    summary = load_json(run / "summary.json")
    telemetry_rows = read_csv(run / "telemetry.csv")
    frames = build_frames(telemetry_rows, summary)
    if summary_csv:
        summary["summary_csv_rows_seen"] = len(summary_csv)

    html_path = results_dir / "visual_replay.html"
    png_path = results_dir / "visual_summary.png"
    write_html(html_path, run.name, summary, frames)
    png_ok = write_png(png_path, summary)
    print(f"html={html_path}")
    if png_ok:
        print(f"png={png_path}")
    else:
        print("png=not generated; matplotlib unavailable")


if __name__ == "__main__":
    main()
