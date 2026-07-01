/**
 * Global React error boundary for the Vanta app.
 *
 * Catches JavaScript errors anywhere in the child component tree and renders a
 * fallback UI instead of crashing the whole app. This is especially important
 * for native module calls that can throw synchronously or during render.
 */
import React, { Component, ReactNode } from 'react';
import { View, Text, StyleSheet, Button } from 'react-native';

interface Props {
  /** Components wrapped by the boundary. */
  children: ReactNode;
}

interface State {
  /** True once an error has been caught. */
  hasError: boolean;
  /** Optional error message to display. */
  errorMessage: string;
}

/**
 * Class component implementing React's error-boundary lifecycle methods.
 */
export class ErrorBoundary extends Component<Props, State> {
  constructor(props: Props) {
    super(props);
    this.state = { hasError: false, errorMessage: '' };
  }

  /**
   * Called when a descendant throws; updates state so the fallback UI renders.
   */
  static getDerivedStateFromError(error: Error): State {
    return {
      hasError: true,
      errorMessage: error.message || 'Unknown error',
    };
  }

  /**
   * Called after an error is caught; useful for logging to crash reporting.
   */
  componentDidCatch(error: Error, errorInfo: React.ErrorInfo) {
    console.error('ErrorBoundary caught an error:', error, errorInfo);
  }

  /**
   * Resets the boundary so children render again.
   */
  handleReset = () => {
    this.setState({ hasError: false, errorMessage: '' });
  };

  render() {
    if (this.state.hasError) {
      return (
        <View style={styles.container}>
          <Text style={styles.title}>Something went wrong</Text>
          <Text style={styles.message}>{this.state.errorMessage}</Text>
          <Button title="Try again" onPress={this.handleReset} />
        </View>
      );
    }

    return this.props.children;
  }
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
    padding: 24,
    backgroundColor: '#fff',
  },
  title: {
    fontSize: 20,
    fontWeight: 'bold',
    marginBottom: 12,
    color: '#000',
  },
  message: {
    fontSize: 14,
    color: '#666',
    textAlign: 'center',
    marginBottom: 24,
  },
});
