/*
 * Noco-lab: thermo-hygro-stat
 * 
 * Hardware: 
 *   - Arduino Uno (can be replaced with any other arduino)
 *   - LCD shield with keypad
 *   - DHT11 termo+humidity sensor
 *   - DS18B20 sensor for outdoor
 *   - 2 Relay module
 *   
 * Libraries to install:
 *   - DHT sensor library by Adafruit
 *   - OneWire
 *   - DallasTemperature
 */

#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

/******************************************************************************/
/* Pins and other config                                                      */
/******************************************************************************/

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
DHT dht(11, DHT11);  // DHT11 module (temp/humid sensor)

const int RELAY_TEMP_PIN = 3;
const int RELAY_HUMID_PIN = 2;
const int ONE_WIRE_BUS = 13;  // For DS18B20 (outdoor temperature sensor)

const long KEY_TIMEOUT = 30000; // in ms
const long SENSORS_READ_PERIOD = 5000; // in ms, DHT sensor is slow, so 5+ seconds is a good choice

/******************************************************************************/
/* Definitions                                                                */
/******************************************************************************/

enum Key
{
  right,
  up,
  down,
  left,
  select,
  none
};

enum Mode 
{ 
  idle,
  setLowTemp,
  setHighTemp,
  setLowHumid,
  setHighHumid 
};

struct Config
{
  byte lowTemp;
  byte highTemp;
  byte lowHumid;
  byte highHumid;
};

struct State
{
  Mode mode;
  float temp;
  float humid;
  float outdoorTemp;
  bool tempOn;
  bool humidOn;
};

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature dsSensors(&oneWire);
DeviceAddress outdoorSensorAddress;

State state;
Config config;

/******************************************************************************/
/* Functions                                                                  */
/******************************************************************************/

void setup() 
{
  pinMode(RELAY_TEMP_PIN, OUTPUT);
  pinMode(RELAY_HUMID_PIN, OUTPUT);
  processRelays(); // Make sure relays are off at startup
   
  state.mode = idle;
  restoreConfig();
  lcd.begin(16, 2);
  dht.begin();
  dsSensors.begin();
  if (! dsSensors.getAddress(outdoorSensorAddress, 0))
  {
    lcd.setCursor(0, 0);
    lcd.print("ERROR: can't get");
    lcd.setCursor(0, 1);
    lcd.print("DS18B20 address");
    //delay(1000000);
  }
  

  lcd.setCursor(0, 0);
  lcd.print("  Poconoco-lab  ");
  lcd.setCursor(0, 1);
  lcd.print("Thermohygrostat");
  delay(1000);
}

void loop() {
  switch(state.mode)
  {
    case idle         : loopIdle(); break;
    case setLowTemp   : loopSetup("low temp", "\xDF""C", config.lowTemp, 1); break;
    case setHighTemp  : loopSetup("high temp", "\xDF""C", config.highTemp, 1); break;
    case setLowHumid  : loopSetup("low humid", "%", config.lowHumid, 5); break;
    case setHighHumid : loopSetup("high humid", "%", config.highHumid, 5); break;
  }
}

void loopIdle() {
  processIU();
  processSensors();
  processClimate();
  processRelays();
}

void loopSetup(const char* parameterName, const char* units, byte &parameter, byte step) 
{
  processKeys(&parameter, step);
  displaySetup(parameterName, units, parameter);
}

void displaySetup(const char* parameterName, const char* units, const byte parameter) 
{
  char line[17];
  snprintf(line, 17, "Set %s:                ", parameterName);
  lcd.setCursor(0, 0);
  lcd.print(line);

  lcd.setCursor(0, 1);
  lcd.print(parameter);
  lcd.print(units);
  lcd.print("               ");
}

void processIU()
{
  displayIdle();
  processKeys(NULL, 0);
}

