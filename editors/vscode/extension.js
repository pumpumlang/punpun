'use strict';

// PunPun VS Code client. Editor intelligence is served by the standalone PunPun
// language server, which in turn uses the compiler's persistent semantic worker.
// This file intentionally contains no second parser/type checker.

const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');
const { pathToFileURL } = require('url');

const LANGUAGE_SELECTOR = { language: 'punpun', scheme: 'file' };
const TOKEN_TYPES = ['namespace','type','class','interface','struct','typeParameter','parameter','variable','property','enumMember','function','method','keyword','string','number','operator'];
const TOKEN_MODIFIERS = ['declaration','definition','readonly','static','deprecated','abstract','async','modification','documentation','defaultLibrary'];

function projectRoot(document) {
  const folder = document ? vscode.workspace.getWorkspaceFolder(document.uri) : vscode.workspace.workspaceFolders?.[0];
  return folder ? folder.uri.fsPath : (document ? path.dirname(document.uri.fsPath) : process.cwd());
}

function shellQuote(value) {
  value = String(value);
  if (process.platform === 'win32') return `"${value.replace(/"/g, '\\"')}"`;
  return `'${value.replace(/'/g, `'\\''`)}'`;
}

function toolCommand(document) {
  const configured = vscode.workspace.getConfiguration('punpun').get('toolPath', '');
  if (configured) return configured;
  const root = projectRoot(document);
  for (const name of ['pp', 'punpun']) {
    const candidate = path.join(root, name);
    if (fs.existsSync(candidate)) return candidate;
  }
  if (process.platform !== 'win32' && process.env.HOME) {
    const candidate = path.join(process.env.HOME, '.local', 'bin', 'pp');
    if (fs.existsSync(candidate)) return candidate;
  }
  return 'pp';
}

function compilerPath(document, extensionPath) {
  const configured = vscode.workspace.getConfiguration('punpun').get('compilerPath', '');
  if (configured) return configured;
  const executable = process.platform === 'win32' ? 'ppc.exe' : 'ppc';
  const candidates = [
    path.join(projectRoot(document), 'build', executable),
    path.resolve(extensionPath, '..', '..', 'build', executable),
  ];
  if (process.env.HOME) candidates.push(path.join(process.env.HOME, '.local', 'share', 'punpun', 'build', executable));
  for (const candidate of candidates) if (fs.existsSync(candidate)) return candidate;
  return executable;
}

function serverPath(extensionPath) {
  const configured = vscode.workspace.getConfiguration('punpun').get('languageServerPath', '');
  if (configured) return configured;
  const candidates = [
    path.join(extensionPath, 'server', 'server.js'),
    path.resolve(extensionPath, '..', '..', 'tooling', 'lsp', 'server.js'),
  ];
  if (process.env.HOME) candidates.push(path.join(process.env.HOME, '.local', 'share', 'punpun', 'tooling', 'lsp', 'server.js'));
  for (const candidate of candidates) if (fs.existsSync(candidate)) return candidate;
  return candidates[0];
}

function toLspPosition(position) { return { line: position.line, character: position.character }; }
function toLspRange(range) { return { start: toLspPosition(range.start), end: toLspPosition(range.end) }; }
function fromPosition(position) { return new vscode.Position(position.line, position.character); }
function fromRange(range) { return new vscode.Range(fromPosition(range.start), fromPosition(range.end)); }
function uri(value) { return vscode.Uri.parse(value); }

function markdown(value) {
  if (value == null) return undefined;
  if (typeof value === 'string') return new vscode.MarkdownString(value);
  if (value.kind === 'markdown') return new vscode.MarkdownString(value.value || '');
  if (value.kind === 'plaintext') return new vscode.MarkdownString().appendText(value.value || '');
  if (Array.isArray(value)) return new vscode.MarkdownString(value.map(v => typeof v === 'string' ? v : v.value || '').join('\n\n'));
  return new vscode.MarkdownString(String(value.value || value));
}

class LspClient {
  constructor(context, diagnostics, output) {
    this.context = context;
    this.diagnostics = diagnostics;
    this.output = output;
    this.proc = null;
    this.buffer = Buffer.alloc(0);
    this.nextId = 1;
    this.pending = new Map();
    this.ready = false;
    this.readyPromise = null;
  }

  async start() {
    if (this.readyPromise) return this.readyPromise;
    this.readyPromise = this._start();
    return this.readyPromise;
  }

  async _start() {
    const active = vscode.window.activeTextEditor?.document;
    const server = serverPath(this.context.extensionPath);
    if (!fs.existsSync(server)) throw new Error(`PunPun language server not found: ${server}`);
    const env = { ...process.env, PUNPUN_PPC: compilerPath(active, this.context.extensionPath) };
    this.output.appendLine(`Starting PunPun language server: ${server}`);
    this.output.appendLine(`Compiler: ${env.PUNPUN_PPC}`);
    this.proc = childProcess.spawn(process.execPath, [server], { stdio: ['pipe','pipe','pipe'], windowsHide: true, env });
    this.proc.stdout.on('data', chunk => { this.buffer = Buffer.concat([this.buffer, chunk]); this.consume(); });
    this.proc.stderr.on('data', chunk => this.output.append(chunk.toString('utf8')));
    this.proc.on('error', error => this.fail(error));
    this.proc.on('exit', (code, signal) => {
      this.ready = false;
      if (code !== 0 && code != null) this.output.appendLine(`PunPun language server exited: ${code ?? signal}`);
      this.fail(new Error(`PunPun language server exited (${code ?? signal ?? 'unknown'})`));
    });

    const root = vscode.workspace.workspaceFolders?.[0]?.uri;
    const result = await this.request('initialize', {
      processId: process.pid,
      rootUri: root ? root.toString() : pathToFileURL(process.cwd()).toString(),
      capabilities: {
        textDocument: {
          publishDiagnostics: { relatedInformation: true, versionSupport: true },
          completion: { completionItem: { snippetSupport: true } },
          semanticTokens: { requests: { full: true }, tokenTypes: TOKEN_TYPES, tokenModifiers: TOKEN_MODIFIERS, formats: ['relative'] },
          inlayHint: { dynamicRegistration: false },
          codeAction: { codeActionLiteralSupport: { codeActionKind: { valueSet: ['quickfix'] } } },
        },
        workspace: { workspaceFolders: true },
      },
      workspaceFolders: vscode.workspace.workspaceFolders?.map(folder => ({ uri: folder.uri.toString(), name: folder.name })) || null,
    }, false);
    this.capabilities = result?.capabilities || {};
    this.notify('initialized', {});
    this.ready = true;
    for (const document of vscode.workspace.textDocuments) if (document.languageId === 'punpun' && document.uri.scheme === 'file') this.open(document);
  }

  fail(error) {
    for (const { reject } of this.pending.values()) reject(error);
    this.pending.clear();
  }

  consume() {
    for (;;) {
      const headerEnd = this.buffer.indexOf(Buffer.from('\r\n\r\n'));
      if (headerEnd < 0) return;
      const header = this.buffer.slice(0, headerEnd).toString('ascii');
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) { this.buffer = this.buffer.slice(headerEnd + 4); continue; }
      const length = Number(match[1]);
      const bodyStart = headerEnd + 4;
      if (this.buffer.length < bodyStart + length) return;
      const body = this.buffer.slice(bodyStart, bodyStart + length).toString('utf8');
      this.buffer = this.buffer.slice(bodyStart + length);
      let message;
      try { message = JSON.parse(body); } catch (_) { continue; }
      if (Object.prototype.hasOwnProperty.call(message, 'id')) {
        const pending = this.pending.get(message.id);
        if (!pending) continue;
        this.pending.delete(message.id);
        if (message.error) pending.reject(new Error(message.error.message || 'LSP error'));
        else pending.resolve(message.result);
      } else if (message.method) this.notification(message.method, message.params || {});
    }
  }

  notification(method, params) {
    if (method !== 'textDocument/publishDiagnostics') return;
    const target = uri(params.uri);
    const document = vscode.workspace.textDocuments.find(doc => doc.uri.toString() === params.uri);
    if (document && params.version != null && params.version !== document.version) return;
    const converted = (params.diagnostics || []).map(item => {
      const severity = ({1:vscode.DiagnosticSeverity.Error,2:vscode.DiagnosticSeverity.Warning,3:vscode.DiagnosticSeverity.Information,4:vscode.DiagnosticSeverity.Hint})[item.severity] || vscode.DiagnosticSeverity.Error;
      const diagnostic = new vscode.Diagnostic(fromRange(item.range), item.message || '', severity);
      diagnostic.source = item.source || 'PunPun';
      if (item.code != null) diagnostic.code = item.code;
      if (item.tags) diagnostic.tags = item.tags;
      // Keep machine-readable help for code actions while also exposing it on hover.
      diagnostic._punpunData = item.data;
      return diagnostic;
    });
    this.diagnostics.set(target, converted);
  }

  send(message) {
    if (!this.proc?.stdin?.writable) throw new Error('PunPun language server is not running');
    const body = Buffer.from(JSON.stringify({ jsonrpc: '2.0', ...message }), 'utf8');
    this.proc.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
    this.proc.stdin.write(body);
  }

  request(method, params = {}, ensureStarted = true) {
    if (ensureStarted && !this.ready) return this.start().then(() => this.request(method, params, false));
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      try { this.send({ id, method, params }); }
      catch (error) { this.pending.delete(id); reject(error); }
    });
  }
  notify(method, params = {}) { try { this.send({ method, params }); } catch (error) { this.output.appendLine(error.message); } }

  open(document) {
    if (!this.ready || document.languageId !== 'punpun' || document.uri.scheme !== 'file') return;
    this.notify('textDocument/didOpen', { textDocument: { uri: document.uri.toString(), languageId: 'punpun', version: document.version, text: document.getText() } });
  }
  change(document) {
    if (!this.ready || document.languageId !== 'punpun' || document.uri.scheme !== 'file') return;
    this.notify('textDocument/didChange', { textDocument: { uri: document.uri.toString(), version: document.version }, contentChanges: [{ text: document.getText() }] });
  }
  save(document) {
    if (!this.ready || document.languageId !== 'punpun' || document.uri.scheme !== 'file') return;
    this.notify('textDocument/didSave', { textDocument: { uri: document.uri.toString() }, text: document.getText() });
  }
  close(document) {
    if (!this.ready || document.languageId !== 'punpun' || document.uri.scheme !== 'file') return;
    this.notify('textDocument/didClose', { textDocument: { uri: document.uri.toString() } });
    this.diagnostics.delete(document.uri);
  }

  async stop() {
    if (!this.proc) return;
    try { await this.request('shutdown', {}, false); } catch (_) {}
    try { this.notify('exit', {}); } catch (_) {}
    try { this.proc.stdin.end(); } catch (_) {}
    this.proc = null;
  }
}

function textDocumentParams(document, position) {
  const params = { textDocument: { uri: document.uri.toString() } };
  if (position) params.position = toLspPosition(position);
  return params;
}

function completionKind(kind) {
  const values = [undefined,vscode.CompletionItemKind.Text,vscode.CompletionItemKind.Method,vscode.CompletionItemKind.Function,vscode.CompletionItemKind.Constructor,vscode.CompletionItemKind.Field,vscode.CompletionItemKind.Variable,vscode.CompletionItemKind.Class,vscode.CompletionItemKind.Interface,vscode.CompletionItemKind.Module,vscode.CompletionItemKind.Property,vscode.CompletionItemKind.Unit,vscode.CompletionItemKind.Value,vscode.CompletionItemKind.Enum,vscode.CompletionItemKind.Keyword,vscode.CompletionItemKind.Snippet,vscode.CompletionItemKind.Color,vscode.CompletionItemKind.File,vscode.CompletionItemKind.Reference,vscode.CompletionItemKind.Folder,vscode.CompletionItemKind.EnumMember,vscode.CompletionItemKind.Constant,vscode.CompletionItemKind.Struct,vscode.CompletionItemKind.Event,vscode.CompletionItemKind.Operator,vscode.CompletionItemKind.TypeParameter];
  return values[kind] || vscode.CompletionItemKind.Text;
}

function convertCompletion(item) {
  const result = new vscode.CompletionItem(item.label, completionKind(item.kind));
  result.detail = item.detail;
  result.documentation = markdown(item.documentation);
  if (item.sortText) result.sortText = item.sortText;
  if (item.filterText) result.filterText = item.filterText;
  if (item.insertTextFormat === 2 && item.insertText) result.insertText = new vscode.SnippetString(item.insertText);
  else if (item.insertText) result.insertText = item.insertText;
  if (item.textEdit) result.textEdit = vscode.TextEdit.replace(fromRange(item.textEdit.range), item.textEdit.newText);
  if (item.additionalTextEdits) result.additionalTextEdits = item.additionalTextEdits.map(edit => vscode.TextEdit.replace(fromRange(edit.range), edit.newText));
  return result;
}

function convertWorkspaceEdit(edit) {
  if (!edit) return undefined;
  const result = new vscode.WorkspaceEdit();
  for (const [rawUri, edits] of Object.entries(edit.changes || {})) for (const item of edits) result.replace(uri(rawUri), fromRange(item.range), item.newText);
  return result;
}

function runTool(action, extra = []) {
  const document = vscode.window.activeTextEditor?.document;
  if (!document || document.languageId !== 'punpun') return vscode.window.showErrorMessage('Open a PunPun .pp file first.');
  const terminal = vscode.window.createTerminal({ name: `PunPun ${action}`, cwd: projectRoot(document) });
  terminal.show(true);
  const args = [action, ...extra].map(shellQuote).join(' ');
  terminal.sendText(`${shellQuote(toolCommand(document))} ${args}`);
}

function registerProviders(context, client) {
  context.subscriptions.push(vscode.languages.registerCompletionItemProvider(LANGUAGE_SELECTOR, {
    async provideCompletionItems(document, position) {
      const value = await client.request('textDocument/completion', textDocumentParams(document, position));
      const items = Array.isArray(value) ? value : value?.items || [];
      return items.map(convertCompletion);
    }
  }, '.', ':', '@'));

  context.subscriptions.push(vscode.languages.registerHoverProvider(LANGUAGE_SELECTOR, {
    async provideHover(document, position) {
      const value = await client.request('textDocument/hover', textDocumentParams(document, position));
      return value ? new vscode.Hover(markdown(value.contents), value.range ? fromRange(value.range) : undefined) : undefined;
    }
  }));

  context.subscriptions.push(vscode.languages.registerDefinitionProvider(LANGUAGE_SELECTOR, {
    async provideDefinition(document, position) {
      const value = await client.request('textDocument/definition', textDocumentParams(document, position));
      if (!value) return undefined;
      const values = Array.isArray(value) ? value : [value];
      return values.map(item => new vscode.Location(uri(item.uri), fromRange(item.range)));
    }
  }));

  context.subscriptions.push(vscode.languages.registerReferenceProvider(LANGUAGE_SELECTOR, {
    async provideReferences(document, position, options) {
      const value = await client.request('textDocument/references', { ...textDocumentParams(document, position), context: { includeDeclaration: options.includeDeclaration } });
      return (value || []).map(item => new vscode.Location(uri(item.uri), fromRange(item.range)));
    }
  }));

  context.subscriptions.push(vscode.languages.registerRenameProvider(LANGUAGE_SELECTOR, {
    async provideRenameEdits(document, position, newName) {
      return convertWorkspaceEdit(await client.request('textDocument/rename', { ...textDocumentParams(document, position), newName }));
    }
  }));

  context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider(LANGUAGE_SELECTOR, {
    async provideDocumentSymbols(document) {
      return (await client.request('textDocument/documentSymbol', textDocumentParams(document)) || []).map(item => new vscode.DocumentSymbol(item.name, item.detail || '', item.kind, fromRange(item.range), fromRange(item.selectionRange || item.range)));
    }
  }));

  context.subscriptions.push(vscode.languages.registerWorkspaceSymbolProvider({
    async provideWorkspaceSymbols(query) {
      return (await client.request('workspace/symbol', { query }) || []).map(item => new vscode.SymbolInformation(item.name, item.kind, item.containerName || '', new vscode.Location(uri(item.location.uri), fromRange(item.location.range))));
    }
  }));

  context.subscriptions.push(vscode.languages.registerDocumentFormattingEditProvider(LANGUAGE_SELECTOR, {
    async provideDocumentFormattingEdits(document, options) {
      const edits = await client.request('textDocument/formatting', { textDocument: { uri: document.uri.toString() }, options: { tabSize: options.tabSize, insertSpaces: options.insertSpaces } });
      return (edits || []).map(item => vscode.TextEdit.replace(fromRange(item.range), item.newText));
    }
  }));

  context.subscriptions.push(vscode.languages.registerSignatureHelpProvider(LANGUAGE_SELECTOR, {
    async provideSignatureHelp(document, position) {
      const value = await client.request('textDocument/signatureHelp', textDocumentParams(document, position));
      if (!value) return undefined;
      const result = new vscode.SignatureHelp();
      result.activeSignature = value.activeSignature || 0;
      result.activeParameter = value.activeParameter || 0;
      result.signatures = (value.signatures || []).map(item => {
        const sig = new vscode.SignatureInformation(item.label, markdown(item.documentation));
        sig.parameters = (item.parameters || []).map(param => new vscode.ParameterInformation(param.label, markdown(param.documentation)));
        return sig;
      });
      return result;
    }
  }, '(', ','));

  const legend = new vscode.SemanticTokensLegend(TOKEN_TYPES, TOKEN_MODIFIERS);
  context.subscriptions.push(vscode.languages.registerDocumentSemanticTokensProvider(LANGUAGE_SELECTOR, {
    async provideDocumentSemanticTokens(document) {
      const value = await client.request('textDocument/semanticTokens/full', textDocumentParams(document));
      return new vscode.SemanticTokens(new Uint32Array(value?.data || []));
    }
  }, legend));

  context.subscriptions.push(vscode.languages.registerInlayHintsProvider(LANGUAGE_SELECTOR, {
    async provideInlayHints(document, range) {
      const value = await client.request('textDocument/inlayHint', { textDocument: { uri: document.uri.toString() }, range: toLspRange(range) });
      return (value || []).map(item => {
        const hint = new vscode.InlayHint(fromPosition(item.position), typeof item.label === 'string' ? item.label : item.label.map(p => p.value).join(''), item.kind === 2 ? vscode.InlayHintKind.Parameter : vscode.InlayHintKind.Type);
        hint.paddingLeft = item.paddingLeft; hint.paddingRight = item.paddingRight; return hint;
      });
    }
  }));

  context.subscriptions.push(vscode.languages.registerCodeActionsProvider(LANGUAGE_SELECTOR, {
    async provideCodeActions(document, range, contextInfo) {
      const diagnostics = contextInfo.diagnostics.map(d => ({ range: toLspRange(d.range), severity: d.severity + 1, code: typeof d.code === 'object' ? d.code.value : d.code, source: d.source, message: d.message, data: d._punpunData }));
      const actions = await client.request('textDocument/codeAction', { textDocument: { uri: document.uri.toString() }, range: toLspRange(range), context: { diagnostics } });
      return (actions || []).map(item => {
        const action = new vscode.CodeAction(item.title, vscode.CodeActionKind.QuickFix);
        action.edit = convertWorkspaceEdit(item.edit); action.isPreferred = item.isPreferred; return action;
      });
    }
  }, { providedCodeActionKinds: [vscode.CodeActionKind.QuickFix] }));
}

async function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection('punpun');
  const output = vscode.window.createOutputChannel('PunPun');
  const client = new LspClient(context, diagnostics, output);
  context.subscriptions.push(diagnostics, output, { dispose: () => client.stop() });

  registerProviders(context, client);
  context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(document => client.open(document)));
  context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(event => client.change(event.document)));
  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(document => client.save(document)));
  context.subscriptions.push(vscode.workspace.onDidCloseTextDocument(document => client.close(document)));

  context.subscriptions.push(vscode.commands.registerCommand('punpun.build', () => runTool('build')));
  context.subscriptions.push(vscode.commands.registerCommand('punpun.run', () => runTool('run')));
  context.subscriptions.push(vscode.commands.registerCommand('punpun.check', () => runTool('check')));
  context.subscriptions.push(vscode.commands.registerCommand('punpun.format', () => vscode.commands.executeCommand('editor.action.formatDocument')));
  context.subscriptions.push(vscode.commands.registerCommand('punpun.test', () => runTool('test')));
  context.subscriptions.push(vscode.commands.registerCommand('punpun.buildWindows', () => runTool('build', ['--target', 'windows-x86_64'])));
  context.subscriptions.push(vscode.commands.registerCommand('punpun.restartLanguageServer', async () => {
    await client.stop(); client.ready = false; client.readyPromise = null; await client.start();
    vscode.window.showInformationMessage('PunPun language server restarted.');
  }));

  try { await client.start(); }
  catch (error) {
    output.appendLine(error.stack || String(error));
    vscode.window.showErrorMessage(`PunPun language server failed to start: ${error.message}`);
  }
}

async function deactivate() {}
module.exports = { activate, deactivate };
