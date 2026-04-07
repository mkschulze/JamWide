import { describe, it, expect, beforeEach } from 'vitest';
import { renderRosterStrip, isBotUser } from '../ui';

describe('Roster Label Strip (VID-10)', () => {
  beforeEach(() => {
    // Set up test DOM safely (test environment only)
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

  it('renders pill for each user', () => {
    renderRosterStrip([
      { idx: 0, name: 'Alice', streamId: 'Alice' },
      { idx: 1, name: 'Bob', streamId: 'Bob' },
    ]);

    const pills = document.querySelectorAll('.roster-pill');
    expect(pills.length).toBe(2);
    expect(pills[0].textContent).toBe('Alice');
    expect(pills[1].textContent).toBe('Bob');
  });

  it('shows roster strip when users present', () => {
    renderRosterStrip([{ idx: 0, name: 'Alice', streamId: 'Alice' }]);
    const strip = document.getElementById('roster-strip');
    expect(strip!.style.display).toBe('flex');
  });

  it('hides roster strip when no users', () => {
    renderRosterStrip([]);
    const strip = document.getElementById('roster-strip');
    expect(strip!.style.display).toBe('none');
  });

  // Addresses review concern R-HIGH-02: roster lifecycle -- full state replacement
  it('clears previous pills before rendering new ones (full state replacement)', () => {
    renderRosterStrip([{ idx: 0, name: 'Alice', streamId: 'Alice' }]);
    renderRosterStrip([{ idx: 0, name: 'Bob', streamId: 'Bob' }]);

    const pills = document.querySelectorAll('.roster-pill');
    expect(pills.length).toBe(1);
    expect(pills[0].textContent).toBe('Bob');
  });

  // Addresses review concern R-HIGH-02: roster lifecycle -- user leaving
  it('handles user leaving (3 users -> 2 users)', () => {
    renderRosterStrip([
      { idx: 0, name: 'Alice', streamId: 'Alice' },
      { idx: 1, name: 'Bob', streamId: 'Bob' },
      { idx: 2, name: 'Carol', streamId: 'Carol' },
    ]);
    renderRosterStrip([
      { idx: 0, name: 'Alice', streamId: 'Alice' },
      { idx: 1, name: 'Carol', streamId: 'Carol' },
    ]);

    const pills = document.querySelectorAll('.roster-pill');
    expect(pills.length).toBe(2);
    expect(pills[0].textContent).toBe('Alice');
    expect(pills[1].textContent).toBe('Carol');
  });

  // Addresses review concern R-HIGH-02: roster lifecycle -- user joining
  it('handles user joining (1 user -> 3 users)', () => {
    renderRosterStrip([{ idx: 0, name: 'Alice', streamId: 'Alice' }]);
    renderRosterStrip([
      { idx: 0, name: 'Alice', streamId: 'Alice' },
      { idx: 1, name: 'Bob', streamId: 'Bob' },
      { idx: 2, name: 'Carol', streamId: 'Carol' },
    ]);

    const pills = document.querySelectorAll('.roster-pill');
    expect(pills.length).toBe(3);
  });

  it('hides strip when all users leave', () => {
    renderRosterStrip([{ idx: 0, name: 'Alice', streamId: 'Alice' }]);
    expect(document.getElementById('roster-strip')!.style.display).toBe('flex');

    renderRosterStrip([]);
    expect(document.getElementById('roster-strip')!.style.display).toBe('none');
  });

  it('uses textContent for XSS safety (T-12-02)', () => {
    const malicious = '<img src=x onerror=alert(1)>';
    renderRosterStrip([{ idx: 0, name: malicious, streamId: 'test' }]);

    const pill = document.querySelector('.roster-pill');
    // textContent escapes HTML -- the raw string is displayed, not executed
    expect(pill!.textContent).toBe(malicious);
    // Verify no HTML element was created from the malicious string
    expect(pill!.children.length).toBe(0);
  });

  it('sets data-stream-id attribute on pills', () => {
    renderRosterStrip([{ idx: 0, name: 'Alice', streamId: 'Aliceguitar' }]);
    const pill = document.querySelector('.roster-pill') as HTMLElement;
    expect(pill.dataset.streamId).toBe('Aliceguitar');
  });

  it('filters out bot users from roster strip', () => {
    renderRosterStrip([
      { idx: 0, name: 'Alice', streamId: 'Alice' },
      { idx: 1, name: 'ninbot', streamId: 'ninbot' },
      { idx: 2, name: 'Bob', streamId: 'Bob' },
    ]);
    const pills = document.querySelectorAll('.roster-pill');
    expect(pills.length).toBe(2);
    expect(pills[0].textContent).toBe('Alice');
    expect(pills[1].textContent).toBe('Bob');
  });

  it('hides strip when only bot users remain', () => {
    renderRosterStrip([
      { idx: 0, name: 'ninbot', streamId: 'ninbot' },
      { idx: 1, name: 'jambot', streamId: 'jambot' },
    ]);
    const strip = document.getElementById('roster-strip');
    expect(strip!.style.display).toBe('none');
    expect(document.querySelectorAll('.roster-pill').length).toBe(0);
  });
});

describe('isBotUser', () => {
  it('returns true for known bot names', () => {
    expect(isBotUser('ninbot')).toBe(true);
    expect(isBotUser('jambot')).toBe(true);
    expect(isBotUser('ninjam')).toBe(true);
  });

  it('is case-insensitive', () => {
    expect(isBotUser('NinBot')).toBe(true);
    expect(isBotUser('NINBOT')).toBe(true);
  });

  it('returns false for regular users', () => {
    expect(isBotUser('Alice')).toBe(false);
    expect(isBotUser('Bob')).toBe(false);
  });
});
