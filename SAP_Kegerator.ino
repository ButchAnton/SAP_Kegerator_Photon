#include "OneWire.h"
#include "spark-dallas-temperature.h"
#include "HttpClient.h"

// A couple of utility functions that sort of work like a general
// printf statement.  Sort of.

void pnprintf(Print& p, uint16_t bufsize, const char* fmt, ...) {
   char buff[bufsize];
   va_list args;
   va_start(args, fmt);
   vsnprintf(buff, bufsize, fmt, args);
   va_end(args);
   p.println(buff);
}

#define pprintf(p, fmt, ...) pnprintf(p, 128, fmt, __VA_ARGS__)
#define Serial_printf(fmt, ...) pprintf(Serial, fmt, __VA_ARGS__)

void examples() {
   pnprintf(Serial, 64, "%s", "2123");
   pprintf(Serial, "%d", 123);
   Serial_printf("%d", 123);
}

//
// HTTP configuration
//

// This is an HCP ondemand trial instance.  Note that HCP accepts
// HTTP or HTTPS connections, but HTTP connections are just 302'ed
// to HTTPS, so we can't do it directly right now (until we have
// a good SSL/TLS solution for the Particle Photon and Arduino).
// To get around this, we use an AWS instance running Nginx, which
// proxies the incoming HTTP request to HCP over HTTPS.

/// String server = "kegeratori831533trial.hanatrial.ondemand.com";
String server = "ec2-54-166-47-147.compute-1.amazonaws.com";
int port = 80;
char *path = "/sap/devs/demo/iot/services/iot_input.xsodata/";
HttpClient http;
http_request_t request;
http_response_t response;
http_header_t headers[] = {
    {"Content-Type", "application/json"},
    {"Accept" , "application/json" },
    {"Authorization", "Basic U1lTVEVNOlBhc3N3MHJk"},
    {NULL, NULL} // NOTE: Always terminate headers with NULL
};



//
// Timekeeping
//

long currentTime = 0;

//
// Setup for flow meter
//

int flowPin = D1;  // Can't use pin 0
long clicks = 0;
long count = 0;

//
// Setup for temperature sensor
//

int tempSensorPin = D2;
OneWire oneWire(tempSensorPin);
DallasTemperature sensors(&oneWire);
float tempC = 0.0;
float tempF = 0.0;
long temperatureReportTime = 0;
#define TEMPERATURE_REPORTING_INTERVAL 1000 // ms

// Have we finished the pour?
#define PULSE_LATENCY (200) // ms between pulses
#define POUR_MINIMUM (.40)  // minimum pour volume in oz
// #define PULSES_PER_GALLON (1703.25)   // Adafruit flow meter
#define PULSES_PER_GALLON (10313.00)   // FT-330 flow meter
long pulses = 0;  // Number of pulses
long timeToEndPour = 0; // When we should consider a pour ended

//
// Setup for door switch
//

int doorSwitchPin = D3; // D3 to one outside pin, GND to the other outside pin (center pin unconnected)
#define DOOR_OPEN 0     // May need to change depending on build
#define DOOR_CLOSED 1   // May need to change depending on build
int doorState = DOOR_CLOSED;
int previousDoorState = DOOR_CLOSED;

//
// Temperature sensor functions
//

float getTemperatureInF() {
  tempF = 0.0;
  sensors.requestTemperatures();
  tempF = sensors.getTempFByIndex(0);
  if (tempF != DEVICE_DISCONNECTED_F) {
    // Serial.print(tempF); Serial.println("F");
  }
  return tempF;
}

float getTemperatureInC() {
  tempC = 0.0;
  sensors.requestTemperatures();
  tempC = sensors.getTempCByIndex(0);
  if (tempC > DEVICE_DISCONNECTED_C) {
    // Serial.print(tempC); Serial.println("C");
  }
  return tempC;
}

