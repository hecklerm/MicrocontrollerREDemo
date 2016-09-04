/*
 * REDemo.ino
 * 
 * This code reads all the various sensors (humidity, temperature, amps, volts) & 
 * reports it over serial connection.
 * 
 * Author: Mark A. Heckler (@MkHeck, mark.heckler@gmail.com)
 * 
 * License: MIT License
 * 
 * Please use freely with attribution. Thank you!
 */

#include <Wire.h>
#include <dht11.h>
#include <Adafruit_INA219.h>

/*
 * Hardware configuration
 */
// Set up sensors
dht11 DHT11;
Adafruit_INA219 ina219;

// Hardware pin definitions
// Digital I/O pins
const int DHT_PIN = 2;
const int POWER_FAN = 5;
const int INT_LIGHT_PIN = 12; // Interior lighting
const int LIGHT_PIN = 13;     // Status light

// Other constants
const int LOWER_TEMP = 0;
const int UPPER_TEMP = 30;
const int NEGATIVE = 0;
const int AFFIRMATIVE = 1;

// Other sensor variables
int chk;
float busVoltage;
float shuntVoltage;
float current_mA;
float loadVoltage;

// State, comm, & other variables
int inByte;
boolean isAutonomous = true;
boolean isLightOn = false;
boolean isPowerOn = false;
boolean isIntLightOn = true; // This allows us to "force sync" it off upon startup
boolean isExtLightOn = false;
int areWindowsOpen = false;
int powerOnSeconds = 0;


void setup(void) {
  Serial.begin(9600);
  Serial.println("Initializing...");

  DHT11.attach(DHT_PIN);
  ina219.begin();

  /*
   * Initialize pin(s)
   */
  pinMode(INT_LIGHT_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(POWER_FAN, OUTPUT);

  Serial.println("Sensors initialized and online!");
  
  interiorLightOff();
}

void loop(void) {
  // Get the temp/humidity
  chk = DHT11.read();
  //Serial.print("Temp=");
  //Serial.println(DHT11.temperature);

  // Current & voltage readings
  getEnergyReadings();
  
  // Get the current status of outputs, build the message, & send it to output (the Pi)
  Serial.println(buildMessage(getStatus()));

  if (loadVoltage > 2 && loadVoltage < 12.25 && powerOnSeconds > 60) {
    // If V < 11.8V, battery is drained
    lightOff();
    powerOff();
  } else {
    if (Serial.available() > 0) {
      Serial.print("Incoming character...");
      inByte = Serial.read();
      Serial.println(inByte);
  
      switch (inByte) {
      case 'A':
        // Enable automatic power/light management
        isAutonomous = true;
        break;
      case 'a':
        // Disable automatic power/light management
        isAutonomous = false;
        break;
      default:
        if (!isAutonomous) {  // Only act on inputs if isAutonomous is overridden
          actOnInput(inByte);
        }
        break;
      }    
    }
    
    if (isAutonomous) {
      if (DHT11.temperature > LOWER_TEMP && DHT11.temperature < UPPER_TEMP) {
        // Temperature is within desired range...(Celsius)
        // Extinguish power (heat/fan), ignite "ready" light.
        if (powerOnSeconds > 60) {
          powerOff();
          lightOn();
          //Serial.println("lightOn, temp in range");
        } else {
          powerOnSeconds++;
        }
      } else {
        if (loadVoltage > 12.50) {
          // Temperature is out of desired range...
          // Engage heat/fan (depending upon season) and extinguish light.
          // If V too low, though, it can't sustain the heater power draw.
          powerOn();
          lightOff();
          //Serial.println("lightOff, temp not in range");
        } else {
          if (powerOnSeconds > 60) {
            powerOff();
            lightOn();
            //Serial.println("lightOn, battery low");
          } else {
            powerOnSeconds++;
          }
        }
      }
    }    
  }

  delay(1000);
}

/*
 * Output processing
 */
void getEnergyReadings() {
  // Get the current/voltage readings
  busVoltage = ina219.getBusVoltage_V();
  shuntVoltage = ina219.getShuntVoltage_mV();
  current_mA = ina219.getCurrent_mA();
  loadVoltage = busVoltage + (shuntVoltage / 1000);

  if (loadVoltage < 0 || loadVoltage > 18) {
    // Disconnected, provide reasonable values for testing
    loadVoltage = 12.25;
    current_mA = 222;
  }
}
 
int getStatus() {
  int status = 0;
  
  if (isAutonomous) {
    status += 32;
  }
  if (isPowerOn) {
    status += 16;
  }
  if (isLightOn) {
    status += 8;
  }
  if (areWindowsOpen == AFFIRMATIVE) {
    status +=4;
  }
  if (isIntLightOn) {
    status += 2;
  }
  if (isExtLightOn) {
    status += 1;
  }
  
  return status;
}

String buildMessage(int status) {
    // Build the message to transmit
    String msg = "{";
    msg += long(DHT11.humidity * 100);
    msg += ",";
    msg += long(DHT11.temperature * 100);
    msg += ",";
    msg += int(loadVoltage * 1000);
    msg += ",";
    msg += int(current_mA);
    msg += ",";
    msg += status;
    msg += "}";

    return msg;
}

/*
 * Input processing
 */
void actOnInput(int inByte) {
  switch (inByte) {
  case 'L':
    lightOn();
    break;
  case 'l':
    lightOff();
    break;
  case 'P':
    powerOn();
    break;
  case 'p':
    powerOff();
    break;
  case 'I':
    interiorLightOn();
    break;
  case 'i':
    interiorLightOff();
    break;
  }
}

/*
 * Physical interface functions
 */
void lightOn() {
  // Turn on light (if not on already)
  if (!isLightOn) {
    digitalWrite(LIGHT_PIN, HIGH);
    isLightOn = true;
  }
}

void lightOff() {
  // Turn off light (if on)
  if (isLightOn) {
    digitalWrite(LIGHT_PIN, LOW);
    isLightOn = false;
  }
}

void powerOn() {
  // Turn on power (if not on already)
  if (!isPowerOn) {
    digitalWrite(POWER_FAN, HIGH);
    isPowerOn = true;
  }
  powerOnSeconds++;  // increment power on counter
}

void powerOff() {
  // Turn off power (if on)
  if (isPowerOn) {
    // Shut both off, regardless
    digitalWrite(POWER_FAN, LOW);
    isPowerOn = false;
  }
  powerOnSeconds = 0;  // reset counter for time power is on
}

void interiorLightOn() {
  // Turn on interior light (if not on already)
  if (!isIntLightOn) {
    digitalWrite(INT_LIGHT_PIN, HIGH);
    isIntLightOn = true;
  }
}

void interiorLightOff() {
  // Turn off interior light (if on)
  if (isIntLightOn) {
    digitalWrite(INT_LIGHT_PIN, LOW);
    isIntLightOn = false;
  }
}

