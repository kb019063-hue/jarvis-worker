#!/usr/bin/env node
// Check the canvas/scroll fixture independently of Litebird, at the same
// 200x150 viewport and positions used by the BrowserShell unit suite. Requires
// @sparticuz/chromium, puppeteer-core and pngjs; see Phase 170 for versions.
const fs = require('node:fs');
const path = require('node:path');
const chromium = require('@sparticuz/chromium').default;
const puppeteer = require('puppeteer-core');
const { PNG } = require('pngjs');

const fixture = process.argv[2] ? path.resolve(process.argv[2]) :
  path.resolve(__dirname, '../../tests/render/canvas-scroll.html');
const steps = [
  { scroll: [0, 0], pixels: [
    [0, 0, [127, 127, 255]], [20, 20, [0, 255, 0]],
    [50, 20, [191, 63, 127]], [150, 20, [127, 127, 255]],
    [199, 149, [127, 127, 255]],
  ] },
  { scroll: [15, 300], pixels: [
    [0, 0, [127, 127, 255]], [15, 0, [0, 255, 0]],
    [50, 0, [191, 63, 127]], [150, 0, [127, 127, 255]],
    [199, 149, [127, 127, 255]],
  ] },
];

(async () => {
  const browser = await puppeteer.launch({
    args: chromium.args,
    executablePath: await chromium.executablePath(),
    headless: true,
    defaultViewport: { width: 200, height: 150, deviceScaleFactor: 1 },
  });
  let assertions = 0, failures = 0;
  try {
    const page = await browser.newPage();
    await page.setContent(fs.readFileSync(fixture, 'utf8'));
    for (const step of steps) {
      const actualScroll = await page.evaluate(([x, y]) => {
        window.scrollTo(x, y);
        return [window.scrollX, window.scrollY];
      }, step.scroll);
      ++assertions;
      if (actualScroll.some((n, i) => n !== step.scroll[i])) {
        ++failures;
        console.error(`FAIL scroll: got ${actualScroll}, expected ${step.scroll}`);
      }
      const image = PNG.sync.read(Buffer.from(await page.screenshot({ type: 'png' })));
      if (image.width !== 200 || image.height !== 150) throw new Error('Unexpected viewport size');
      for (const [x, y, expected] of step.pixels) {
        ++assertions;
        const offset = (y * image.width + x) * 4;
        const actual = Array.from(image.data.subarray(offset, offset + 4));
        if (actual.some((n, i) => n !== [...expected, 255][i])) {
          ++failures;
          console.error(`FAIL scroll ${step.scroll}: (${x},${y}) is ${actual}, expected ${expected},255`);
        }
      }
    }
    console.log(`${await browser.version()}: ${assertions - failures}/${assertions} scroll and pixel assertions match Chromium`);
  } finally {
    await browser.close();
  }
  if (failures) process.exitCode = 1;
})().catch(error => { console.error(error); process.exitCode = 1; });