void displayIdle()
{
  char line[17];

  snprintf(line, 7, "%d\xDF""C     ", int(state.temp)); 
  line[7]=0;
  lcd.setCursor(0, 0);
  lcd.print(line);

  snprintf(line, 8, "%d\xDF""C     ", int(state.outdoorTemp)); 
  line[14]=0;
  lcd.setCursor(6, 0);
  lcd.print(line);
  
  snprintf(line, 4, "%d%%     ", int(state.humid));
  lcd.setCursor(13, 0);
  lcd.print(line);

  lcd.setCursor(0, 1);
  lcd.print(state.tempOn ? "Heat on  " : "Heat off ");
  lcd.setCursor(9, 1);
  lcd.print(state.humidOn ? "Fan on  " : "Fan off ");
}

void processKeys(byte *parameter, byte step) 
{
  static Key lastKey = none;
  static long lastTime = 0;
  Key key = readKey();

  const long now = millis();

  if (key == lastKey)
  {
    // Nothing changed
    const long now = millis();
    if (state.mode != idle && lastTime > 0 && now - lastTime > KEY_TIMEOUT)
      toggleModeToIdle();
    return;
  }

  lastKey = key;
  lastTime = now;

  if (key == select)
  {
    toggleMode();
    return;
  }

  if (state.mode == idle)
    return;

  // Other modes are editing config
  if (parameter == NULL)
    return;
  
  if (key == up)
    *parameter += step;
  else if (key == down)
    *parameter -= step;
}

void toggleMode()
{
  if (state.mode == setHighHumid)
    toggleModeToIdle();
  else
  {
    state.mode = Mode(state.mode + 1);
  }
}

void toggleModeToIdle()
{
  state.mode = idle;
  writeConfig();  
}

void processSensors() 
{
  static long lastReadTime = 0;
  const long now = millis();

  if (now - lastReadTime > SENSORS_READ_PERIOD)
  {
    lastReadTime = now;
    // Each read can take up to 250 mseconds, so calling processKeysIdle()
    // in between in an attempt to keep keys responsive
    
    state.temp = dht.readTemperature();
    processIU();
    state.humid = dht.readHumidity();
    processIU();
    dsSensors.requestTemperatures(); 
    processIU();
    state.outdoorTemp = dsSensors.getTempC(outdoorSensorAddress);
    processIU();
  }
}

bool isValidTemp(float temp)
{
  return temp > -90 && temp < 90;
}

bool isValidHumid(float humid)
{
  return humid >= 10 && humid <= 100;
}

void processClimate()
{
  // Safety fallback to turn everything off in case of no data or reading error
  if (! isValidTemp(state.temp) 
          || ! isValidHumid(state.humid)
          || ! isValidTemp(state.outdoorTemp))
  {
    state.tempOn = false;
    state.humidOn = false;
    return;
  }

  if (state.temp <= config.lowTemp)
    state.tempOn = true; // Turn on heater when temp is too low
  else if (state.temp >= config.highTemp)
    state.tempOn = false; // Turn off hetaer if temp is high
  // else if within range, keep prev heater state

  if (state.outdoorTemp < state.temp)
  {
    state.humidOn = false; // Keep fan turned off if outside is colder than inside
  }
  else
  {
    if (state.humid <= config.lowHumid)
      state.humidOn = false; // Turn fan off if humid is low
    else if (state.humid >= config.highHumid)
      state.humidOn = true; // Turn fan on if humid is too high
    // else if within range, keep prev fan state
  }
}

int boolToDigital(bool val)
{
  return val ? LOW : HIGH;
}

void processRelays() 
{
  digitalWrite(RELAY_TEMP_PIN, boolToDigital(state.tempOn));
  digitalWrite(RELAY_HUMID_PIN, boolToDigital(state.humidOn));
}

Key readKey() 
{
  int analogKey = analogRead(0);
  
  if (analogKey < 60) 
    return right;

  if (analogKey < 200)
    return up;

  if (analogKey < 400)
    return down;

  if (analogKey < 600)
    return left;

  if (analogKey < 800)
    return select;

  return none;
}

void resetDefaultConfig() {
  config.lowTemp = 4;
  config.highTemp = 6;
  config.lowHumid = 60;
  config.highHumid = 70;
}

void restoreConfig() {
//  EEPROM.get(0, config);
  
//  if (config.lowTemp == 0xFF)
    resetDefaultConfig();
}

void writeConfig()
{
//  EEPROM.put(0, config);
}
