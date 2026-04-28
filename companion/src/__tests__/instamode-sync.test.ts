import { describe, it, expect, beforeEach } from 'vitest';
import {
  setLastAutoDelay,
  getActiveDelayMs,
  getDelayDisplayText,
  isManualMode,
  switchToManual,
  switchToAuto,
  resetDelayState,
  getLastAutoSyncMode,
  showSyncingOverlay,
  hideSyncingOverlay,
} from '../ui';

describe('Three-Tier Delay Priority (VID-13, D-11)', () => {
  beforeEach(() => {
    document.body.textContent = '';
    const main = document.createElement('div');
    main.id = 'main-area';
    document.body.appendChild(main);
    resetDelayState();
  });

  it('setLastAutoDelay with syncMode="measured" sets lastAutoSyncMode', () => {
    setLastAutoDelay(4200, 'measured');
    expect(getLastAutoSyncMode()).toBe('measured');
    expect(getActiveDelayMs()).toBe(4200);
  });

  it('setLastAutoDelay with syncMode="calculated" sets lastAutoSyncMode', () => {
    setLastAutoDelay(4000, 'calculated');
    expect(getLastAutoSyncMode()).toBe('calculated');
  });

  it('setLastAutoDelay without syncMode defaults to calculated (D-13 backward compat)', () => {
    setLastAutoDelay(4000);
    expect(getLastAutoSyncMode()).toBe('calculated');
  });

  it('setLastAutoDelay with unknown syncMode defaults to calculated (validation)', () => {
    setLastAutoDelay(4000, 'bogus' as any);
    expect(getLastAutoSyncMode()).toBe('calculated');
  });

  it('manual mode overrides measured mode in display text (D-11)', () => {
    setLastAutoDelay(4200, 'measured');
    switchToManual(5000);
    expect(getDelayDisplayText()).toBe('5.0s (manual)');
    expect(isManualMode()).toBe(true);
  });

  it('manual mode preserves lastAutoSyncMode for later restore', () => {
    setLastAutoDelay(4200, 'measured');
    switchToManual(5000);
    expect(getLastAutoSyncMode()).toBe('measured');
  });

  it('switchToAuto restores last measured syncMode (D-11)', () => {
    setLastAutoDelay(4200, 'measured');
    switchToManual(5000);
    switchToAuto();
    expect(getDelayDisplayText()).toBe('4.2s (measured)');
    expect(isManualMode()).toBe(false);
  });

  it('switchToAuto restores last calculated syncMode', () => {
    setLastAutoDelay(4000, 'calculated');
    switchToManual(5000);
    switchToAuto();
    expect(getDelayDisplayText()).toBe('4.0s (calculated)');
    expect(isManualMode()).toBe(false);
  });

  it('transition: calculated -> measured updates lastAutoSyncMode', () => {
    setLastAutoDelay(4000, 'calculated');
    setLastAutoDelay(4200, 'measured');
    expect(getLastAutoSyncMode()).toBe('measured');
    expect(getDelayDisplayText()).toBe('4.2s (measured)');
  });

  it('resetDelayState resets lastAutoSyncMode to calculated', () => {
    setLastAutoDelay(4200, 'measured');
    resetDelayState();
    expect(getLastAutoSyncMode()).toBe('calculated');
  });
});

describe('Sync Mode Footer Display (VID-13, D-12)', () => {
  beforeEach(() => {
    document.body.textContent = '';
    resetDelayState();
  });

  it('returns "--" when no delay set', () => {
    expect(getDelayDisplayText()).toBe('--');
  });

  it('returns "4.2s (measured)" for measured delay', () => {
    setLastAutoDelay(4200, 'measured');
    expect(getDelayDisplayText()).toBe('4.2s (measured)');
  });

  it('returns "4.0s (calculated)" for calculated delay', () => {
    setLastAutoDelay(4000, 'calculated');
    expect(getDelayDisplayText()).toBe('4.0s (calculated)');
  });

  it('returns "5.0s (manual)" for manual delay', () => {
    switchToManual(5000);
    expect(getDelayDisplayText()).toBe('5.0s (manual)');
  });

  it('returns "0.0s (measured) No delay" for zero measured delay', () => {
    setLastAutoDelay(0, 'measured');
    expect(getDelayDisplayText()).toBe('0.0s (measured) No delay');
  });

  it('returns "0.0s (manual) No delay" for zero manual delay', () => {
    switchToManual(0);
    expect(getDelayDisplayText()).toBe('0.0s (manual) No delay');
  });
});

describe('Syncing Overlay (VID-13, D-09)', () => {
  beforeEach(() => {
    document.body.textContent = '';
    const main = document.createElement('div');
    main.id = 'main-area';
    document.body.appendChild(main);
    resetDelayState();
  });

  it('showSyncingOverlay creates #sync-overlay element', () => {
    showSyncingOverlay(4000);
    const overlay = document.getElementById('sync-overlay');
    expect(overlay).not.toBeNull();
    expect(overlay!.textContent).toContain('Syncing');
  });

  it('showSyncingOverlay sets overlay inside #main-area', () => {
    showSyncingOverlay(4000);
    const mainArea = document.getElementById('main-area');
    expect(mainArea!.querySelector('#sync-overlay')).not.toBeNull();
  });

  it('hideSyncingOverlay removes #sync-overlay element', () => {
    showSyncingOverlay(4000);
    hideSyncingOverlay();
    expect(document.getElementById('sync-overlay')).toBeNull();
  });

  it('showSyncingOverlay replaces existing overlay (repeated update)', () => {
    showSyncingOverlay(4000);
    showSyncingOverlay(8000);
    const overlays = document.querySelectorAll('#sync-overlay');
    expect(overlays.length).toBe(1);
  });

  it('showSyncingOverlay does nothing if #main-area missing', () => {
    document.body.textContent = '';  // remove #main-area
    showSyncingOverlay(4000);
    expect(document.getElementById('sync-overlay')).toBeNull();
  });
});
