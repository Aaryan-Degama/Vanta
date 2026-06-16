import { StyleSheet, Text, View, Button, Alert } from 'react-native';
import * as VantaEngine from './modules/vanta-bridge';

export default function App() {

  const handleKotlinPress = () => {
    const result = VantaEngine.helloFromKotlin();
    Alert.alert("Result", result);
  };

  const handleCppPress = () => {
    const result = VantaEngine.helloFromCpp();
    Alert.alert("Result", result);
  };

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Vanta Native Bridge Test</Text>

      <View style={styles.buttonContainer}>
        <Button title="Trigger Kotlin yo" onPress={handleKotlinPress} color="#841584" />
      </View>

      <View style={styles.buttonContainer}>
        <Button title="Trigger C++ via Kotlin" onPress={handleCppPress} color="#0066cc" />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#fff',
    alignItems: 'center',
    justifyContent: 'center',
  },
  title: {
    fontSize: 20,
    fontWeight: 'bold',
    marginBottom: 40,
  },
  buttonContainer: {
    marginVertical: 10,
    width: '80%',
  }
});