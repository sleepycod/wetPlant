/*
wetPlant - v.1 
MS 10/12/23 - All should be working - prayge

Notes:
	Had issue with setting "moisture[sensorIndex] = map_moisture;" caused ESP reset.
	Problem was that "int moisture[] = {0};" was declared outside of loop/setup

*/

#include "thingProperties.h"
//#include <SoftwareSerial.h>
//#include <Regex.h>

//SoftwareSerial nanoSerial(D2, D3); // RX, TX
const int relayPins[] = {D0, D5, D6, D7, D8}; // Array of relay control pins
const int numRelays = 5; //sizeof(relayPins); // Number of relays
int active_pump; // which pump active

unsigned long startedPumpTime;
const int pumpCooldown = 30; //seconds between starting pump, debouncer and to allow water to permeate to sensor
int lowMoistureCount[numRelays] = {0};
const int lowMoistureCountLimit = 5; // unsuccessful attempt to water limit - count relay-on cycles before raise no-water error

int moisture_low_values[numRelays]; // Array of moisture_low values
int pump_runtime_values[numRelays]; // Array of pump_runtime values
int moisture_low_default = 20;  // Set default
int pump_runtime_default = 5;  // Set default

String msg; // for logging
int checkCounter = 0; // Counter to track the number of iterations

const int nanoresetPin = D3; // pin connected to nano reset pin
int invalidFormatCount = 0; // how many times invalid data recieved from nano

int wet = 920;// sensor value for wet reading
int dry = 175;// sensor value for dry reading

int moisture[numRelays] = {50}; // Array to store moisture values for each sensor

// sensor value smoothing 
const int numSensors = 5; // Number of sensors
const int numReadings = 15; // Number of readings to average per sensor
int readings[numSensors][numReadings]; // Array to store readings for each sensor
int readindex[numSensors] = {0}; // Array to store the index for each sensor's readings

void setup() {
  //Serial.begin(115200); // Initialize Serial Monitor
  Serial.begin(9600); // Initialize Serial Monitor
  //nanoSerial.begin(9600); // Initialize SoftwareSerial for Nano communication
  delay(500);
  
  pinMode(nanoresetPin, OUTPUT); // Set nanoresetPin as OUTPUT
  digitalWrite(nanoresetPin, HIGH); // Hold in nano reset button until later - bc nano tx/rx connection causing reset??? (voltage issue?)
  
  for (int i = 0; i < numRelays; i++) {
    pinMode(relayPins[i], OUTPUT); // Set relay pins as output
    digitalWrite(relayPins[i], HIGH); // turn on relays
    delay(100);
    digitalWrite(relayPins[i], LOW); // turn off relays
  }

  // Make sure the pump is not running
  stopPump();

  // Connect to Arduino IoT Cloud
  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(4);
  ArduinoCloud.printDebugInfo();
  
  // Initialize defaults
  for (int i = 0; i < numRelays; ++i) {
    moisture_low_values[i] = moisture_low_default;  // Set default
    pump_runtime_values[i] = pump_runtime_default;  // Set default
  }
  plant_select = 0; // Set default
  
  // Initialize the readings arrays and indexes for each sensor
  for (int i = 0; i < numSensors; i++) {
    for (int j = 0; j < numReadings; j++) {
  	readings[i][j] = 0;
    }
    readindex[i] = 0;
  }

}

