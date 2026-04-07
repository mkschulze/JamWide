import { describe, it, expect, beforeEach, vi } from 'vitest';
import { applyBufferDelay, getCachedBufferDelay, resetCachedBufferDelay, reapplyCachedBufferDelay } from '../ui';

describe('Buffer Delay Relay (VID-08)', () => {
  beforeEach(() => {
    // Set up test DOM (safe in test environment, not production code)
    document.body.textContent = '';
    const main = document.createElement('main');
    main.id = 'main-area';
    document.body.appendChild(main);
    resetCachedBufferDelay();
  });

  it('caches buffer delay value', () => {
    applyBufferDelay(8000);
    expect(getCachedBufferDelay()).toBe(8000);
  });

  it('sends postMessage to iframe with setBufferDelay', () => {
    const mockPostMessage = vi.fn();
    const iframe = document.createElement('iframe');
    Object.defineProperty(iframe, 'contentWindow', {
      value: { postMessage: mockPostMessage },
    });
    document.getElementById('main-area')!.appendChild(iframe);

    applyBufferDelay(8000);
    expect(mockPostMessage).toHaveBeenCalledWith({ setBufferDelay: 8000 }, '*');
  });

  it('does not throw when no iframe exists', () => {
    expect(() => applyBufferDelay(8000)).not.toThrow();
    expect(getCachedBufferDelay()).toBe(8000);
  });

  it('reapplyCachedBufferDelay sends cached value to iframe', () => {
    const mockPostMessage = vi.fn();
    const iframe = document.createElement('iframe');
    Object.defineProperty(iframe, 'contentWindow', {
      value: { postMessage: mockPostMessage },
    });
    document.getElementById('main-area')!.appendChild(iframe);

    applyBufferDelay(12000);
    mockPostMessage.mockClear();

    reapplyCachedBufferDelay();
    expect(mockPostMessage).toHaveBeenCalledWith({ setBufferDelay: 12000 }, '*');
  });

  it('reapplyCachedBufferDelay is no-op when no cached value', () => {
    const mockPostMessage = vi.fn();
    const iframe = document.createElement('iframe');
    Object.defineProperty(iframe, 'contentWindow', {
      value: { postMessage: mockPostMessage },
    });
    document.getElementById('main-area')!.appendChild(iframe);

    reapplyCachedBufferDelay();
    expect(mockPostMessage).not.toHaveBeenCalled();
  });

  it('overwrites previous cached value with new one', () => {
    applyBufferDelay(8000);
    applyBufferDelay(16000);
    expect(getCachedBufferDelay()).toBe(16000);
  });
});
