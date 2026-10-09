#!/usr/bin/env node
// Run a Glulx story headless under Quixe and print its text transcript: the Glulx counterpart of
// the `zvm` command the execution tier uses for Z-code.
//
//   node glulx-run.js story.ulx  < input
//
// Each line of stdin answers one line or character input request (a character request takes the
// line's first character, or Return for an empty line). When stdin runs out and the story asks for
// more, the run ends. A fatal VM error is printed as "Glulx fatal error: …" so a test fails on it.
//
// A line `@link N [partial]` clicks the Nth hyperlink printed so far (1-based) instead. `partial` is
// text the player has typed at a pending line prompt without pressing Return; the story sees it if it
// cancels the line input. A click with no hyperlink request pending prints a note and ends the run,
// and a line request that resumes with pre-entered text prints it as `[initial: …]`.
//
// Quixe comes from $QUIXE_DIR, else beguilex's node_modules (the same install that provides zvm).
'use strict';
const fs = require('fs');
const path = require('path');

const story = process.argv[2];
if(!story || !fs.existsSync(story)){
    console.error(`Usage: glulx-run.js <story.ulx> — "${story || ''}" does not exist`);
    process.exit(2);
}
const quixeDir = process.env.QUIXE_DIR
    || path.resolve(__dirname, '../../../beguilex/node_modules/quixe');

let inputLines = fs.readFileSync(0, 'utf8').split('\n');
if(inputLines.length && inputLines[inputLines.length - 1] === '') inputLines.pop();

// app.js writes each paragraph to the console and Quixe logs its own progress; only the transcript
// below belongs on stdout.
const realLog = console.log;
console.log = () => {};

const Q = require(path.join(quixeDir, 'app.js'));
const out = [];
let finished = false;
function finish(){
    if(finished) return;
    finished = true;
    for(const c of heldContent) collect(c);   // the story never asked for input: nothing to tell apart
    process.stdout.write(out.join('').replace(/^\n+/, '') + '\n');
    process.exit(0);
}

// With GLULX_RUN_GRIDS=1, text-grid windows (status bars, menus) are tracked too, and a snapshot of
// each is printed whenever the story waits for input.
const showGrids = process.env.GLULX_RUN_GRIDS === '1';
// With GLULX_RUN_WINDOWS=1, text printed to a buffer window other than the main one (the one the
// player types into) starts its own line, tagged `[win N] `, so it can't run into the main text.
const tagWindows = process.env.GLULX_RUN_WINDOWS === '1';
let mainWin = null;
const heldContent = [];   // updates that arrived before the main window was known
const grids = new Map();   // window id → array of line strings
// A GlkOte content array mixes { style, text, hyperlink? } objects with bare `style, text` string
// pairs, so a pair's position cannot be read from the array index.
function eachRun(content, fn){
    for(let i = 0; i < content.length; ){
        const item = content[i];
        if(typeof item === 'object' && item){ fn(item); i++; }
        else { fn({ style: item, text: content[i + 1] }); i += 2; }
    }
}
function gridText(content){
    let t = '';
    eachRun(content, run => { if(run.text) t += run.text; });
    return t;
}
let promptIdx = -1;   // where the latest update's trailing `>` prompt starts in `out`, or -1
function snapshotGrids(){
    const snaps = [];
    for(const [id, lines] of grids)
        snaps.push(`\n[grid ${id}]\n` + lines.map(l => '|' + (l || '') + '|').join('\n') + '\n');
    if(promptIdx >= 0) out.splice(promptIdx, 0, ...snaps);   // before the prompt the echo continues
    else out.push(...snaps);
}

function collect(content){
    const tagged = [];   // other windows' lines: after the main window's text for the same update,
    let promptAt = -1;   // but before its trailing `>` prompt, which the command's echo continues
    for(const win of content || []){
        if(showGrids && win.lines){
            const g = grids.get(win.id) || [];
            if(win.clear) g.length = 0;
            for(const ln of win.lines) g[ln.line] = gridText(ln.content || []);
            grids.set(win.id, g);
        }
        for(const para of win.text || []){
            if(tagWindows && mainWin !== null && win.id !== mainWin){
                let t = '';
                eachRun(para.content || [], run => {
                    if(run.text) t += run.text;
                    if(run.hyperlink) links.push({ win: win.id, value: run.hyperlink });
                });
                if(t) tagged.push(`\n[win ${win.id}] ` + t + '\n');
                continue;
            }
            if(!para.append) out.push('\n');
            const paraStart = out.length;
            let paraText = '';
            eachRun(para.content || [], run => {
                if(run.text) paraText += run.text;
                if(run.text) out.push(run.text);
                if(run.hyperlink) links.push({ win: win.id, value: run.hyperlink });
            });
            if(!para.append && paraText.trim() === '>') promptAt = paraStart - 1;
        }
    }
    if(promptAt >= 0) out.splice(promptAt, 0, ...tagged);
    else for(const t of tagged) out.push(t);
    promptIdx = promptAt >= 0 ? promptAt + tagged.length : -1;
}

const links = [];   // every hyperlinked run of text printed, in order: { win, value }

let iface = null;
const glkote = global.GlkOte;
const origInit = glkote.init;
glkote.init = (i) => { iface = i; origInit(i); };
glkote.error = (msg) => {
    // Text printed just before the fault is still in Glk's buffers; flush it so the transcript shows
    // where the story was.
    const glkUpdate = glkote.update;
    glkote.update = (arg) => collect(arg.content);
    try { global.Glk.update(); } catch(e) { /* the VM may be too far gone to flush */ }
    glkote.update = glkUpdate;
    out.push(`\nGlulx fatal error: ${msg}\n`);
    finish();
};
glkote.update = (arg) => {
    if(tagWindows && mainWin === null){
        // The main window is the one the player types into. Until it asks for input, hold the text,
        // so what other windows print before then is still told apart.
        const first = (arg.input || []).find(r => r.type === 'line' || r.type === 'char');
        if(!first){ heldContent.push(arg.content); }
        else {
            mainWin = first.id;
            for(const c of heldContent) collect(c);
            heldContent.length = 0;
            collect(arg.content);
        }
    } else collect(arg.content);
    if(arg.disable) return finish();                     // the story exited
    const inputs = arg.input || [];
    const req = inputs.find(r => r.type);
    if(!req) return;
    if(req.type === 'line' && req.initial) out.push(`[initial: ${req.initial}]`);
    if(showGrids) snapshotGrids();
    if(inputLines.length === 0) return finish();
    const line = inputLines.shift();
    const click = /^@link\s+(\d+)(?:\s(.*))?$/.exec(line);
    if(click){
        const link = links[Number(click[1]) - 1];
        if(!link){ out.push(`\n[glulx-run: no hyperlink #${click[1]}]\n`); return finish(); }
        if(!inputs.some(r => r.id === link.win && r.hyperlink)){
            out.push(`\n[glulx-run: hyperlink #${click[1]} clicked with no hyperlink request pending]\n`);
            return finish();
        }
        const partial = (click[2] !== undefined && req.type === 'line') ? { [req.id]: click[2] } : undefined;
        setImmediate(() => iface.accept({ type: 'hyperlink', gen: arg.gen, window: link.win, value: link.value, partial }));
        return;
    }
    const value = req.type === 'char' ? (line.length ? line[0] : 'return') : line;
    setImmediate(() => iface.accept({ type: req.type, gen: arg.gen, window: req.id, value }));
};

new Q(0).init(story, () => {});
setTimeout(() => { out.push('\n[glulx-run: timed out]\n'); finish(); }, 20000).unref();
