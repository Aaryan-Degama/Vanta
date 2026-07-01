/**
 * Hook that provides a consistent way to surface native-module errors to users.
 *
 * Screens call `handleError(error)` whenever a Vanta engine call fails. The
 * hook formats the error and shows an Alert on Android/iOS (or logs it on web).
 */
import { useCallback } from 'react';
import { Alert, Platform } from 'react-native';

/**
 * Formats an unknown error value into a human-readable string.
 *
 * @param error Error value from a catch clause.
 * @returns Message safe to show to the user.
 */
function formatError(error: unknown): string {
  if (error instanceof Error) {
    return error.message;
  }
  if (typeof error === 'string') {
    return error;
  }
  return 'An unexpected error occurred. Please try again.';
}

/**
 * Hook that returns a stable error handler.
 *
 * @param context Short description of the operation that failed (e.g. "search").
 */
export function useVantaError(context: string) {
  return useCallback(
    (error: unknown) => {
      const message = formatError(error);
      console.error(`Vanta error (${context}):`, message);

      if (Platform.OS === 'web') {
        // Alert is not available in the browser; rely on the console.
        return;
      }

      Alert.alert(context, message);
    },
    [context]
  );
}
