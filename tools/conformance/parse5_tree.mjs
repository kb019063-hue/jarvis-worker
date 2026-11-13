// Prints the tree parse5 builds for a page, in the format of the html5lib
// tree-construction tests, which is also what `litebird --dump-dom-webplatform`
// prints. Started by tools/conformance/diff_trees.py and of no use on its own.
//
//     node parse5_tree.mjs <directory in which parse5 is installed>
//
// Reads one request on each line of standard input, writes the tree to
// <path>.tree and answers with a line saying "done", so that one process
// serves any number of pages. A request is a JSON object:
//
//     {"path": "page.html"}                             a document, scripting enabled
//     {"path": "page.html", "scripting": false}         the same with scripting disabled
//     {"path": "page.html", "context": "svg title"}     a fragment, as innerHTML of that element
//
// The context is written as in the corpus: a tag name, with "svg " or "math "
// before it for an element of those namespaces.
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const installed = createRequire(path.resolve(process.argv[2], 'package.json'));
const { parse, parseFragment, defaultTreeAdapter } = await import(pathToFileURL(installed.resolve('parse5')).href);

const HTML = 'http://www.w3.org/1999/xhtml';
const SVG = 'http://www.w3.org/2000/svg';
const MATHML = 'http://www.w3.org/1998/Math/MathML';
const ELEMENT_PREFIX = { [SVG]: 'svg ', [MATHML]: 'math ' };
const ATTRIBUTE_PREFIX = {
  'http://www.w3.org/1999/xlink': 'xlink ',
  'http://www.w3.org/XML/1998/namespace': 'xml ',
  'http://www.w3.org/2000/xmlns/': 'xmlns ',
};

function dump(node, depth, lines) {
  const indent = '| ' + '  '.repeat(depth);
  if (node.nodeName === '#documentType') {
    const ids = node.publicId || node.systemId ? ` "${node.publicId}" "${node.systemId}"` : '';
    lines.push(`${indent}<!DOCTYPE ${node.name}${ids}>`);
    return;
  }
  if (node.nodeName === '#text') { lines.push(`${indent}"${node.value}"`); return; }
  if (node.nodeName === '#comment') { lines.push(`${indent}<!-- ${node.data} -->`); return; }

  lines.push(`${indent}<${ELEMENT_PREFIX[node.namespaceURI] || ''}${node.tagName}>`);
  const attributes = node.attrs.map((a) => [(ATTRIBUTE_PREFIX[a.namespace] || '') + a.name, a.value]);
  attributes.sort((x, y) => (x[0] < y[0] ? -1 : x[0] > y[0] ? 1 : 0));
  for (const [name, value] of attributes) lines.push(`${indent}  ${name}="${value}"`);
  if (node.tagName === 'template' && node.namespaceURI === HTML) {
    lines.push(`${indent}  content`);
    for (const child of node.content.childNodes) dump(child, depth + 2, lines);
  }
  for (const child of node.childNodes) dump(child, depth + 1, lines);
}

function contextElement(name) {
  if (name.startsWith('svg ')) return defaultTreeAdapter.createElement(name.slice(4), SVG, []);
  if (name.startsWith('math ')) return defaultTreeAdapter.createElement(name.slice(5), MATHML, []);
  return defaultTreeAdapter.createElement(name, HTML, []);
}

for await (const line of readline.createInterface({ input: process.stdin })) {
  if (!line) continue;
  const request = JSON.parse(line);
  const source = fs.readFileSync(request.path, 'utf8');
  const options = { scriptingEnabled: request.scripting !== false };
  const root = request.context ? parseFragment(contextElement(request.context), source, options) : parse(source, options);
  const lines = [];
  for (const child of root.childNodes) dump(child, 0, lines);
  fs.writeFileSync(request.path + '.tree', lines.length ? lines.join('\n') + '\n' : '');
  process.stdout.write('done\n');
}
