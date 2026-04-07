import { describe, it, expect, beforeEach } from 'vitest';
import { isPopoutMessage, isDeactivateMessage } from '../types';
import { renderRosterStrip } from '../ui';

describe('PopoutMessage type guard (VID-07)', () => {
  it('returns true for valid PopoutMessage', () => {
    expect(isPopoutMessage({ type: 'popout', streamId: 'abc' })).toBe(true);
  });

  it('returns false for RosterMessage', () => {
    expect(isPopoutMessage({ type: 'roster', users: [] })).toBe(false);
  });

  it('returns false for null', () => {
    expect(isPopoutMessage(null)).toBe(false);
  });

  it('returns false for missing streamId', () => {
    expect(isPopoutMessage({ type: 'popout' })).toBe(false);
  });

  it('returns false for non-string streamId', () => {
    expect(isPopoutMessage({ type: 'popout', streamId: 42 })).toBe(false);
  });
});

describe('DeactivateMessage type guard (VID-07)', () => {
  it('returns true for valid DeactivateMessage', () => {
    expect(isDeactivateMessage({ type: 'deactivate' })).toBe(true);
  });

  it('returns false for ConfigMessage', () => {
    expect(isDeactivateMessage({ type: 'config', room: 'r', push: 'p', noaudio: true, wsPort: 7170 })).toBe(false);
  });

  it('returns false for null', () => {
    expect(isDeactivateMessage(null)).toBe(false);
  });

  it('returns false for string', () => {
    expect(isDeactivateMessage('deactivate')).toBe(false);
  });
});

describe('renderRosterStrip creates buttons (VID-07 popout)', () => {
  beforeEach(() => {
    document.body.textContent = '';
    const main = document.createElement('main');
    main.id = 'main-area';
    const strip = document.createElement('div');
    strip.id = 'roster-strip';
    strip.setAttribute('role', 'status');
    strip.setAttribute('aria-label', 'Connected participants');
    strip.style.display = 'none';
    main.appendChild(strip);
    document.body.appendChild(main);
  });

  it('creates <button> elements (not <span>) with class roster-pill', () => {
    renderRosterStrip([{ idx: 0, name: 'Alice', streamId: 'Alice' }]);
    const pills = document.querySelectorAll('.roster-pill');
    expect(pills.length).toBe(1);
    expect(pills[0].tagName).toBe('BUTTON');
  });

  it('pills have data-stream-id attribute', () => {
    renderRosterStrip([{ idx: 0, name: 'Alice', streamId: 'AliceGuitar' }]);
    const pill = document.querySelector('.roster-pill') as HTMLElement;
    expect(pill.dataset.streamId).toBe('AliceGuitar');
  });

  it('pills have title attribute "Pop out {Username} video"', () => {
    renderRosterStrip([{ idx: 0, name: 'Alice', streamId: 'Alice' }]);
    const pill = document.querySelector('.roster-pill') as HTMLButtonElement;
    expect(pill.title).toBe('Pop out Alice video');
  });

  it('pills have class roster-pill', () => {
    renderRosterStrip([
      { idx: 0, name: 'Alice', streamId: 'Alice' },
      { idx: 1, name: 'Bob', streamId: 'Bob' },
    ]);
    const pills = document.querySelectorAll('.roster-pill');
    expect(pills.length).toBe(2);
    pills.forEach(pill => {
      expect(pill.classList.contains('roster-pill')).toBe(true);
    });
  });
});
