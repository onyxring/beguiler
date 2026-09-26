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
    process.stdout.write(out.join('').replace(/^\n+/, '') + '\n');
    process.exit(0);
}

function collect(content){
    for(const win of content || []){
        for(const para of win.text || []){
            if(!para.append) out.push('\n');
            const c = para.content || [];
            for(let i = 0; i < c.length; i++){
                const item = c[i];
                if(typeof item === 'object' && item) { if(item.text) out.push(item.text); }
                else if(i % 2 === 1) out.push(item);   // [style, text, style, text, …]
            }
        }
    }
}

let iface = null;
const glkote = global.GlkOte;
const origInit = glkote.init;
glkote.init = (i) => { iface = i; origInit(i); };
glkote.error = (msg) => { out.push(`\nGlulx fatal error: ${msg}\n`); finish(); };
glkote.update = (arg) => {
    collect(arg.content);
    if(arg.disable) return finish();                     // the story exited
    const req = (arg.input || [])[0];
    if(!req) return;
    if(inputLines.length === 0) return finish();
    const line = inputLines.shift();
    const value = req.type === 'char' ? (line.length ? line[0] : 'return') : line;
    setImmediate(() => iface.accept({ type: req.type, gen: arg.gen, window: req.id, value }));
};

new Q(0).init(story, () => {});
setTimeout(() => { out.push('\n[glulx-run: timed out]\n'); finish(); }, 20000).unref();
