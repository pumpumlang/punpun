#!/usr/bin/env node
'use strict';

// PunPun 0.5 LSP. Diagnostics come from the SAME compiler semantic analyzer used
// by pp check/build/run through one long-lived `ppc semantic-worker` process.
// Editor-only features deliberately avoid invoking codegen/linking.

const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');
const { fileURLToPath, pathToFileURL } = require('url');

let inputBuffer = Buffer.alloc(0);
let shutdownRequested = false;
let rootPath = process.cwd();
const documents = new Map();
const diagnosticTimers = new Map();

function compilerPath() {
  if (process.env.PUNPUN_PPC) return process.env.PUNPUN_PPC;
  const checkout = path.resolve(__dirname, '..', '..', 'build', process.platform === 'win32' ? 'ppc.exe' : 'ppc');
  if (fs.existsSync(checkout)) return checkout;
  return process.platform === 'win32' ? 'ppc.exe' : 'ppc';
}

function compiler(args, options = {}) {
  return childProcess.spawnSync(compilerPath(), args, {
    encoding: 'utf8', timeout: 15000, windowsHide: true, ...options,
  });
}

class SemanticWorker {
  constructor() {
    this.process = null;
    this.buffer = Buffer.alloc(0);
    this.pending = [];
    this.failed = null;
  }

  start() {
    if (this.process && !this.process.killed) return;
    this.failed = null;
    this.process = childProcess.spawn(compilerPath(), ['semantic-worker'], {
      stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true,
    });
    this.process.stdout.on('data', chunk => {
      this.buffer = Buffer.concat([this.buffer, chunk]);
      this.consume();
    });
    this.process.stderr.on('data', () => {}); // protocol diagnostics arrive on stdout.
    this.process.on('error', error => this.rejectAll(error));
    this.process.on('exit', (code, signal) => {
      if (!this.failed) this.rejectAll(new Error(`semantic worker exited (${code ?? signal ?? 'unknown'})`));
      this.process = null;
    });
  }

  rejectAll(error) {
    this.failed = error;
    while (this.pending.length) this.pending.shift().reject(error);
  }

  consume() {
    for (;;) {
      const newline = this.buffer.indexOf(0x0a);
      if (newline < 0) return;
      const header = this.buffer.slice(0, newline).toString('utf8').trim();
      const match = /^RESULT\s+(\d+)$/.exec(header);
      if (!match) {
        // Drop one malformed protocol line rather than poisoning every request.
        this.buffer = this.buffer.slice(newline + 1);
        continue;
      }
      const length = Number(match[1]);
      if (this.buffer.length < newline + 1 + length) return;
      const bodyStart = newline + 1;
      const body = this.buffer.slice(bodyStart, bodyStart + length).toString('utf8');
      this.buffer = this.buffer.slice(bodyStart + length);
      const request = this.pending.shift();
      if (request) request.resolve(body);
    }
  }

  check(file, source) {
    this.start();
    if (!this.process || !this.process.stdin.writable) return Promise.reject(this.failed || new Error('semantic worker unavailable'));
    const fileBytes = Buffer.from(path.resolve(file), 'utf8');
    const sourceBytes = Buffer.from(source, 'utf8');
    return new Promise((resolve, reject) => {
      this.pending.push({ resolve, reject });
      const header = Buffer.from(`CHECK ${fileBytes.length} ${sourceBytes.length}\n`, 'utf8');
      try {
        this.process.stdin.write(Buffer.concat([header, fileBytes, sourceBytes]));
      } catch (error) {
        this.pending.pop();
        reject(error);
      }
    });
  }

  stop() {
    if (!this.process) return;
    try { this.process.stdin.write('QUIT\n'); } catch (_) {}
    try { this.process.stdin.end(); } catch (_) {}
  }
}

const semanticWorker = new SemanticWorker();

function fallbackLanguageInfo() {
  return {
    compiler_version: '0.5.0-beta',
    keywords: ['bring', 'launch', 'say', 'fn', 'object', 'struct', 'contract', 'meets', 'init', 'let', 'mut', 'const', 'return', 'if', 'else', 'while', 'for', 'in', 'break', 'continue', 'true', 'false', 'unsafe', 'raw', 'public', 'private', 'protected', 'extern', 'native', 'async', 'await', 'self'],
    types: ['i64', 'i32', 'u64', 'u32', 'f64', 'f32', 'bool', 'String', 'nums', 'void'],
    builtins: [], modules: [], stdlib_symbols: [],
  };
}

function readLanguageInfo() {
  const result = compiler(['language-info']);
  if (result.status !== 0 || !result.stdout) return fallbackLanguageInfo();
  try { return JSON.parse(result.stdout); }
  catch (_) { return fallbackLanguageInfo(); }
}
let languageInfo = readLanguageInfo();

