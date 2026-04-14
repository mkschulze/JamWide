import { describe, it, expect, beforeEach, vi } from 'vitest';
import { setLastAutoDelay, getActiveDelayMs, resetDelayState, reapplyActiveDelay } from '../ui';

describe('Buffer Delay Relay (VID-08)', () => {
  beforeEach(() => {
    // Set up test DOM (safe in test environment, not production code)
    document.body.textContent = '';
    const main = document.createElement('main');
    main.id = 'main-area';
    document.body.appendChild(main);
    resetDelayState();
  });

  it('stores delay value via setLastAutoDelay', () => {
    setLastAutoDelay(8000);
    expect(getActiveDelayMs()).toBe(8000);
  });

  it('sends postMessage to iframe with setBufferDelay and VDO.Ninja origin', () => {
    const mockPostMessage = vi.fn();
    const iframe = document.createElement('iframe');
    Object.defineProperty(iframe, 'contentWindow', {
      value: { postMessage: mockPostMessage },
    });
    document.getElementById('main-area')!.appendChild(iframe);

    setLastAutoDelay(8000);
    expect(mockPostMessage).toHaveBeenCalledWith({ setBufferDelay: 8000 }, 'https://vdo.ninja');
  });

  it('does not throw when no iframe exists', () => {
    expect(() => setLastAutoDelay(8000)).not.toThrow();
    expect(getActiveDelayMs()).toBe(8000);
  });

  it('reapplyActiveDelay sends active value to iframe', () => {
    const mockPostMessage = vi.fn();
    const iframe = document.createElement('iframe');
    Object.defineProperty(iframe, 'contentWindow', {
      value: { postMessage: mockPostMessage },
    });
    document.getElementById('main-area')!.appendChild(iframe);

    setLastAutoDelay(12000);
    mockPostMessage.mockClear();

    reapplyActiveDelay();
    expect(mockPostMessage).toHaveBeenCalledWith({ setBufferDelay: 12000 }, 'https://vdo.ninja');
  });

  it('reapplyActiveDelay is no-op when no active value', () => {
    const mockPostMessage = vi.fn();
    const iframe = document.createElement('iframe');
    Object.defineProperty(iframe, 'contentWindow', {
      value: { postMessage: mockPostMessage },
    });
    document.getElementById('main-area')!.appendChild(iframe);

    reapplyActiveDelay();
    expect(mockPostMessage).not.toHaveBeenCalled();
  });

  it('overwrites previous value with new one', () => {
    setLastAutoDelay(8000);
    setLastAutoDelay(16000);
    expect(getActiveDelayMs()).toBe(16000);
  });
});