void loop() {
  ArduinoCloud.update();
  
  // Stop watering after the configured duration
  if (pump_on && (millis() - startedPumpTime) >= pump_runtime_values[active_pump]*1000) {
    stopPump();
  }
  
  //get data from nano, check format valid (put this to a function)
  bool validFormat = false;
  String moisture_str = "";
  if ((!pump_on) && Serial.available()) { // if pumping dont even read serial
    String discard_str = Serial.readStringUntil('\n'); // First discard data until newline character (incase nano halfway through output)
    moisture_str = Serial.readStringUntil('\n'); // Read incoming data until newline character is encountered
	
	if (isValidFormat(moisture_str, numRelays)) {
	  validFormat = true;
	  invalidFormatCount=0;
	} else {
	  validFormat = false;
	  invalidFormatCount++; // increment, if we go over some limit maybe need to reset nano or smth
	  status = "Invalid format! data:"+String(moisture_str)+" - (invalidcount:"+String(invalidFormatCount)+")";
	  // Handle the case where the input format is invalid
	}
  }
  
  // store recieved moisture values 
  // sensors get affected by relays switching so dont measure while pump on
  // also we busy pumping so pay attention to that
  // * move !pump_on up somewhere so nothing happens during.
  if ((!pump_on) && validFormat) {
    int sensorIndex = 0;
    int lastIndex = 0;

	for (int i = 0; i < moisture_str.length(); i++) {
	  if (moisture_str.charAt(i) == ',' || i == moisture_str.length() - 1) {
		String number = moisture_str.substring(lastIndex, i);
		int raw_moisture = number.toInt(); // Convert substring to an integer
		int map_moisture = map(raw_moisture, wet, dry, 0, 100);

		if (map_moisture >= 0 && map_moisture <= 100) {
		  // Store the reading in the array for the corresponding sensor
		  readings[sensorIndex][readindex[sensorIndex]] = map_moisture;

		  // Calculate the average of the readings for the current sensor
		  int total = 0;
		  for (int j = 0; j < numReadings; j++) {
			total += readings[sensorIndex][j];
		  }
		  moisture[sensorIndex] = round(total / numReadings);
		  
		  // Assign each value to corresponding moisture variable for arduino cloud
		  switch (sensorIndex) {
			  case 0: moisture0 = moisture[sensorIndex]; break;
			  case 1: moisture1 = moisture[sensorIndex]; break;
			  case 2: moisture2 = moisture[sensorIndex]; break;
			  case 3: moisture3 = moisture[sensorIndex]; break;
			  case 4: moisture4 = moisture[sensorIndex]; break;
			  default: break;
			}
		  // Increment index for the next reading and wrap around if needed
		  readindex[sensorIndex]++;
		  if (readindex[sensorIndex] >= numReadings) {
		    readindex[sensorIndex] = 0;
		  }
		} else {
		  status = "moisture out of range! sensor:" + String(sensorIndex) + " - (map_moisture:" + String(map_moisture) + " - raw_moisture:" + String(raw_moisture) + ")";
		  moisture[sensorIndex] = raw_moisture;  // when invalid set to max to prevent watering (FOR NOW THIS IS RAW SO I CAN SEE THE VALUES)
		}
		
		sensorIndex++;
		lastIndex = i + 1;
	  }
	}
  }
	
	// Start watering when moisture below limit
	// Check !pump_on and pumpCooldown seconds have passed since last pump 
	if ((!pump_on) && validFormat && (millis()-startedPumpTime) >= pumpCooldown*1000) {
	  // Generate a random starting index, prevent rewatering one plant
	  int randomStartIndex = random(numRelays);
	  for (int j = 0; j < numRelays; ++j) {
		int i = (j + randomStartIndex) % numRelays; // Ensures cycling through indices without repetition
		// Check moisture levels against moisture_low and trigger pump (if lowMoistureCountLimit+1 not reached)
		// lowMoistureCountLimit+1 lets the status be set from startPump function
		// could remove this check if startPump isnt going to spam 
		if ((moisture[i] < moisture_low_values[i]) && (lowMoistureCount[i] < lowMoistureCountLimit+1)) {
		  //Serial.println("moisture[" + String(i) + "] < moisture_low_value[" + String(i) + "] ");
		  //Serial.println("lowMoistureIncrement[" + String(i) + "]: " + String(lowMoistureCount[i]) + " ");
		  startPump(i); // Start the specific pump
		  lowMoistureCount[i]++; // Increment count if moisture low
		}
		if (moisture[i] >= moisture_low_values[i]) {
		  lowMoistureCount[i] = 0; // Reset count if moisture not low
		}
	  }
	}
  
  delay(1000);
}

// This function is triggered whenever the server sends a change event,
// which means that someone changed a value remotely and we need to do
// something.
void onPumpOnChange() {
  if (pump_on) {
    startPump(plant_select);
  } else {
    stopPump();
  }
}

void startPump(int pump) {
  //Serial.println("Entered startPump(" + String(pump) + ")");
  //plant_select = pump; // set plant_select to pump 
  //onPlantSelectChange(); // call to get values and log that plant_select has changed
  unsigned long currentTime = millis();
  // Check if attempt limit reached
  if (lowMoistureCount[pump] >= lowMoistureCountLimit) {
    msg = "Error on pump " + String(pump) + " : unsuccessful attempts to water limit reached";
    //Serial.println(msg);
    status = msg;
    return;
  }
  // Activate the relay (turn on a pump)
  if (pump >= 0 && pump < numRelays) {
    digitalWrite(relayPins[pump], HIGH);
    pump_on = true; // set pump_on flag true
    active_pump = pump; // flag active pump
    startedPumpTime = currentTime; // Update the last activation time for the relay
    msg = "Pump " + String(pump) + " started";
    //Serial.println(msg);
    status = msg;
    return;
  }
}

void stopPump() {
  pump_on = false;
  // Deactivate all relays (turn off all pump)
  for (int i = 0; i < numRelays; i++) {
    digitalWrite(relayPins[i], LOW);
  }
  msg = "Pump stopped";
  //Serial.println(msg);
  status = msg;
}

void onPumpRuntimeChange() {
  // Your code for handling changes in pump runtime
  pump_runtime_values[plant_select] = pump_runtime;
  msg = "plant_select: " + String(plant_select) ;
  msg = msg + " | pump_runtime: " + String(pump_runtime);
  //Serial.println(msg);
  status = msg;
}

void onMoistureLowChange() {
  // Your code for handling changes in moisture low value
  moisture_low_values[plant_select] = moisture_low;
  msg = "plant_select: " + String(plant_select);
  msg = msg + " | moisture_low: " + String(moisture_low);
  //Serial.println(msg);
  status = msg;
  lowMoistureCount[plant_select] = 0; // Resets moisture low counter - allows manual reset
}

void onPlantSelectChange() { // Fetch the selected values
  if (plant_select >= 0 && plant_select < sizeof(moisture_low_values)) {
    // Fetch corresponding moisture_low and pump_runtime values from arrays
    moisture_low = moisture_low_values[plant_select];
    pump_runtime = pump_runtime_values[plant_select];
  }
  msg = "plant_select: " + String(plant_select);
  msg = msg + " | moisture_low: " + String(moisture_low);
  msg = msg + " | pump_runtime: " + String(pump_runtime);
  //Serial.println(msg);
  status = msg;
}

int getTokenCount(const String& input, char delimiter) {
  char inputStr[input.length() + 1];
  input.toCharArray(inputStr, input.length() + 1);

  char* token = strtok(inputStr, &delimiter);
  int count = 0;

  while (token != NULL) {
    count++;
    token = strtok(NULL, &delimiter);
  }

  return count;
}

bool isValidFormat(const String& input, int validTokenCount) {
  char delimiter = ',';
  int tokenCount = getTokenCount(input, delimiter);

  if (tokenCount == validTokenCount) {
    return true;
  } else {
    return false;
  }
}
