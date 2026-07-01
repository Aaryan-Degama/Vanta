/**
 * Unit tests for the pure formatting helpers.
 */
import { formatBytes, formatDate } from '../utils/formatting';

describe('formatBytes', () => {
  it('returns 0 B for zero bytes', () => {
    expect(formatBytes(0)).toBe('0 B');
  });

  it('returns 0 B for negative bytes', () => {
    expect(formatBytes(-100)).toBe('0 B');
  });

  it('formats bytes', () => {
    expect(formatBytes(512)).toBe('512 B');
  });

  it('formats kilobytes', () => {
    expect(formatBytes(1536)).toBe('1.5 KB');
  });

  it('formats megabytes', () => {
    expect(formatBytes(1024 * 1024 * 2)).toBe('2 MB');
  });

  it('formats gigabytes', () => {
    expect(formatBytes(1024 * 1024 * 1024 * 1.5)).toBe('1.5 GB');
  });
});

describe('formatDate', () => {
  it('returns an empty string for falsy input', () => {
    expect(formatDate(0)).toBe('');
  });

  it('formats a known Unix timestamp', () => {
    // 2024-01-01T00:00:00Z
    const formatted = formatDate(1704067200);
    expect(formatted).toContain('2024');
    expect(formatted).toContain('Jan');
  });
});
