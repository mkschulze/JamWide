import { test, expect, type Page } from '@playwright/test';
import { MockPluginServer } from './mock-ws-server';

test.use({
  permissions: ['camera', 'microphone'],
  launchOptions: {
    args: ['--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream'],
  },
});

type VdoTarget = {
  name: string;
  baseUrl: string;
  companionPath: string;
};

const VDO_ORIGIN = 'https://vdo.ninja';
const BUFFER_SEQUENCE = [9000, 4000, 13000, 5000, 11000, 7000];
const VDO_TARGETS: VdoTarget[] = [
  { name: 'default-alpha', baseUrl: 'https://vdo.ninja/alpha/', companionPath: '/' },
  { name: 'production-override', baseUrl: 'https://vdo.ninja/', companionPath: '/?vdoProduction=1' },
];

let mock: MockPluginServer;

test.beforeEach(async () => {
  mock = new MockPluginServer(7170);
});

test.afterEach(async () => {
  await mock.close();
});

function vdoUrl(target: VdoTarget, query: string): string {
  return `${target.baseUrl}?${query}`;
}

async function callVdoIframe(page: Page, iframeSelector: string, payload: Record<string, unknown>, timeoutMs = 6000) {
  return page.evaluate(
    ({ iframeSelector, payload, timeoutMs }) =>
      new Promise<Record<string, unknown>>((resolve, reject) => {
        const iframe = document.querySelector(iframeSelector) as HTMLIFrameElement | null;
        if (!iframe?.contentWindow) {
          reject(new Error(`VDO iframe not found: ${iframeSelector}`));
          return;
        }

        const cib = `cib-${Date.now()}-${Math.random().toString(16).slice(2)}`;
        const timer = window.setTimeout(() => {
          window.removeEventListener('message', onMessage);
          reject(new Error(`Timed out waiting for VDO callback ${JSON.stringify(payload)}`));
        }, timeoutMs);

        function onMessage(event: MessageEvent) {
          if (event.source !== iframe?.contentWindow) return;
          const data = event.data;
          if (!data || typeof data !== 'object' || data.cib !== cib) return;
          window.clearTimeout(timer);
          window.removeEventListener('message', onMessage);
          resolve(data as Record<string, unknown>);
        }

        window.addEventListener('message', onMessage);
        iframe.contentWindow.postMessage({ ...payload, cib }, '*');
      }),
    { iframeSelector, payload, timeoutMs }
  );
}

async function callVdoTop(page: Page, payload: Record<string, unknown>, timeoutMs = 6000) {
  return page.evaluate(
    ({ payload, timeoutMs }) =>
      new Promise<Record<string, unknown>>((resolve, reject) => {
        const cib = `cib-${Date.now()}-${Math.random().toString(16).slice(2)}`;
        const timer = window.setTimeout(() => {
          window.removeEventListener('message', onMessage);
          reject(new Error(`Timed out waiting for top-level VDO callback ${JSON.stringify(payload)}`));
        }, timeoutMs);

        function onMessage(event: MessageEvent) {
          const data = event.data;
          if (!data || typeof data !== 'object' || data.cib !== cib) return;
          window.clearTimeout(timer);
          window.removeEventListener('message', onMessage);
          resolve(data as Record<string, unknown>);
        }

        window.addEventListener('message', onMessage);
        window.postMessage({ ...payload, cib }, '*');
      }),
    { payload, timeoutMs }
  );
}

async function postBufferDelayToIframe(page: Page, iframeSelector: string, delayMs: number, streamId?: string) {
  await page.evaluate(
    ({ iframeSelector, delayMs, streamId, targetOrigin }) => {
      const iframe = document.querySelector(iframeSelector) as HTMLIFrameElement | null;
      if (!iframe?.contentWindow) throw new Error(`VDO iframe not found: ${iframeSelector}`);
      const payload: { setBufferDelay: number; streamID?: string } = { setBufferDelay: delayMs };
      if (streamId) payload.streamID = streamId;
      iframe.contentWindow.postMessage(payload, targetOrigin);
    },
    { iframeSelector, delayMs, streamId, targetOrigin: VDO_ORIGIN }
  );
}

async function startPublisherIfNeeded(page: Page) {
  const joinButton = page.getByRole('button', { name: /join room with camera/i });
  try {
    await joinButton.click({ timeout: 10000 });
    await page.waitForTimeout(3000);
  } catch {
    // Autostart/test media may have already started.
  }
}

