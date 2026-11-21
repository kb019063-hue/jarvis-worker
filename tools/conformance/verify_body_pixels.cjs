#!/usr/bin/env node
// Independently check the body-origin reference pixels with Chromium.
// Requires @sparticuz/chromium, puppeteer-core and pngjs; see Phase 152.
const fs = require('node:fs');
const path = require('node:path');
const chromium = require('@sparticuz/chromium').default;
const puppeteer = require('puppeteer-core');
const { PNG } = require('pngjs');

const corpus = process.argv[2] ? path.resolve(process.argv[2]) :
  path.resolve(__dirname, '../../tests/render/body-origin.json');

function check(image, assertion) {
  if (assertion.kind !== 'pixel' && assertion.kind !== 'rect') {
    throw new Error(`Unsupported assertion kind: ${assertion.kind}`);
  }
  if (!/^#[0-9a-f]{6}$/i.test(assertion.colour)) {
    throw new Error(`Unsupported colour: ${assertion.colour}`);
  }
  const expected = [1, 3, 5].map(i => parseInt(assertion.colour.slice(i, i + 2), 16));
  expected.push(255);
  const w = assertion.kind === 'pixel' ? 1 : assertion.width;
  const h = assertion.kind === 'pixel' ? 1 : assertion.height;
  const x = assertion.x, y = assertion.y;
  if (![x, y, w, h].every(Number.isInteger) || x < 0 || y < 0 || w <= 0 || h <= 0 ||
      x + w > image.width || y + h > image.height) {
    throw new Error(`Invalid assertion rectangle: ${JSON.stringify(assertion)}`);
  }
  for (let yy = y; yy < y + h; ++yy) {
    for (let xx = x; xx < x + w; ++xx) {
      const offset = (yy * image.width + xx) * 4;
      const actual = Array.from(image.data.subarray(offset, offset + 4));
      if (!actual.every((channel, i) => channel === expected[i])) {
        return `(${xx},${yy}) is ${actual.join(',')}, expected ${expected.join(',')}`;
      }
    }
  }
  return null;
}

(async () => {
  const tests = JSON.parse(fs.readFileSync(corpus, 'utf8')).tests;
  const browser = await puppeteer.launch({
    args: chromium.args,
    executablePath: await chromium.executablePath(),
    headless: true,
    defaultViewport: { width: 800, height: 600, deviceScaleFactor: 1 },
  });
  let count = 0, failures = 0;
  try {
    const page = await browser.newPage();
    for (const test of tests) {
      await page.setContent(test.html);
      const image = PNG.sync.read(Buffer.from(await page.screenshot({ type: 'png' })));
      if (image.width !== 800 || image.height !== 600) throw new Error('Unexpected viewport size');
      for (const assertion of test.assert) {
        ++count;
        const error = check(image, assertion);
        if (error) {
          ++failures;
          console.error(`FAIL ${test.name}: ${assertion.describe}: ${error}`);
        }
      }
    }
    console.log(`${await browser.version()}: ${count - failures}/${count} assertions across ${tests.length} pages match Chromium`);
  } finally {
    await browser.close();
  }
  if (failures) process.exitCode = 1;
})().catch(error => { console.error(error); process.exitCode = 1; });
