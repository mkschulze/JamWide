import { describe, it, expect, beforeEach } from 'vitest';
import { buildVdoNinjaUrl } from '../ui';

describe('buildVdoNinjaUrl with viewStreamId (VID-07 popout)', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  it('appends &view= when viewStreamId is provided', () => {
    const url = buildVdoNinjaUrl('room1', 'user1', undefined, undefined, undefined, 'abc123');
    expect(url).toContain('&view=abc123');
  });

  it('does NOT include &push= when viewStreamId is provided (view-only popout)', () => {
    const url = buildVdoNinjaUrl('room1', 'user1', undefined, undefined, undefined, 'abc123');
    expect(url).not.toContain('&push=');
  });

  it('includes &push= when viewStreamId is NOT provided (backward compat)', () => {
    const url = buildVdoNinjaUrl('room1', 'user1');
    expect(url).toContain('&push=user1');
  });

  it('URI-encodes viewStreamId with special characters', () => {
    const url = buildVdoNinjaUrl('room1', 'user1', undefined, undefined, undefined, 'stream id+foo');
    expect(url).toContain('&view=stream%20id%2Bfoo');
    expect(url).not.toContain('&push=');
  });
});
