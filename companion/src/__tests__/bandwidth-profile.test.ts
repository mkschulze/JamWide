import { describe, it, expect, beforeEach } from 'vitest';
import { getSavedBandwidthProfile, saveBandwidthProfile, BANDWIDTH_PROFILES } from '../ui';

describe('Bandwidth Profile (VID-12)', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  it('defaults to balanced when localStorage is empty', () => {
    expect(getSavedBandwidthProfile()).toBe('balanced');
  });

  it('saves and retrieves low profile', () => {
    saveBandwidthProfile('low');
    expect(getSavedBandwidthProfile()).toBe('low');
  });

  it('saves and retrieves high profile', () => {
    saveBandwidthProfile('high');
    expect(getSavedBandwidthProfile()).toBe('high');
  });

  it('uses localStorage key jamwide-bandwidth-profile', () => {
    saveBandwidthProfile('low');
    expect(localStorage.getItem('jamwide-bandwidth-profile')).toBe('low');
  });

  // Addresses review concern R-MEDIUM-07: defensive handling for invalid stored values
  it('falls back to balanced when localStorage has invalid value', () => {
    localStorage.setItem('jamwide-bandwidth-profile', 'ultra');
    expect(getSavedBandwidthProfile()).toBe('balanced');
  });

  it('falls back to balanced when localStorage has empty string', () => {
    localStorage.setItem('jamwide-bandwidth-profile', '');
    expect(getSavedBandwidthProfile()).toBe('balanced');
  });

  it('falls back to balanced when localStorage has numeric value', () => {
    localStorage.setItem('jamwide-bandwidth-profile', '42');
    expect(getSavedBandwidthProfile()).toBe('balanced');
  });

  it('BANDWIDTH_PROFILES has correct quality values (inverted numbering)', () => {
    expect(BANDWIDTH_PROFILES.low.quality).toBe(2);
    expect(BANDWIDTH_PROFILES.balanced.quality).toBe(1);
    expect(BANDWIDTH_PROFILES.high.quality).toBe(0);
  });

  it('BANDWIDTH_PROFILES has correct bitrate caps', () => {
    expect(BANDWIDTH_PROFILES.low.maxvideobitrate).toBe(500);
    expect(BANDWIDTH_PROFILES.balanced.maxvideobitrate).toBe(1500);
    expect(BANDWIDTH_PROFILES.high.maxvideobitrate).toBe(3000);
  });
});
