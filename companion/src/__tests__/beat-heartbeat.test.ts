import { describe, it, expect, beforeEach } from 'vitest';
import { updateSyncIndicator, hideSyncIndicator } from '../ui';
import { isBeatHeartbeatMessage } from '../types';

describe('Sync Indicator (Beat Heartbeat)', () => {
  beforeEach(() => {
    document.body.textContent = '';

    // Set up minimal DOM needed for sync indicator
    const footer = document.createElement('footer');
    footer.id = 'footer';

    const indicator = document.createElement('span');
    indicator.id = 'sync-indicator';
    indicator.style.display = 'none';

    const dot = document.createElement('span');
    dot.id = 'sync-beat-dot';
    dot.className = 'sync-dot';

    const label = document.createElement('span');
    label.id = 'sync-beat-label';
    label.className = 'sync-label';

    indicator.appendChild(dot);
    indicator.appendChild(label);
    footer.appendChild(indicator);
    document.body.appendChild(footer);

    // Also add required elements for other ui.ts functions
    const header = document.createElement('header');
    const badge = document.createElement('span');
    badge.id = 'status-badge';
    header.appendChild(badge);
    document.body.appendChild(header);

    const main = document.createElement('main');
    main.id = 'main-area';
    document.body.appendChild(main);
  });

  it('shows indicator with correct text', () => {
    updateSyncIndicator(4, 16, 42);

    const container = document.getElementById('sync-indicator')!;
    const label = document.getElementById('sync-beat-label')!;

    expect(container.style.display).toBe('inline-flex');
    expect(label.textContent).toBe('5/16 #42');
  });

  it('displays beat 0 as "1/BPI"', () => {
    updateSyncIndicator(0, 16, 1);

    const label = document.getElementById('sync-beat-label')!;
    expect(label.textContent).toBe('1/16 #1');
  });

  it('applies beat-flash-downbeat class on beat 0', () => {
    updateSyncIndicator(0, 16, 1);

    const dot = document.getElementById('sync-beat-dot')!;
    expect(dot.classList.contains('beat-flash-downbeat')).toBe(true);
    expect(dot.classList.contains('beat-flash')).toBe(false);
  });

  it('applies beat-flash class on non-zero beat', () => {
    updateSyncIndicator(5, 16, 1);

    const dot = document.getElementById('sync-beat-dot')!;
    expect(dot.classList.contains('beat-flash')).toBe(true);
    expect(dot.classList.contains('beat-flash-downbeat')).toBe(false);
  });

  it('hides indicator', () => {
    updateSyncIndicator(3, 16, 1);
    hideSyncIndicator();

    const container = document.getElementById('sync-indicator')!;
    expect(container.style.display).toBe('none');
  });

  it('updates label when beat changes', () => {
    updateSyncIndicator(0, 8, 1);
    const label = document.getElementById('sync-beat-label')!;
    expect(label.textContent).toBe('1/8 #1');

    updateSyncIndicator(7, 8, 2);
    expect(label.textContent).toBe('8/8 #2');
  });
});

describe('isBeatHeartbeatMessage validator', () => {
  it('accepts valid message', () => {
    expect(isBeatHeartbeatMessage({ type: 'beatHeartbeat', beat: 3, bpi: 16, interval: 1 })).toBe(true);
  });

  it('rejects wrong type', () => {
    expect(isBeatHeartbeatMessage({ type: 'config', beat: 3, bpi: 16, interval: 1 })).toBe(false);
  });

  it('rejects missing beat field', () => {
    expect(isBeatHeartbeatMessage({ type: 'beatHeartbeat', bpi: 16, interval: 1 })).toBe(false);
  });

  it('rejects missing bpi field', () => {
    expect(isBeatHeartbeatMessage({ type: 'beatHeartbeat', beat: 3, interval: 1 })).toBe(false);
  });

  it('rejects missing interval field', () => {
    expect(isBeatHeartbeatMessage({ type: 'beatHeartbeat', beat: 3, bpi: 16 })).toBe(false);
  });

  it('rejects null', () => {
    expect(isBeatHeartbeatMessage(null)).toBe(false);
  });

  it('rejects string', () => {
    expect(isBeatHeartbeatMessage('beatHeartbeat')).toBe(false);
  });

  it('rejects number fields as strings', () => {
    expect(isBeatHeartbeatMessage({ type: 'beatHeartbeat', beat: '3', bpi: 16, interval: 1 })).toBe(false);
  });
});
