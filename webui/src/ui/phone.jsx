import { useState } from "preact/hooks";
import { SCREENS, isSetup, stepName } from "../flow/screens.js";
import { ask, mainActions } from "../flow/asks.jsx";
import { Btn, AppHeader, ModePill, progress, Segs, StepArt, Frame, LogView, JogPad, DetailsBody, FilesBody, jogLock, workPos } from "./parts.jsx";

// Defined outside Phone so its children keep their state (typed values) across re-renders
function Section({ id, name, tag, summary, sec, toggle, children }) {
  return <details open={sec === id}><summary onClick={e => { e.preventDefault(); toggle(id); }}><b>{name}</b>{tag}<span>{summary}</span></summary>
    {sec === id && <div class="accbody">{children}</div>}</details>;
}


// Phone layout B: the state is pinned on top, a small visual stays on screen, sections collapse below it,
// and Run and Stop are pinned at the bottom.
export function Phone({ k, ctx, onSettings, overlay }) {
  const s = SCREENS[k], setup = isSetup(k), q = ask(k, ctx);
  const [open, setOpen] = useState(null);
  const sec = open || (setup || s.mode === "Error" || k === "wifi" ? "step" : "details");
  const toggle = (id) => setOpen(sec === id ? "none" : id);
  const { skipped, label } = setup ? progress(s.step, ctx.a.ran) : {};
  const lock = jogLock(k, ctx.d);
  const main = mainActions(k, ctx).map(b => ({ ...b, label: b.label.replace(/^Run .*/, "Run").replace(/^Continue from .*/, "Continue") }));
  return <div class="phoneapp">
    <header class="p-head">
      <AppHeader d={ctx.d} onSettings={onSettings} />
      <div class="p-top"><ModePill s={s} /><span class="sub">{s.sub}</span></div>
      {setup ? <div class="p-prog"><span>{label}</span><Segs step={s.step} skipped={skipped} /></div>
        : <div class={`p-msg${s.alert || q.alert ? " alert" : ""}`}>{q.text || s.msg}</div>}
    </header>
    <main class="p-body">
      <div class="accvis"><Frame k={k} d={ctx.d} /></div>
      <div class="acc">
        <Section sec={sec} toggle={toggle} id="step" name={setup ? "Setup" : "Next"} summary={setup ? stepName(k) : q.buttons.length ? q.buttons[0].label : "Nothing to do"}>
          {setup && <StepArt step={s.step} d={ctx.d} />}
          <p class="note" style={`color:${q.alert ? "var(--danger)" : "var(--text)"}`}>{q.text || s.msg}</p>
          {q.input}
          {q.buttons.length > 0 && <div class="row">{q.buttons.map(b => <Btn b={b} />)}</div>}
        </Section>
        <Section sec={sec} toggle={toggle} id="details" name="Details" summary={s.nopos ? "No position yet" : `Work X ${workPos(ctx.d).x.toFixed(1)}  Y ${workPos(ctx.d).y.toFixed(1)}`}>
          <DetailsBody k={k} d={ctx.d} a={ctx.a} />
        </Section>
        <Section sec={sec} toggle={toggle} id="jog" name="Jog" summary={lock ? lock[0] : "Ready to jog"}><div class="jogbox"><JogPad k={k} d={ctx.d} send={ctx.send} /></div></Section>
        <Section sec={sec} toggle={toggle} id="files" name="Files" summary={ctx.a.file || "No file chosen"}><FilesBody k={k} {...ctx} /></Section>
        <Section sec={sec} toggle={toggle} id="log" name="Log" summary={(ctx.d.log.at(-1) || "").slice(9, 50)}><LogView d={ctx.d} /></Section>
      </div>
    </main>
    {overlay}
    <footer class="p-act"><div class="row">
      {setup ? <button class="btn go" type="button" disabled>Run</button> : main.map(b => <Btn b={b} />)}
      {ctx.d.connected && k !== "wifi" && <button class="btn danger" type="button" onClick={() => ctx.send("stop")}>Stop</button>}
    </div></footer>
  </div>;
}