function logPageDiagnostics(page: Page, label: string) {
  page.on('console', msg => {
    const text = msg.text();
    if (/error|warn|websocket|socket|failed|denied|gum|publish|room|join/i.test(text)) {
      console.log(`${label} console ${msg.type()}: ${text.slice(0, 500)}`);
    }
  });
  page.on('requestfailed', request => {
    console.log(`${label} requestfailed: ${request.url()} ${request.failure()?.errorText}`);
  });
  page.on('pageerror', err => {
    console.log(`${label} pageerror: ${String(err).slice(0, 500)}`);
  });
}

async function readVdoBufferState(page: Page, iframeSelector: string, streamId: string) {
  const detailed = await callVdoIframe(page, iframeSelector, { getDetailedState: true });
  const stats = await callVdoIframe(page, iframeSelector, { getStats: true });

  const detailedState = (detailed.detailedState ?? {}) as Record<string, any>;
  const item =
    streamId === '*'
      ? Object.values(detailedState).find((entry: any) => entry && !entry.localStream)
      : detailedState[streamId] ?? Object.values(detailedState).find((entry: any) => entry?.streamID === streamId);
  const inbound = ((stats.stats as any)?.inbound ?? {}) as Record<string, any>;
  const streamStats =
    streamId === '*'
      ? item?.streamID
        ? inbound[item.streamID]
        : undefined
      : inbound[streamId] ?? (item?.streamID ? inbound[item.streamID] : undefined);
  const chunkStats = streamStats?.chunked_mode_video;

  return {
    detailedState,
    item,
    streamStats,
    chunkStats,
    requested: item?.chunkedBufferRequested,
    statsBuffer: chunkStats?.buffer_buffer,
  };
}

async function waitForExpectedBuffer(
  page: Page,
  iframeSelector: string,
  streamId: string,
  expectedDelayMs: number,
  timeoutMs = 60000
) {
  const started = Date.now();
  let last: Awaited<ReturnType<typeof readVdoBufferState>> | null = null;
  while (Date.now() - started < timeoutMs) {
    try {
      last = await readVdoBufferState(page, iframeSelector, streamId);
      if (
        last.item &&
        last.chunkStats &&
        last.requested === expectedDelayMs &&
        last.statsBuffer === expectedDelayMs
      ) {
        return last;
      }
    } catch {
      // VDO may still be loading, connecting, or applying the new target.
    }
    await page.waitForTimeout(1000);
  }
  throw new Error(`Timed out waiting for stream ${streamId} buffer=${expectedDelayMs}; last=${JSON.stringify(last)}`);
}

async function waitForAnyBuffer(page: Page, iframeSelector: string, streamId: string, timeoutMs = 60000) {
  const started = Date.now();
  let last: Awaited<ReturnType<typeof readVdoBufferState>> | null = null;
  while (Date.now() - started < timeoutMs) {
    try {
      last = await readVdoBufferState(page, iframeSelector, streamId);
      if (last.item && last.chunkStats && typeof last.statsBuffer === 'number') {
        return last;
      }
    } catch {
      // VDO may still be loading or connecting.
    }
    await page.waitForTimeout(1000);
  }
  throw new Error(`Timed out waiting for stream ${streamId}; last=${JSON.stringify(last)}`);
}