void reportTemperature() {
  double temperature = getTemperatureInF();
  if (temperature > DEVICE_DISCONNECTED_F) {
    Serial_printf("************** POSTing temperature: %f", temperature);
    postTemperatureToHana(temperature);
  }
}

//
// Flow meter functions
//

void countPulses() {
  pulses++;
  timeToEndPour = millis() + PULSE_LATENCY;
  Serial_printf("Got a pulse!  Pulse = %d", pulses);
}

void reportPour(float volume) {
  Serial_printf("############# POSTing pour: %f", volume);
  postPourToHana(volume);
}

//
// Door switch functions
//

// Detemine if the door is open or closed.
// Returns 0 if the door is open, 1 otherwise.

int getDoorState() {
  int state = digitalRead(doorSwitchPin);
  return(state);
}

void reportDoorState(int doorIsOpen) {
  Serial_printf("================= POSTing door: %s", (DOOR_OPEN == doorState) ? "Open" : "Closed");
  if (DOOR_OPEN == doorState) {
    postDoorToHana(1);  // Door is open.
  } else {
    postDoorToHana(0);  // Door is closed.
  }
}

//
// HANA posting functions
//

void postToHana(String endpoint, String body) {

  request.hostname = server;
  request.port = port;
  request.path = path + endpoint;
  request.body = body;
#ifdef DEBUG
  Serial.println("==================== Request body =======================");
  Serial.println(body);
  Serial.println("=================== End of request ======================");
#endif // DEBUG

  http.post(request, response, headers);

#ifdef DEBUG
  Serial.println("=================== Response status =====================");
  Serial.print("***** POST Response status: ");
  Serial.println(response.status);
  Serial.println("==================== Response body ======================");
  Serial.println(response.body);
  Serial.println("=================== End of response =====================");
#endif // DEBUG
}

void postTemperatureToHana(float temperature) {
  String endpoint = "temp";
  String body = "{\"MeasureID\" : \"-1\", \"Temp\" : \"" + String(temperature) + "\", \"Unit\" : \"F\"}";
  postToHana(endpoint, body);
}

void postDoorToHana(int doorOpen) {
  String endpoint = "door";
  String body = "{\"ActivityID\" : \"-1\", \"Open\" : " + String(doorOpen) + "}";
  postToHana(endpoint, body);
}

void postPourToHana(float pour) {
  String endpoint = "flow";
  String body = "{\"PourID\" : \"-1\", \"Pour\" : \"" + String(pour) + "\", \"Unit\" : \"oz\"}";
  postToHana(endpoint, body);
}

void setup() {
  // Flow meter
  pinMode(flowPin, INPUT);
  attachInterrupt(flowPin, countPulses, RISING);
  // Temperature sensor
  sensors.begin();
  sensors.setResolution(TEMP_12_BIT);
  // Door switch
  pinMode(doorSwitchPin, INPUT_PULLUP);
  Serial.begin(9600);
}

void loop() {

  // Check the door state for change.

  doorState = getDoorState();
  // Serial.print("doorIsOpen = "); Serial.println(doorIsOpen);
  if (doorState != previousDoorState) {
    reportDoorState(doorState);
    previousDoorState = doorState;
  }

  // Check to see if we should report temperature.

  if (millis() > temperatureReportTime) {
    reportTemperature();
    temperatureReportTime = millis() + TEMPERATURE_REPORTING_INTERVAL;
  }

  // Check to see if we have a pour.  If so, report it.
  // We have a pour if the current time is greater than the last time
  // we recorded a pour plus the time between pours, and the pour
  // volume is greater than the minimum pour volume.
  if ((millis() > timeToEndPour) && pulses != 0) {
    float gallons = pulses / PULSES_PER_GALLON;
    float ounces = gallons * 128.0;
    // Ignore pours less than some number of ounces
    if (ounces > POUR_MINIMUM) {
      reportPour(ounces);
    }
    pulses = 0;
  }

}
