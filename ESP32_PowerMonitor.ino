#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ThingsBoard.h>
#include <Arduino_MQTT_Client.h>
#include "EmonLib.h"
#define TOKEN "YOUR_DEVICE_TOKEN"
#define THINGSBOARD_SERVER "thingsboard.cloud"

//prepares ESP32 to read electrical data, connect to WiFi, and send data to ThingsBoard
EnergyMonitor emon1;
WiFiClient espClient;
Arduino_MQTT_Client
mqttClient(espClient);
ThingsBoard tb(mqttClient);
bool isSending = false; //tracks if data sending is ON or OFF

//OLED configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306
display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//SCT-013-000, AC-AC adapter, LEDs connected here, and OLED+button
const int currentPin = 35;
const int voltagePin = 34;
const int redLedPin = 18;
const int blueLedPin = 2;
const int greenLedPin = 19;
const int buttonPin = 13;
const int sendButtonPin = 14;
int displayMode = 0; //0=Vrms, 1= Real power, 2=PF, 3=Irms

//Calibration constants, you may have to fine-tune these for accuracy
const double Vcal = 58.83 ; //sets calibration constant for AC-AC Adapter
const double Ical = 27.3; //sets calibration constant for SCT-013-000
const double phase = 1.7; //phase shift

//timer for Thingsboard uploads and message on screen when sendButton is pushed
unsigned long lastSendTime = 0;
unsigned long notificationStartTime = 0;
bool isNotifying = false;
const unsigned long sendInterval = 10000;
const unsigned long notifyDuration = 4000;


void setup() {
  Serial.begin(115200);

  analogReadResolution(12); //esp32 is 12-bit resolution

  //set a range of 3.3V to all ADC pins
  analogSetAttenuation(ADC_11db);

  //calibrate SCT013000 and AC-AC Adapter
  emon1.voltage(voltagePin, Vcal, phase);
  emon1.current(currentPin, Ical);

  //Set LED pins as outputs (turns light on)
  pinMode(redLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);
  

  //Initialize the OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 failed"));
    for(;;);
  }
  display.clearDisplay();
  display.display();
  display.setTextColor(WHITE);

  //Initialize Button with internal pull-up
  pinMode(buttonPin, INPUT_PULLUP);

  //Initialize ThingsBoard sendbutton
  pinMode(sendButtonPin, INPUT_PULLUP);
  pinMode(blueLedPin, OUTPUT);
  
  //start with blue LED off
  digitalWrite(blueLedPin, LOW);

  //start wifi
  WiFi.begin("WIFI_SSID", "WIFI_PASSWORD");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWifi Connected");
}

void loop() {
 
  unsigned long currentMillis = millis(); //get the current time

  //ensures active connection between ESP32 and ThingsBoard.
  if(!tb.connected()){
    if(!tb.connect(THINGSBOARD_SERVER,TOKEN)){
      Serial.println("Failed to connect to ThingsBoard");
    }
  }
 
  //calculate and store readings
  emon1.calcVI(20, 2000); //runs all the complex calculations

  double realPower        =  emon1.realPower; //Real Power (watts)
  double apparentPower    =  emon1.apparentPower; //Apparent power (VA)
  double powerFactor      =  emon1.powerFactor; //Power factor
  double supplyCurrent    =  emon1.Irms; //RMS Current
  double supplyVoltage    =  emon1.Vrms; //RMS Voltage

  //print data on serial monitor
  Serial.print("Real Power: ");
  Serial.print(realPower);
  Serial.println("W"); //displays real power in serial monitor

  Serial.print("Apparent Power: ");
  Serial.print(apparentPower);
  Serial.println("VA"); //displays apparent power in serial monitor

  Serial.print("Power Factor: ");
  Serial.println(powerFactor); //displayed the power factor on serial monitor

  Serial.print("RMS Current: ");
  Serial.print(supplyCurrent);
  Serial.println("A"); //displays RMS current on serial monitor

  Serial.print("RMS Voltage: ");
  Serial.print(supplyVoltage);
  Serial.println("V"); //displays RMS voltage on serial monitor

  //check screen button: it will be LOW when pressed due to INPUT_PULLUP
  if (digitalRead(buttonPin) == LOW){
    displayMode++;
 
  if(displayMode > 3) {
    displayMode = 0;
  }
  delay(200); //simple debounce
  }

  //check send button (thingsboard)
  if(digitalRead(sendButtonPin) == LOW) {
    isSending = !isSending;
    digitalWrite(blueLedPin, isSending ? HIGH : LOW);
    isNotifying = true;
    notificationStartTime = currentMillis; 
    delay(500); 
  }

  //update the DISPLAY
  if(isNotifying && (currentMillis - notificationStartTime < notifyDuration)){
    displayStatusMessage(isSending ? "Sending to \nThingsBoard!" : "Stopped sending\nto ThingsBoard..");
  } else {
      isNotifying = false;
      switch(displayMode) {
      case 0: displayValue("Voltage", supplyVoltage, "V"); break;
      case 1: displayValue("Real Power", realPower, "W"); break;
      case 2: displayValue("Power Factor", powerFactor, ""); break;
      case 3: displayValue("Current", supplyCurrent, "A"); break;
      }
    }

  //thingsboard timer for sends (every 10 seconds, ESP32 sends data to Thingsboard to reduce pollution)
  if(isSending && (currentMillis - lastSendTime >= sendInterval)){
    lastSendTime = currentMillis;
    tb.sendTelemetryData("realPower",realPower);
    tb.sendTelemetryData("apparentPower", apparentPower);
    tb.sendTelemetryData("voltage", supplyVoltage);
    tb.sendTelemetryData("current", supplyCurrent);
    tb.sendTelemetryData("powerFactor", powerFactor);
  }

  //Turn on RED LED if the power factor is POOR/BAD
  if(powerFactor < .92){
    digitalWrite(18, HIGH);
    digitalWrite(19, LOW);
  }

  //Turn on GREEN LED if the power factor is GOOD/ACCEPTABLE
  if(powerFactor > .92){
    digitalWrite(18, LOW);
    digitalWrite(19, HIGH);
  }

  tb.loop(); 
}

//guide the ESP32 how to display stuff on OLED screen
void displayValue(String label, double value, String unit) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 5);
    display.print(label);

    display.setTextSize(2);
    display.setCursor(0, 30);
    display.print(value, 2); //show 2 decimal places
    display.print("" + unit);
    display.display();
}

void displayStatusMessage(String message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,5);
  display.print("System Status: ");

  display.setTextSize(1);
  display.setCursor(0,30);
  display.print(message);
  display.display();
}