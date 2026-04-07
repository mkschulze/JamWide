import { describe, it, expect, beforeEach } from 'vitest';
import { buildVdoNinjaUrl } from '../ui';

describe('VDO.Ninja URL Builder (VID-08, VID-09, VID-12)', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  it('includes chunked mode parameter', () => {
    const url = buildVdoNinjaUrl('room1', 'user1');
    expect(url).toContain('&chunked');
  });

  it('includes chunkbufferadaptive=0 for fixed buffer', () => {
    const url = buildVdoNinjaUrl('room1', 'user1');
    expect(url).toContain('&chunkbufferadaptive=0');
  });

  it('includes chunkbufferceil=180000', () => {
    const url = buildVdoNinjaUrl('room1', 'user1');
    expect(url).toContain('&chunkbufferceil=180000');
  });

  it('defaults to balanced profile (quality=1, maxvideobitrate=1500)', () => {
    const url = buildVdoNinjaUrl('room1', 'user1');
    expect(url).toContain('&quality=1');
    expect(url).toContain('&maxvideobitrate=1500');
  });

  it('low profile uses quality=2, maxvideobitrate=500', () => {
    const url = buildVdoNinjaUrl('room1', 'user1', undefined, 'low');
    expect(url).toContain('&quality=2');
    expect(url).toContain('&maxvideobitrate=500');
  });

  it('high profile uses quality=0, maxvideobitrate=3000', () => {
    const url = buildVdoNinjaUrl('room1', 'user1', undefined, 'high');
    expect(url).toContain('&quality=0');
    expect(url).toContain('&maxvideobitrate=3000');
  });

  it('includes hash fragment as VDO.Ninja password param', () => {
    const url = buildVdoNinjaUrl('room1', 'user1', undefined, undefined, 'abc123def456');
    expect(url).toContain('&password=abc123def456');
  });

  it('omits password param when no hash fragment', () => {
    const url = buildVdoNinjaUrl('room1', 'user1');
    expect(url).not.toContain('&password=');
  });

  it('includes noaudio and cleanoutput', () => {
    const url = buildVdoNinjaUrl('room1', 'user1');
    expect(url).toContain('&noaudio');
    expect(url).toContain('&cleanoutput');
  });

  it('includes effect params when specified', () => {
    const url = buildVdoNinjaUrl('room1', 'user1', 'blur');
    expect(url).toContain('&effects=3');
  });
});
