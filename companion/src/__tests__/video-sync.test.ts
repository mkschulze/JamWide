import { describe, it, expect, beforeEach, vi } from 'vitest';
import {
  setLastAutoDelay,
  getLastAutoDelayMs,
  getActiveDelayMs,
  isManualMode,
  switchToManual,
  switchToAuto,
  resetDelayState,
  buildVdoNinjaUrl,
  getDelayDisplayText,
  reapplyActiveDelay,
} from '../ui';

describe('Video Sync Dual-State Delay Model (VID-08)', () => {
  beforeEach(() => {
    // Set up test DOM
    document.body.textContent = '';
    const main = document.createElement('main');
    main.id = 'main-area';
    document.body.appendChild(main);
    resetDelayState();
  });

  describe('lastAutoDelayMs tracking', () => {
    it('setLastAutoDelay stores value, getLastAutoDelayMs returns it', () => {
      setLastAutoDelay(8000);
      expect(getLastAutoDelayMs()).toBe(8000);
    });

    it('setLastAutoDelay overwrites previous value', () => {
      setLastAutoDelay(8000);
      setLastAutoDelay(12000);
      expect(getLastAutoDelayMs()).toBe(12000);
    });
  });

  describe('activeDelayMs initial state', () => {
    it('initial activeDelayMs is null (no hardcoded default)', () => {
      expect(getActiveDelayMs()).toBeNull();
    });
  });

  describe('auto mode behavior', () => {
    it('in auto mode, setLastAutoDelay also sets activeDelayMs', () => {
      setLastAutoDelay(8000);
      expect(getActiveDelayMs()).toBe(8000);
    });
  });

  describe('manual mode behavior', () => {
    it('in manual mode, setLastAutoDelay does NOT change activeDelayMs', () => {
      setLastAutoDelay(8000);
      switchToManual(5000);
      setLastAutoDelay(12000);
      expect(getActiveDelayMs()).toBe(5000);
      expect(getLastAutoDelayMs()).toBe(12000);
    });
  });

  describe('mode switching', () => {
    it('switchToManual sets activeDelayMs and enables manual mode', () => {
      switchToManual(5000);
      expect(getActiveDelayMs()).toBe(5000);
      expect(isManualMode()).toBe(true);
    });

    it('switchToAuto restores lastAutoDelayMs to activeDelayMs', () => {
      setLastAutoDelay(8000);
      switchToManual(5000);
      switchToAuto();
      expect(getActiveDelayMs()).toBe(8000);
      expect(isManualMode()).toBe(false);
    });

    it('switchToAuto when lastAutoDelayMs is null sets activeDelayMs to null', () => {
      switchToManual(5000);
      switchToAuto();
      expect(getActiveDelayMs()).toBeNull();
      expect(isManualMode()).toBe(false);
    });
  });

  describe('buildVdoNinjaUrl buffer param', () => {
    it('includes &buffer=N when activeDelayMs is set', () => {
      setLastAutoDelay(8000);
      const url = buildVdoNinjaUrl('testroom', 'testpush');
      expect(url).toContain('&buffer=8000');
    });

    it('omits &buffer= when activeDelayMs is null (pre-config)', () => {
      const url = buildVdoNinjaUrl('testroom', 'testpush');
      expect(url).not.toContain('&buffer=');
    });
  });

  describe('getDelayDisplayText', () => {
    it('returns "--" when activeDelayMs is null', () => {
      expect(getDelayDisplayText()).toBe('--');
    });

    it('returns "8.0s (calculated)" when activeDelayMs=8000, calculated mode', () => {
      setLastAutoDelay(8000);
      expect(getDelayDisplayText()).toBe('8.0s (calculated)');
    });

    it('returns "0.0s (manual) No delay" when activeDelayMs=0, manual mode', () => {
      switchToManual(0);
      expect(getDelayDisplayText()).toBe('0.0s (manual) No delay');
    });

    it('returns "5.0s (manual)" when activeDelayMs=5000, manual mode', () => {
      switchToManual(5000);
      expect(getDelayDisplayText()).toBe('5.0s (manual)');
    });

    it('returns "0.0s (calculated) No delay" when activeDelayMs=0, calculated mode', () => {
      setLastAutoDelay(0);
      expect(getDelayDisplayText()).toBe('0.0s (calculated) No delay');
    });
  });

  describe('postMessage security', () => {
    it('applyBufferDelay sends postMessage with https://vdo.ninja targetOrigin', () => {
      const mockPostMessage = vi.fn();
      const iframe = document.createElement('iframe');
      Object.defineProperty(iframe, 'contentWindow', {
        value: { postMessage: mockPostMessage },
      });
      document.getElementById('main-area')!.appendChild(iframe);

      setLastAutoDelay(8000);
      expect(mockPostMessage).toHaveBeenCalledWith(
        { setBufferDelay: 8000 },
        'https://vdo.ninja'
      );
    });
  });

  describe('reapplyActiveDelay', () => {
    it('sends activeDelayMs to iframe when set', () => {
      const mockPostMessage = vi.fn();
      const iframe = document.createElement('iframe');
      Object.defineProperty(iframe, 'contentWindow', {
        value: { postMessage: mockPostMessage },
      });
      document.getElementById('main-area')!.appendChild(iframe);

      setLastAutoDelay(8000);
      mockPostMessage.mockClear();

      reapplyActiveDelay();
      expect(mockPostMessage).toHaveBeenCalledWith(
        { setBufferDelay: 8000 },
        'https://vdo.ninja'
      );
    });

    it('is no-op when activeDelayMs is null', () => {
      const mockPostMessage = vi.fn();
      const iframe = document.createElement('iframe');
      Object.defineProperty(iframe, 'contentWindow', {
        value: { postMessage: mockPostMessage },
      });
      document.getElementById('main-area')!.appendChild(iframe);

      reapplyActiveDelay();
      expect(mockPostMessage).not.toHaveBeenCalled();
    });
  });
});