for (const target of VDO_TARGETS) {
  test(`control: direct VDO ${target.name} viewer applies and updates buffer`, async ({ page, browser }) => {
    test.setTimeout(150000);

    const streamId = 'alice';
    const expectedDelayMs = BUFFER_SEQUENCE[0];

    const publisherContext = await browser.newContext({ permissions: ['camera', 'microphone'] });
    const publisher = await publisherContext.newPage();
    logPageDiagnostics(publisher, `${target.name} direct publisher`);
    await publisher.goto(
      vdoUrl(
        target,
        `push=${streamId}&webcam&autostart&chunked=1800&label=Alice&testmedia=1&testwidth=640&testheight=360&testfps=30`
      ),
      { waitUntil: 'domcontentloaded', timeout: 45000 }
    );
    await startPublisherIfNeeded(publisher);
    await publisher.waitForTimeout(5000);
    console.log(`${target.name} direct publisherUrl`, publisher.url());
    const publisherState = await callVdoTop(publisher, { getDetailedState: true }).catch(err => ({ error: String(err) }));
    test.info().annotations.push({ type: `${target.name}PublisherState`, description: JSON.stringify(publisherState).slice(0, 1000) });

    const viewerUrl = vdoUrl(
      target,
      `view=${streamId}&scene&cleanoutput&noaudio&chunked&fixedchunkbuffer&chunkbufferadaptive=0&chunkbufferceil=180000&chunkbuffer=${expectedDelayMs}&buffer=${expectedDelayMs}`
    );

    logPageDiagnostics(page, `${target.name} direct viewer`);
    await page.goto(target.companionPath);
    await page.setContent(`<iframe id="viewer" src="${viewerUrl}" style="width:100%;height:700px;border:0"></iframe>`);

    let observed = await waitForAnyBuffer(page, '#viewer', streamId);
    expect(observed.item?.localStream).toBe(false);

    for (const delayMs of BUFFER_SEQUENCE) {
      await postBufferDelayToIframe(page, '#viewer', delayMs, streamId);
      observed = await waitForExpectedBuffer(page, '#viewer', streamId, delayMs, 30000);
      expect(observed.item?.localStream).toBe(false);
    }

    await publisherContext.close();
  });

  test(`control: VDO ${target.name} room viewer with scene/view applies buffer`, async ({ page, browser }) => {
    test.setTimeout(120000);

    const expectedDelayMs = BUFFER_SEQUENCE[0];
    const room = `jw-room-control-${target.name}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    const streamId = 'alice';

    const publisherContext = await browser.newContext({ permissions: ['camera', 'microphone'] });
    const publisher = await publisherContext.newPage();
    logPageDiagnostics(publisher, `${target.name} room publisher`);
    await publisher.goto(
      vdoUrl(
        target,
        `room=${encodeURIComponent(room)}&push=${streamId}&webcam&autostart&chunked=1800&label=Alice&testmedia=1&testwidth=640&testheight=360&testfps=30`
      ),
      { waitUntil: 'domcontentloaded', timeout: 45000 }
    );
    await startPublisherIfNeeded(publisher);
    await publisher.waitForTimeout(5000);

    const viewerUrl = vdoUrl(
      target,
      `room=${encodeURIComponent(room)}&scene&cleanoutput&view=${streamId}&noaudio&chunked&fixedchunkbuffer&chunkbufferadaptive=0&chunkbufferceil=180000&chunkbuffer=${expectedDelayMs}&buffer=${expectedDelayMs}`
    );

    logPageDiagnostics(page, `${target.name} room viewer`);
    await page.goto(target.companionPath);
    await page.setContent(`<iframe id="viewer" src="${viewerUrl}" style="width:100%;height:700px;border:0"></iframe>`);

    await waitForAnyBuffer(page, '#viewer', streamId);
    await postBufferDelayToIframe(page, '#viewer', expectedDelayMs, streamId);
    const observed = await waitForExpectedBuffer(page, '#viewer', streamId, expectedDelayMs);
    expect(observed.item?.streamID).toBe(streamId);

    await publisherContext.close();
  });

  test(`popout VDO ${target.name} output applies and updates plugin buffer delay`, async ({ page, browser }) => {
    test.setTimeout(150000);

    const expectedDelayMs = BUFFER_SEQUENCE[0];
    const room = `jw-e2e-${target.name}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    const streamId = 'alice';

    await page.goto(target.companionPath);
    await mock.waitForClient();

    mock.sendBufferDelay(expectedDelayMs);
    await page.waitForTimeout(100);
    mock.sendConfig(room, 'viewer');
    mock.sendRoster([{ idx: 0, name: 'Alice', streamId }]);

    const publisherContext = await browser.newContext({ permissions: ['camera', 'microphone'] });
    const publisher = await publisherContext.newPage();
    logPageDiagnostics(publisher, `${target.name} jamwide publisher`);
    await publisher.goto(
      vdoUrl(
        target,
        `room=${encodeURIComponent(room)}&push=${streamId}&webcam&autostart&chunked=1800&label=Alice&testmedia=1&testwidth=640&testheight=360&testfps=30`
      ),
      { waitUntil: 'domcontentloaded', timeout: 45000 }
    );
    await startPublisherIfNeeded(publisher);
    await publisher.waitForTimeout(5000);
    console.log(`${target.name} jamwide publisherUrl`, publisher.url());
    const publisherState = await callVdoTop(publisher, { getDetailedState: true }).catch(err => ({ error: String(err) }));
    test.info().annotations.push({ type: `${target.name}PublisherState`, description: JSON.stringify(publisherState).slice(0, 1000) });

    const popupPromise = page.waitForEvent('popup');
    await page.locator(`.roster-pill[data-stream-id="${streamId}"]`).click();
    const popout = await popupPromise;

    await expect(popout.locator('#video-area iframe')).toBeVisible({ timeout: 10000 });

    let observed = await waitForAnyBuffer(popout, '#video-area iframe', streamId);
    expect(observed.item?.streamID).toBe(streamId);

    for (const delayMs of BUFFER_SEQUENCE) {
      mock.sendBufferDelay(delayMs);
      observed = await waitForExpectedBuffer(popout, '#video-area iframe', streamId, delayMs, 30000);
      expect(observed.item?.streamID).toBe(streamId);
    }

    await publisherContext.close();
  });
}
