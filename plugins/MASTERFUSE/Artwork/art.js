const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const shot = async (hash, clip, out, scale, transparent) => {
    const p = await b.newPage({ deviceScaleFactor: scale, viewport: { width: 520, height: 840 } });
    await p.goto('file://' + __dirname + '/mock.html#' + hash);
    await p.waitForSelector('body[data-ready="1"]'); await p.waitForTimeout(200);
    await p.screenshot({ path: out, clip, omitBackground: !!transparent });
    await p.close();
  };
  await shot('v=50&layer=bg', { x: 0, y: 0, width: 520, height: 680 }, 'art/background.png', 3, false);
  await shot('v=50&layer=knob', { x: 110, y: 190, width: 300, height: 300 }, 'art/knob.png', 4, true);
  await shot('v=50&layer=small&open=1', { x: 66, y: 706, width: 88, height: 88 }, 'art/knob_small.png', 4, true);
  await shot('v=50&layer=fuse&on=1', { x: 198, y: 584, width: 124, height: 36 }, 'art/fuse_on.png', 4, true);
  await shot('v=50&layer=fuse&on=0', { x: 198, y: 584, width: 124, height: 36 }, 'art/fuse_off.png', 4, true);
  await b.close();
})();
