/**
 * Pure formatting helpers used across the Vanta app.
 *
 * These functions have no React Native or native-module dependencies, so they
 * are easy to unit test and can be reused by any screen.
 */

/**
 * Converts a byte count into a human-readable string.
 *
 * @param bytes Number of bytes. Zero or negative values return "0 B".
 * @returns Formatted string such as "1.5 MB".
 */
export function formatBytes(bytes: number): string {
  if (!bytes || bytes <= 0) return '0 B';

  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.min(sizes.length - 1, Math.floor(Math.log(bytes) / Math.log(k)));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

/**
 * Converts a Unix timestamp (seconds since epoch) into a readable date string.
 *
 * @param unix Unix timestamp in seconds.
 * @returns Formatted date/time string, or an empty string if unix is falsy.
 */
export function formatDate(unix: number): string {
  if (!unix) return '';

  const d = new Date(unix * 1000);
  return (
    d.toLocaleDateString('en-GB', { day: 'numeric', month: 'short', year: 'numeric' }) +
    ' ' +
    d.toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit', hour12: true })
  );
}
