/*
nano_analogtoserial - v.1
MS 10/12/23 - get each analog value, output to serial, comma separated 
*/

const int analogPins[] = {A0, A1, A2, A3, A4};  // Analog pins for your sensors

void setup() {
  Serial.begin(9600);
}

void loop() {
  for (int i = 0; i < 5; i++) {
    int sensorValue = analogRead(analogPins[i]);
    Serial.print(sensorValue);
    
    if (i < 4) {
      Serial.print(",");  // Use a comma as a delimiter between sensor values
    }
  }

  Serial.println();  // Add a newline character to indicate the end of the data

  delay(1000);  // Adjust the delay based on your monitoring frequency
}
