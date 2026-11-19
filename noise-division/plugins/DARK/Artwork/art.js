// Renders the artwork layers in art.html to ../Assets/*.png at 2x.  Run: node art.js  (needs Playwright)
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 520, height: 680 }, deviceScaleFactor: 2 });
  p.on('pageerror', e => console.log('ERR', e.message));
  const out = path.join(__dirname, '..', 'Assets');
  for (const [file, hash, opaque] of [['bg_light', 'layer=bg&dark=0', true], ['bg_dark', 'layer=bg&dark=1', true],
                                      ['moon_pale', 'layer=moon&red=0'], ['moon_blood', 'layer=moon&red=1'],
                                      ['candle', 'layer=candle'], ['knob_small', 'layer=small']]) {
    await p.goto('file://' + path.join(__dirname, 'art.html') + '#' + hash); await p.reload();
    await p.waitForSelector('body[data-ready="1"]', { timeout: 15000 });
    await p.locator('canvas').screenshot({ path: path.join(out, file + '.png'), omitBackground: !opaque });
  }
  await b.close();
})();