function send(message) {
  const payload = Buffer.from(JSON.stringify(message), 'utf8');
  process.stdout.write(`Content-Length: ${payload.length}\r\n\r\n`);
  process.stdout.write(payload);
}
function response(id, result) { send({ jsonrpc: '2.0', id, result }); }
function errorResponse(id, code, message) { send({ jsonrpc: '2.0', id, error: { code, message } }); }
function notify(method, params) { send({ jsonrpc: '2.0', method, params }); }

function uriToPath(uri) { try { return fileURLToPath(uri); } catch (_) { return null; } }
function pathToUri(file) { return pathToFileURL(file).toString(); }

function offsetAt(text, position) {
  let offset = 0, line = 0;
  while (line < position.line && offset < text.length) {
    const next = text.indexOf('\n', offset);
    if (next < 0) return text.length;
    offset = next + 1; ++line;
  }
  return Math.min(text.length, offset + Math.max(0, position.character));
}
function positionAt(text, target) {
  target = Math.max(0, Math.min(text.length, target));
  let line = 0, lineStart = 0;
  for (let i = 0; i < target; ++i) if (text.charCodeAt(i) === 10) { ++line; lineStart = i + 1; }
  return { line, character: target - lineStart };
}
function wordAt(text, position) {
  const offset = offsetAt(text, position);
  let start = offset, end = offset;
  while (start > 0 && /[A-Za-z0-9_]/.test(text[start - 1])) --start;
  while (end < text.length && /[A-Za-z0-9_]/.test(text[end])) ++end;
  return { word: text.slice(start, end), start, end };
}

function symbolKind(kind) {
  return ({ function: 12, method: 6, object: 5, struct: 23, contract: 11, field: 8, parameter: 13, variable: 13, constant: 14, module: 2 })[kind] || 13;
}

