// Usage: node reference.cjs CAPTURE_ROOT OUTPUT_DIR
const fs = require('node:fs/promises');
const path = require('node:path');
const puppeteer = require('puppeteer-core');
const packaged = require('@sparticuz/chromium');
const {pages} = require('./pages.json');
const [root, out] = process.argv.slice(2);
const origin = 'http://snapshot.test';
const mime = {html:'text/html; charset=utf-8',css:'text/css',js:'text/javascript',svg:'image/svg+xml',png:'image/png',woff2:'font/woff2',json:'application/json'};
const wait = ms => new Promise(r => setTimeout(r, ms));
async function run() {
  await fs.mkdir(out,{recursive:true});
  const browser = await puppeteer.launch({executablePath: await packaged.executablePath(), args: packaged.args.filter(a => a !== '--single-process'),headless:true});
  const results = [];
  try {
    for (const [i,t] of pages.entries()) {
      const ctx = await browser.createBrowserContext();
      const page = await ctx.newPage();
      const name = path.join(out, `ref-${i+1}`);
      const dir = path.resolve(root,t.source);
      const row = {id:i+1,passed:false}; results.push(row);
      try {
        await page.setViewport({width:800,height:600,deviceScaleFactor:1});
        await page.setRequestInterception(true);
        page.on('request', async req => {
          const u = new URL(req.url());
          if (u.protocol === 'data:') return req.continue();
          if (u.origin !== origin) return req.abort();
          try {
            const file = path.resolve(dir,'.'+decodeURIComponent(u.pathname)+(u.pathname.endsWith('/')?'index.html':''));
            if (!file.startsWith(dir+path.sep)) return req.abort();
            const body = await fs.readFile(file);
            await req.respond({status:200,contentType:mime[file.split('.').pop()]||'application/octet-stream',body});
          } catch { if (!req.isInterceptResolutionHandled()) await req.respond({status:404,body:''}); }
        });
        await page.goto(`${origin}/${t.path}${t.path.endsWith('/')?'index.html':''}`,{waitUntil:'networkidle2',timeout:15000});
        await page.screenshot({path:name+'.png'});
        if (t.heading) {
          if (!(await page.$eval("#contents details",e=>e.open))) await page.click('#contents summary');
          await page.click(t.click);
          await wait(400); const rect = await page.$eval('#'+t.heading+' :is(h2,h3,h4)', e=>e.getBoundingClientRect().top);
          row.observed=[page.url(),rect]; row.passed = page.url().endsWith('#'+t.heading) && rect >= -1 && rect < 600;
        } else if(t.manual) {
          await Promise.all([page.waitForNavigation(), page.click(t.click)]);
          row.passed = page.url().endsWith('/'+t.manual+'.html') && (await page.$eval('h1',e=>e.innerText)).includes(t.manual+'(1)');
        } else if(t.type) {
          await page.click(t.click); await page.keyboard.down('Control'); await page.keyboard.press('a'); await page.keyboard.up('Control'); await page.keyboard.type(t.type);
          await page.$eval(t.read,e=>e.scrollIntoView({block:'nearest'}));
          const result = await page.$eval(t.read,e=>({text:e.value??e.innerText,visible:e.checkVisibility() && e.getBoundingClientRect().top<600 && e.getBoundingClientRect().bottom>0}));
          row.observed=result.text; row.passed=result.visible && (t.read==='#valid'?result.text.includes(t.expect):result.text===t.expect) && (await page.$eval(t.click,e=>e.value??e.innerText))===t.type;
        } else if(t.game==='2048') {
          const board=()=>page.$$eval('.tile',els=>els.map(e=>e.className).sort().join(';'));
          const before=await board();
          for(const key of ['ArrowLeft','ArrowUp','ArrowRight','ArrowDown']) {await page.keyboard.press(key);await wait(200);if(before && before!==await board()){row.passed=true;break;}}
        } else {
          await page.click('#startBtn'); await wait(1000);
          row.passed=await page.$eval('#pauseBtn',e=>e.checkVisibility() && e.getBoundingClientRect().width>0) && await page.evaluate(()=>window.gameState===1);
        }
        await page.screenshot({path:name+'-after.png'});
      } catch (e) {row.error=String(e);} finally {await ctx.close();}
      console.log(i+1, row.passed, row.error??row.observed??'');
    }
    await fs.writeFile(path.join(out,'reference.json'),JSON.stringify({browser:await browser.version(),results},null,2));
  } finally {await browser.close();}
}
run().catch(e=>{console.error(e);process.exitCode=1;});
