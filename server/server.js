import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { execFile, execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const REPO_ROOT = path.resolve(__dirname, '..');
const LITEBIRD_BIN = path.join(REPO_ROOT, 'build', 'litebird');

const PORT = 3000;
const HOST = '0.0.0.0';

function getExamples() {
    const examplesDir = path.join(REPO_ROOT, 'examples');
    if (!fs.existsSync(examplesDir)) return {};
    const files = fs.readdirSync(examplesDir).filter(f => f.endsWith('.html'));
    const examples = {};
    for (const f of files) {
        examples[f] = fs.readFileSync(path.join(examplesDir, f), 'utf-8');
    }
    return examples;
}

function runLitebird(htmlContent) {
    const tmpHtml = path.join('/tmp', `litebird_tmp_${Date.now()}_${Math.random().toString(36).slice(2)}.html`);
    const tmpBmp = path.join('/tmp', `litebird_out_${Date.now()}_${Math.random().toString(36).slice(2)}.bmp`);
    fs.writeFileSync(tmpHtml, htmlContent, 'utf-8');

    try {
        // 1. Dump DOM
        const domOut = execFileSync(LITEBIRD_BIN, ['--dump-dom', tmpHtml], { encoding: 'utf-8', timeout: 5000 });

        // 2. Dump CSS
        const cssOut = execFileSync(LITEBIRD_BIN, ['--dump-css', tmpHtml], { encoding: 'utf-8', timeout: 5000 });

        // 3. Dump Layout
        const layoutOut = execFileSync(LITEBIRD_BIN, ['--dump-layout', tmpHtml], { encoding: 'utf-8', timeout: 5000 });

        // 4. Dump Paint Commands
        const paintOut = execFileSync(LITEBIRD_BIN, ['--dump-paint', tmpHtml], { encoding: 'utf-8', timeout: 5000 });

        // 5. Dump Bytecode & JS execution
        let bytecodeOut = '';
        try {
            bytecodeOut = execFileSync(LITEBIRD_BIN, ['--dump-bytecode', tmpHtml], { encoding: 'utf-8', timeout: 5000 });
        } catch (e) {
            bytecodeOut = e.stdout || e.message;
        }

        // 6. Native 2D Rasterizer BMP
        execFileSync(LITEBIRD_BIN, ['--render-bmp', tmpBmp, tmpHtml], { encoding: 'utf-8', timeout: 5000 });
        let bmpBase64 = '';
        if (fs.existsSync(tmpBmp)) {
            const bmpBuf = fs.readFileSync(tmpBmp);
            bmpBase64 = `data:image/bmp;base64,${bmpBuf.toString('base64')}`;
            fs.unlinkSync(tmpBmp);
        }

        fs.unlinkSync(tmpHtml);

        return {
            dom: domOut.replace(/\x1B\[[0-9;]*[mK]/g, ''),
            css: cssOut.replace(/\x1B\[[0-9;]*[mK]/g, ''),
            layout: layoutOut.replace(/\x1B\[[0-9;]*[mK]/g, ''),
            paint: paintOut.replace(/\x1B\[[0-9;]*[mK]/g, ''),
            bytecode: bytecodeOut.replace(/\x1B\[[0-9;]*[mK]/g, ''),
            bmpBase64
        };
    } catch (err) {
        if (fs.existsSync(tmpHtml)) fs.unlinkSync(tmpHtml);
        if (fs.existsSync(tmpBmp)) fs.unlinkSync(tmpBmp);
        throw err;
    }
}

const server = http.createServer((req, res) => {
    // CORS headers
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }

    const url = new URL(req.url, `http://${req.headers.host}`);

    if (url.pathname === '/api/examples' && req.method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(getExamples()));
        return;
    }

    if (url.pathname === '/api/render' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => { body += chunk; });
        req.on('end', () => {
            try {
                const data = JSON.parse(body || '{}');
                const html = data.html || '';
                const result = runLitebird(html);
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify(result));
            } catch (err) {
                res.writeHead(500, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: err.message }));
            }
        });
        return;
    }

    // Serve static files
    let filePath = path.join(__dirname, 'public', url.pathname === '/' ? 'index.html' : url.pathname);
    if (!fs.existsSync(filePath)) {
        filePath = path.join(__dirname, 'public', 'index.html');
    }

    const ext = path.extname(filePath);
    const contentType = ext === '.html' ? 'text/html' :
                        ext === '.css'  ? 'text/css' :
                        ext === '.js'   ? 'text/javascript' :
                        'application/octet-stream';

    fs.readFile(filePath, (err, content) => {
        if (err) {
            res.writeHead(404);
            res.end('Not Found');
            return;
        }
        res.writeHead(200, { 'Content-Type': contentType });
        res.end(content);
    });
});

server.listen(PORT, HOST, () => {
    console.log(`Litebird Workbench dev server listening on http://${HOST}:${PORT}`);
});