function parseParameters(raw) {
  if (!raw || !raw.trim()) return [];
  return raw.split(',').map(value => value.trim()).filter(Boolean).map(value => {
    const match = /^(?:mut\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([^=]+?)(?:\s*=.*)?$/.exec(value);
    return match ? { name: match[1], type: match[2].trim() } : { name: value, type: '?' };
  });
}

// Lightweight syntax index for navigation/completion. Compiler diagnostics and
// type correctness remain authoritative; this index intentionally stays usable
// while the user is halfway through a declaration.
function indexDocument(uri, text) {
  const symbols = [];
  const types = new Map();
  const variableTypes = new Map();
  const add = (name, kind, start, extra = {}) => {
    symbols.push({ name, kind, uri, range: { start: positionAt(text, start), end: positionAt(text, start + name.length) }, ...extra });
  };

  for (const match of text.matchAll(/\b(async\s+)?(fn)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*(?:->\s*([^\s{;]+))?/g)) {
    const name = match[3], start = match.index + match[0].indexOf(name);
    add(name, 'function', start, { async: !!match[1], signature: `${match[1] ? 'async ' : ''}${name}(${match[4].trim()})${match[5] ? ` -> ${match[5]}` : ''}`, parameters: parseParameters(match[4]), resultType: match[5] || 'void' });
  }
  for (const match of text.matchAll(/\b(object|struct|contract)\s+([A-Za-z_][A-Za-z0-9_]*)/g)) {
    const kind = match[1], name = match[2], start = match.index + match[0].lastIndexOf(name);
    add(name, kind, start);
    types.set(name, { name, kind, fields: [], methods: [], constructor: null, start });
  }

  // Index type bodies with brace balancing so fields/methods attach to their owner.
  for (const [name, type] of types) {
    const declaration = new RegExp(`\\b${type.kind}\\s+${name}\\b`).exec(text);
    if (!declaration) continue;
    const open = text.indexOf('{', declaration.index + declaration[0].length);
    if (open < 0) continue;
    let depth = 1, close = open + 1;
    for (; close < text.length && depth > 0; ++close) {
      if (text[close] === '{') ++depth;
      else if (text[close] === '}') --depth;
    }
    const bodyEnd = depth === 0 ? close - 1 : text.length;
    const body = text.slice(open + 1, bodyEnd);
    const bodyOffset = open + 1;

    for (const match of body.matchAll(/\b(public|private|protected)?\s*(?:let\s+(mut\s+)?)?([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_<>&*?\[\]]*)\s*[;,]/g)) {
      const fieldName = match[3];
      if (['self', 'return', 'let', 'mut'].includes(fieldName)) continue;
      const start = bodyOffset + match.index + match[0].indexOf(fieldName);
      const field = { name: fieldName, kind: 'field', type: match[4], visibility: match[1] || (type.kind === 'object' ? 'private' : 'public'), mutable: !!match[2], uri, range: { start: positionAt(text, start), end: positionAt(text, start + fieldName.length) } };
      type.fields.push(field); symbols.push(field);
    }
    for (const match of body.matchAll(/\b(public|private|protected)?\s*(async\s+)?fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*(?:->\s*([^\s{;]+))?/g)) {
      const methodName = match[3], start = bodyOffset + match.index + match[0].indexOf(methodName);
      const method = { name: methodName, kind: 'method', owner: name, async: !!match[2], visibility: match[1] || (type.kind === 'object' ? 'private' : 'public'), signature: `${match[2] ? 'async ' : ''}${methodName}(${match[4].trim()})${match[5] ? ` -> ${match[5]}` : ''}`, parameters: parseParameters(match[4]), resultType: match[5] || 'void', uri, range: { start: positionAt(text, start), end: positionAt(text, start + methodName.length) } };
      type.methods.push(method); symbols.push(method);
    }
    const init = /\b(public|private|protected)?\s*init\s*\(([^)]*)\)/g.exec(body);
    if (init) type.constructor = { name, signature: `${name}(${init[2].trim()})`, parameters: parseParameters(init[2]), resultType: name };
  }

  for (const match of text.matchAll(/\b(?:let\s+(?:mut\s+)?|const\s+)([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*([A-Za-z_][A-Za-z0-9_<>&*?\[\]]*))?\s*=\s*([A-Za-z_][A-Za-z0-9_]*)?/g)) {
    const name = match[1], explicitType = match[2], ctor = match[3];
    const start = match.index + match[0].indexOf(name);
    const inferred = explicitType || (ctor && types.has(ctor) ? ctor : null);
    if (inferred) variableTypes.set(name, inferred);
    add(name, /\bconst\b/.test(match[0]) ? 'constant' : 'variable', start, { valueType: inferred || '?' });
  }
  for (const fn of symbols.filter(s => s.kind === 'function' || s.kind === 'method')) {
    for (const parameter of fn.parameters || []) variableTypes.set(parameter.name, parameter.type);
  }
  return { symbols, types, variableTypes };
}

function walkPunpunFiles(directory, out, depth = 0) {
  if (!directory || depth > 30 || out.length >= 1000) return;
  let entries;
  try { entries = fs.readdirSync(directory, { withFileTypes: true }); } catch (_) { return; }
  for (const entry of entries) {
    if (out.length >= 1000) return;
    if (entry.name === '.git' || entry.name === '.punpun' || entry.name === 'build' || entry.name === 'node_modules') continue;
    const full = path.join(directory, entry.name);
    if (entry.isDirectory()) walkPunpunFiles(full, out, depth + 1);
    else if (entry.isFile() && entry.name.endsWith('.pp')) out.push(full);
  }
}
function allDocuments() {
  const result = new Map(documents);
  const files = []; walkPunpunFiles(rootPath, files);
  for (const file of files) {
    const uri = pathToUri(file);
    if (result.has(uri)) continue;
    try { result.set(uri, { uri, text: fs.readFileSync(file, 'utf8'), version: 0 }); } catch (_) {}
  }
  return [...result.values()];
}
function allIndexes() { return allDocuments().map(document => ({ document, index: indexDocument(document.uri, document.text) })); }
function findDefinition(name, receiverType = null) {
  for (const { index } of allIndexes()) {
    if (receiverType && index.types.has(receiverType)) {
      const type = index.types.get(receiverType);
      const member = [...type.fields, ...type.methods].find(item => item.name === name);
      if (member) return member;
    }
    const symbol = index.symbols.find(item => item.name === name && !['field', 'method'].includes(item.kind));
    if (symbol) return symbol;
  }
  return null;
}
function occurrences(uri, text, word) {
  if (!word) return [];
  // Preserve offsets while blanking comments and string contents. Rename and
  // references must never rewrite an identifier merely mentioned in prose.
  const chars = [...text];
  let state = 'code';
  for (let i = 0; i < chars.length; ++i) {
    const c = chars[i], next = chars[i + 1] || '';
    if (state === 'line') {
      if (c === '\n') state = 'code'; else chars[i] = ' ';
      continue;
    }
    if (state === 'string') {
      if (c === '\\') { chars[i] = ' '; if (i + 1 < chars.length) chars[++i] = ' '; continue; }
      if (c === '"') { chars[i] = ' '; state = 'code'; } else if (c !== '\n') chars[i] = ' ';
      continue;
    }
    if (state === 'triple') {
      if (c === '"' && next === '"' && chars[i + 2] === '"') {
        chars[i] = chars[i + 1] = chars[i + 2] = ' '; i += 2; state = 'code';
      } else if (c !== '\n') chars[i] = ' ';
      continue;
    }
    if (c === '/' && next === '/') { chars[i] = chars[i + 1] = ' '; ++i; state = 'line'; continue; }
    if (c === '#') { chars[i] = ' '; state = 'line'; continue; }
    if (c === '"' && next === '"' && chars[i + 2] === '"') {
      chars[i] = chars[i + 1] = chars[i + 2] = ' '; i += 2; state = 'triple'; continue;
    }
    if (c === '"') { chars[i] = ' '; state = 'string'; }
  }
  const searchable = chars.join('');
  const escaped = word.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const regex = new RegExp(`\\b${escaped}\\b`, 'g');
  return [...searchable.matchAll(regex)].map(match => ({ uri, range: { start: positionAt(text, match.index), end: positionAt(text, match.index + word.length) } }));
}

function startsWord(line, word) {
  return line === word || (line.startsWith(word) && !/[A-Za-z0-9_]/.test(line[word.length] || ''));
}
function opensLegacyBlock(line) {
  if (!line.endsWith(':')) return false;
  return ['launch', 'craft', 'shape', 'when', 'otherwise', 'whilst', 'each'].some(word => startsWord(line, word));
}
function leadingClosingBraces(line) {
  let count = 0;
  for (const c of line) {
    if (/\s/.test(c)) continue;
    if (c === '}') { count++; continue; }
    break;
  }
  return count;
}
function togglesTriple(line) {
  return ((line.match(/"""/g) || []).length % 2) !== 0;
}
function braceCounts(line) {
  let opens = 0, closes = 0, string = false, escape = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (!string && c === '/' && line[i + 1] === '/') break;
    if (!string && c === '#') break;
    if (string) {
      if (escape) { escape = false; continue; }
      if (c === '\\') { escape = true; continue; }
      if (c === '"') string = false;
      continue;
    }
    if (c === '"') { string = true; continue; }
    if (c === '{') opens++;
    else if (c === '}') closes++;
  }
  return { opens, closes };
}
function formatText(source) {
  const lines = source.replace(/\r\n/g, '\n').replace(/\r/g, '\n').split('\n');
  if (lines.length && lines[lines.length - 1] === '') lines.pop();
  let indent = 0, triple = false;
  const output = [];
  for (const raw of lines) {
    if (triple) {
      output.push(raw);
      if (togglesTriple(raw)) triple = false;
      continue;
    }
    const line = raw.trim();
    if (!line) { output.push(''); continue; }
    const startsTriple = togglesTriple(line);
    const legacyClose = startsWord(line, 'done') || startsWord(line, 'otherwise');
    const leadingCloses = leadingClosingBraces(line);
    if (legacyClose) indent = Math.max(0, indent - 1);
    if (leadingCloses) indent = Math.max(0, indent - leadingCloses);
    output.push(' '.repeat(indent * 4) + line);
    if (startsTriple) { triple = true; continue; }
    const { opens, closes } = braceCounts(line);
    indent += opens - (closes - leadingCloses);
    if (opensLegacyBlock(line)) indent++;
    indent = Math.max(0, indent);
  }
  return output.join('\n') + '\n';
}

function catalog() {
  const values = new Map();
  for (const builtin of languageInfo.builtins || []) {
    const args = (builtin.parameters || []).map((type, i) => `arg${i + 1}: ${type}`).join(', ');
    values.set(builtin.name, { name: builtin.name, signature: `${builtin.name}(${args}) -> ${builtin.result}`, documentation: builtin.documentation || '', module: null });
  }
  for (const symbol of languageInfo.stdlib_symbols || []) {
    const args = (symbol.parameters || []).map(parameter => `${parameter.name}: ${parameter.type}`).join(', ');
    values.set(symbol.name, { name: symbol.name, signature: `${symbol.name}(${args}) -> ${symbol.result}`, documentation: `From ${symbol.module}.`, module: symbol.module });
  }
  for (const { index } of allIndexes()) {
    for (const symbol of index.symbols) if (symbol.kind === 'function') values.set(symbol.name, { name: symbol.name, signature: symbol.signature, documentation: 'Workspace function.', module: null });
    for (const type of index.types.values()) if (type.constructor) values.set(type.name, { name: type.name, signature: type.constructor.signature, documentation: `${type.kind} constructor.`, module: null });
  }
  return values;
}
function snippetFor(signature) {
  const open = signature.indexOf('('), close = signature.indexOf(')', open + 1);
  if (open < 0 || close < 0) return signature;
  const name = signature.slice(0, open), raw = signature.slice(open + 1, close).trim();
  if (!raw) return `${name}()`;
  const args = raw.split(',').map((arg, i) => `\${${i + 1}:${arg.trim().split(':')[0].trim()}}`).join(', ');
  return `${name}(${args})`;
}

function memberContext(document, position) {
  const offset = offsetAt(document.text, position);
  const prefix = document.text.slice(0, offset);
  const match = /([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)?$/.exec(prefix);
  if (!match) return null;
  const index = indexDocument(document.uri, document.text);
  const receiver = match[1], typed = match[2] || '';
  const receiverType = index.variableTypes.get(receiver) || (index.types.has(receiver) ? receiver : null);
  return receiverType ? { receiver, receiverType, typed, index } : null;
}
function completionItems(document, position) {
  const member = memberContext(document, position);
  if (member) {
    const type = member.index.types.get(member.receiverType) || allIndexes().map(x => x.index.types.get(member.receiverType)).find(Boolean);
    if (!type) return [];
    const entries = [...type.fields, ...type.methods].filter(item => item.visibility !== 'private' && item.name.startsWith(member.typed));
    return entries.map(item => ({
      label: item.name, kind: item.kind === 'method' ? 2 : 5,
      detail: item.kind === 'method' ? item.signature : `field ${item.name}: ${item.type}`,
      insertText: item.kind === 'method' ? snippetFor(item.signature) : item.name,
      insertTextFormat: item.kind === 'method' ? 2 : 1,
    }));
  }

  const offset = offsetAt(document.text, position);
  const prefix = document.text.slice(Math.max(0, offset - 140), offset);
  if (/\b(?:bring|import)\s+[A-Za-z0-9_.:]*$/.test(prefix))
    return (languageInfo.modules || []).map(name => ({ label: name.replace(/\./g, '::'), kind: 9, detail: 'PunPun standard library module' }));

  const items = [];
  for (const entry of catalog().values()) items.push({ label: entry.name, kind: 3, detail: entry.signature, documentation: entry.documentation, insertText: snippetFor(entry.signature), insertTextFormat: 2 });
  for (const { index } of allIndexes()) {
    for (const symbol of index.symbols) {
      if (['field', 'method'].includes(symbol.kind)) continue;
      items.push({ label: symbol.name, kind: symbolKind(symbol.kind), detail: symbol.signature || symbol.valueType || symbol.kind });
    }
  }
  for (const keyword of languageInfo.keywords || []) items.push({ label: keyword, kind: 14, detail: 'PunPun keyword' });
  for (const keyword of ['bring', 'launch', 'say', 'contract', 'meets', 'extern', 'native']) items.push({ label: keyword, kind: 14, detail: 'PunPun keyword' });
  for (const type of languageInfo.types || []) items.push({ label: type, kind: 25, detail: 'PunPun type' });
  return items;
}

function parseDiagnosticOutput(output, document) {
  const diagnostics = [];
  const regex = /(error|warning)\[([A-Z]\d+)\]: ([^\n]+)\n\s*-->\s+(.+):(\d+):(\d+)/g;
  for (const match of output.matchAll(regex)) {
    const line = Math.max(0, Number(match[5]) - 1);
    const character = Math.max(0, Number(match[6]) - 1);
    const sourceLine = document.text.split('\n')[line] || '';
    const tail = sourceLine.slice(character);
    const token = /^[A-Za-z_][A-Za-z0-9_]*|^\S+/.exec(tail)?.[0] || '';
    const length = Math.max(1, token.length);
    const help = /\n\s*= help:\s*([^\n]+)/.exec(output.slice(match.index))?.[1] || '';
    diagnostics.push({
      range: { start: { line, character }, end: { line, character: character + length } },
      severity: match[1] === 'warning' ? 2 : 1,
      code: match[2], source: 'punpun',
      message: help ? `${match[3]}\n${help}` : match[3],
      data: { help },
    });
  }
  return diagnostics;
}

async function publishDiagnostics(uri) {
  const document = documents.get(uri);
  if (!document) return;
  const version = document.version;
  const file = uriToPath(uri);
  if (!file) return;
  try {
    const output = await semanticWorker.check(file, document.text);
    const current = documents.get(uri);
    if (!current || current.version !== version) return; // stale response from older buffer.
    const diagnostics = output ? parseDiagnosticOutput(output, current) : [];
    notify('textDocument/publishDiagnostics', { uri, version, diagnostics });
  } catch (error) {
    notify('window/logMessage', { type: 2, message: `PunPun semantic worker unavailable: ${error.message}` });
  }
}
function scheduleDiagnostics(uri, immediate = false) {
  if (diagnosticTimers.has(uri)) clearTimeout(diagnosticTimers.get(uri));
  const timer = setTimeout(() => { diagnosticTimers.delete(uri); void publishDiagnostics(uri); }, immediate ? 0 : 20);
  diagnosticTimers.set(uri, timer);
}
function applyChanges(document, changes) {
  let text = document.text;
  for (const change of changes) {
    if (!change.range) { text = change.text; continue; }
    const start = offsetAt(text, change.range.start), end = offsetAt(text, change.range.end);
    text = text.slice(0, start) + change.text + text.slice(end);
  }
  return text;
}

const tokenTypes = ['namespace', 'type', 'class', 'interface', 'struct', 'typeParameter', 'parameter', 'variable', 'property', 'enumMember', 'function', 'method', 'keyword', 'string', 'number', 'operator'];
const tokenModifiers = ['declaration','definition','readonly','static','deprecated','abstract','async','modification','documentation','defaultLibrary'];
function semanticTokenData(document) {
  const entries = [];
  const index = indexDocument(document.uri, document.text);
  const addRange = (range, type, modifiers = 0) => {
    if (!range || range.start.line !== range.end.line) return;
    entries.push({ line: range.start.line, char: range.start.character, length: Math.max(1, range.end.character - range.start.character), type, modifiers });
  };
  const kindMap = { object: 'class', struct: 'struct', contract: 'interface', function: 'function', method: 'method', field: 'property', parameter: 'parameter', variable: 'variable', constant: 'variable' };
  for (const symbol of index.symbols) {
    const typeName = kindMap[symbol.kind];
    if (typeName) { const modifiers = (symbol.kind === 'constant' ? (1 << 2) : (1 << 0)) | (symbol.async ? (1 << 6) : 0); addRange(symbol.range, tokenTypes.indexOf(typeName), modifiers); }
  }
  const keywords = new Set([...(languageInfo.keywords || []), 'bring', 'launch', 'say', 'contract', 'meets', 'extern', 'native', 'async', 'await']);
  const wordRegex = /[A-Za-z_][A-Za-z0-9_]*/g;
  for (const match of document.text.matchAll(wordRegex)) {
    if (!keywords.has(match[0])) continue;
    const start = positionAt(document.text, match.index);
    entries.push({ line: start.line, char: start.character, length: match[0].length, type: tokenTypes.indexOf('keyword'), modifiers: 0 });
  }
  for (const match of document.text.matchAll(/"(?:\\.|[^"\\])*"/g)) {
    const start = positionAt(document.text, match.index);
    entries.push({ line: start.line, char: start.character, length: Math.max(1, match[0].split('\n')[0].length), type: tokenTypes.indexOf('string'), modifiers: 0 });
  }
  for (const match of document.text.matchAll(/\b\d+(?:\.\d+)?\b/g)) {
    const start = positionAt(document.text, match.index);
    entries.push({ line: start.line, char: start.character, length: match[0].length, type: tokenTypes.indexOf('number'), modifiers: 0 });
  }
  entries.sort((a, b) => a.line - b.line || a.char - b.char || a.type - b.type);
  const data = [];
  let prevLine = 0, prevChar = 0;
  for (const entry of entries) {
    const deltaLine = entry.line - prevLine;
    const deltaStart = deltaLine === 0 ? entry.char - prevChar : entry.char;
    if (deltaLine < 0 || deltaStart < 0) continue;
    data.push(deltaLine, deltaStart, entry.length, entry.type, entry.modifiers);
    prevLine = entry.line; prevChar = entry.char;
  }
  return data;
}

function capabilities() {
  return {
    textDocumentSync: { openClose: true, change: 2, save: { includeText: true } },
    completionProvider: { resolveProvider: false, triggerCharacters: ['.', '(', ':', '@'] },
    hoverProvider: true, definitionProvider: true, referencesProvider: true,
    renameProvider: { prepareProvider: true }, documentSymbolProvider: true,
    workspaceSymbolProvider: true, documentFormattingProvider: true,
    signatureHelpProvider: { triggerCharacters: ['(', ','] },
    semanticTokensProvider: { legend: { tokenTypes, tokenModifiers }, full: true },
    inlayHintProvider: true,
    codeActionProvider: true,
  };
}

function resolveSymbolAt(document, position) {
  const word = wordAt(document.text, position).word;
  if (!word) return null;
  const offset = offsetAt(document.text, position);
  const before = document.text.slice(Math.max(0, offset - word.length - 80), offset);
  const memberMatch = /([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)?$/.exec(before);
  if (memberMatch) {
    const index = indexDocument(document.uri, document.text);
    const receiverType = index.variableTypes.get(memberMatch[1]);
    if (receiverType) return findDefinition(word, receiverType);
  }
  return findDefinition(word);
}

function signatureForCall(document, position) {
  const offset = offsetAt(document.text, position);
  const before = document.text.slice(0, offset);
  const methodMatch = /([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*\(([^()]*)$/.exec(before);
  if (methodMatch) {
    const index = indexDocument(document.uri, document.text);
    const receiverType = index.variableTypes.get(methodMatch[1]);
    const def = receiverType ? findDefinition(methodMatch[2], receiverType) : null;
    if (def?.signature) return { entry: { signature: def.signature, documentation: `${receiverType}.${def.name}` }, args: methodMatch[3] };
  }
  const match = /([A-Za-z_][A-Za-z0-9_]*)\s*\(([^()]*)$/.exec(before);
  if (!match) return null;
  const entry = catalog().get(match[1]);
  return entry ? { entry, args: match[2] } : null;
}

function handleRequest(message) {
  const { id, method, params = {} } = message;
  if (method === 'initialize') {
    const candidate = params.rootUri || params.rootPath || (params.workspaceFolders && params.workspaceFolders[0]?.uri);
    if (candidate) rootPath = candidate.startsWith?.('file:') ? (uriToPath(candidate) || rootPath) : candidate;
    languageInfo = readLanguageInfo();
    semanticWorker.start();
    return response(id, { capabilities: capabilities(), serverInfo: { name: 'punpun-lsp', version: '0.5.0-beta' } });
  }
  if (method === 'shutdown') { shutdownRequested = true; semanticWorker.stop(); return response(id, null); }
  if (shutdownRequested) return errorResponse(id, -32600, 'server is shutting down');

  const uri = params.textDocument?.uri;
  const document = uri ? documents.get(uri) : null;

  if (method === 'textDocument/completion') return response(id, document ? completionItems(document, params.position) : []);
  if (method === 'textDocument/hover') {
    if (!document) return response(id, null);
    const word = wordAt(document.text, params.position);
    const builtin = catalog().get(word.word);
    const symbol = resolveSymbolAt(document, params.position);
    let value = null;
    if (symbol) {
      const detail = symbol.signature || (symbol.kind === 'field' ? `field ${symbol.name}: ${symbol.type}` : symbol.valueType ? `${symbol.name}: ${symbol.valueType}` : `${symbol.kind} ${symbol.name}`);
      value = `\`${detail}\``;
    } else if (builtin) value = `\`${builtin.signature}\`\n\n${builtin.documentation || ''}`;
    return response(id, value ? { contents: { kind: 'markdown', value }, range: { start: positionAt(document.text, word.start), end: positionAt(document.text, word.end) } } : null);
  }
  if (method === 'textDocument/definition') {
    if (!document) return response(id, null);
    const found = resolveSymbolAt(document, params.position);
    return response(id, found ? { uri: found.uri, range: found.range } : null);
  }
  if (method === 'textDocument/references') {
    if (!document) return response(id, []);
    const word = wordAt(document.text, params.position).word;
    const locations = [];
    for (const candidate of allDocuments()) locations.push(...occurrences(candidate.uri, candidate.text, word));
    return response(id, locations);
  }
  if (method === 'textDocument/prepareRename') {
    if (!document) return response(id, null);
    const word = wordAt(document.text, params.position);
    const reserved = new Set([...(languageInfo.keywords || []), ...(languageInfo.types || [])]);
    const builtin = (languageInfo.builtins || []).some(item => item.name === word.word);
    if (!word.word || reserved.has(word.word) || builtin) return response(id, null);
    return response(id, { range: { start: positionAt(document.text, word.start), end: positionAt(document.text, word.end) }, placeholder: word.word });
  }
  if (method === 'textDocument/rename') {
    if (!document) return response(id, null);
    const word = wordAt(document.text, params.position).word;
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(params.newName || '')) return errorResponse(id, -32602, 'invalid PunPun identifier');
    const changes = {};
    for (const candidate of allDocuments()) {
      const edits = occurrences(candidate.uri, candidate.text, word).map(location => ({ range: location.range, newText: params.newName }));
      if (edits.length) changes[candidate.uri] = edits;
    }
    return response(id, { changes });
  }
  if (method === 'textDocument/documentSymbol') {
    if (!document) return response(id, []);
    return response(id, indexDocument(uri, document.text).symbols.map(symbol => ({ name: symbol.name, kind: symbolKind(symbol.kind), range: symbol.range, selectionRange: symbol.range, detail: symbol.signature || symbol.type || symbol.valueType || symbol.kind })));
  }
  if (method === 'workspace/symbol') {
    const query = String(params.query || '').toLowerCase();
    const symbols = [];
    for (const { index } of allIndexes()) for (const symbol of index.symbols) {
      if (!query || symbol.name.toLowerCase().includes(query)) symbols.push({ name: symbol.name, kind: symbolKind(symbol.kind), location: { uri: symbol.uri, range: symbol.range } });
    }
    return response(id, symbols);
  }
  if (method === 'textDocument/formatting') {
    if (!document) return response(id, []);
    const formatted = formatText(document.text);
    if (formatted === document.text) return response(id, []);
    return response(id, [{ range: { start: { line: 0, character: 0 }, end: positionAt(document.text, document.text.length) }, newText: formatted }]);
  }
  if (method === 'textDocument/signatureHelp') {
    if (!document) return response(id, null);
    const found = signatureForCall(document, params.position);
    if (!found) return response(id, null);
    const open = found.entry.signature.indexOf('('), close = found.entry.signature.indexOf(')', open + 1);
    const raw = open >= 0 && close > open ? found.entry.signature.slice(open + 1, close).trim() : '';
    const parameters = raw ? raw.split(',').map(label => ({ label: label.trim() })) : [];
    return response(id, { signatures: [{ label: found.entry.signature, documentation: found.entry.documentation || '', parameters }], activeSignature: 0, activeParameter: Math.min((found.args.match(/,/g) || []).length, Math.max(0, parameters.length - 1)) });
  }
  if (method === 'textDocument/semanticTokens/full') return response(id, { data: document ? semanticTokenData(document) : [] });
  if (method === 'textDocument/inlayHint') {
    if (!document) return response(id, []);
    const index = indexDocument(uri, document.text);
    const hints = [];
    for (const symbol of index.symbols) if (symbol.kind === 'variable' && symbol.valueType && symbol.valueType !== '?') {
      hints.push({ position: symbol.range.end, label: `: ${symbol.valueType}`, kind: 1, paddingLeft: true });
    }
    return response(id, hints);
  }
  if (method === 'textDocument/codeAction') {
    const actions = [];
    for (const diagnostic of params.context?.diagnostics || []) {
      const help = diagnostic.data?.help;
      const typo = /did you mean [`']([^`']+)[`']/.exec(help || '');
      if (typo && document) actions.push({ title: `Change to '${typo[1]}'`, kind: 'quickfix', diagnostics: [diagnostic], edit: { changes: { [uri]: [{ range: diagnostic.range, newText: typo[1] }] } } });
      const unknown = /unknown (?:name|function) ['`]([A-Za-z_][A-Za-z0-9_]*)['`]/.exec(diagnostic.message || '');
      if (unknown && document) {
        const candidates = (languageInfo.stdlib_symbols || []).filter(item => item.name === unknown[1] && item.module);
        if (candidates.length === 1) {
          const moduleName = candidates[0].module.replace(/\./g, '::');
          const already = new RegExp(`\\b(?:bring|import)\\s+${moduleName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\s*;`).test(document.text);
          if (!already) actions.push({
            title: `Import ${moduleName}`,
            kind: 'quickfix',
            diagnostics: [diagnostic],
            edit: { changes: { [uri]: [{ range: { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } }, newText: `bring ${moduleName};\n` }] } },
          });
        }
      }
    }
    return response(id, actions);
  }
  return errorResponse(id, -32601, `method not found: ${method}`);
}

function handleNotification(message) {
  const { method, params = {} } = message;
  if (method === 'exit') { semanticWorker.stop(); process.exit(shutdownRequested ? 0 : 1); }
  if (method === 'initialized') return;
  if (method === 'textDocument/didOpen') {
    const item = params.textDocument;
    documents.set(item.uri, { uri: item.uri, text: item.text, version: item.version || 0 });
    scheduleDiagnostics(item.uri, true); return;
  }
  if (method === 'textDocument/didChange') {
    const item = documents.get(params.textDocument.uri); if (!item) return;
    item.text = applyChanges(item, params.contentChanges || []); item.version = params.textDocument.version ?? item.version;
    scheduleDiagnostics(item.uri); return;
  }
  if (method === 'textDocument/didSave') {
    const item = documents.get(params.textDocument.uri);
    if (item && typeof params.text === 'string') item.text = params.text;
    if (item) scheduleDiagnostics(item.uri, true); return;
  }
  if (method === 'textDocument/didClose') {
    const uri = params.textDocument.uri; documents.delete(uri);
    if (diagnosticTimers.has(uri)) clearTimeout(diagnosticTimers.get(uri)); diagnosticTimers.delete(uri);
    notify('textDocument/publishDiagnostics', { uri, diagnostics: [] });
  }
}

function dispatch(message) {
  try { if (Object.prototype.hasOwnProperty.call(message, 'id')) handleRequest(message); else handleNotification(message); }
  catch (error) { if (Object.prototype.hasOwnProperty.call(message, 'id')) errorResponse(message.id, -32603, error?.message || String(error)); }
}
function consume() {
  for (;;) {
    const headerEnd = inputBuffer.indexOf('\r\n\r\n'); if (headerEnd < 0) return;
    const header = inputBuffer.slice(0, headerEnd).toString('ascii');
    const match = /(?:^|\r\n)Content-Length:\s*(\d+)/i.exec(header);
    if (!match) { inputBuffer = inputBuffer.slice(headerEnd + 4); continue; }
    const length = Number(match[1]); if (inputBuffer.length < headerEnd + 4 + length) return;
    const bodyStart = headerEnd + 4;
    const body = inputBuffer.slice(bodyStart, bodyStart + length).toString('utf8');
    inputBuffer = inputBuffer.slice(bodyStart + length);
    try { dispatch(JSON.parse(body)); } catch (_) {}
  }
}
process.stdin.on('data', chunk => { inputBuffer = Buffer.concat([inputBuffer, chunk]); consume(); });
process.stdin.on('end', () => { semanticWorker.stop(); process.exit(shutdownRequested ? 0 : 1); });
process.stdin.resume();
