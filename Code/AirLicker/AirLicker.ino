#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_AHT10.h>
#include "Adafruit_CCS811.h"
#include <WiFi.h>
#include <NTPClient.h>         // Include NTPClient library      
#include <Time.h>           // Include Arduino time library    
#include <WiFiUdp.h>
#include <ThingSpeak.h>
#include <HardwareSerial.h>
#include <s8_uart.h>

//Initialize ThingSpeak communication
unsigned long myChannelNumber = 1;
const char * myWriteAPIKey = "481SMA4B15DTMFZ2";
WiFiClient client;
// Initialize timer variables for update the channel, lastTime hold the last update clock time
// messageTimer hold the desire data sampling rate (ms)
unsigned long lastTime = 0;
unsigned long messageTimer = 15000;

// Initialize AirSense S8 via SerialPort2
HardwareSerial senseAir(2);  //using UART2
byte readCO2[] = {0xFE, 0X04, 0X00, 0X03, 0X00, 0X01, 0XD5, 0xC5};  //Command packet to read Co2 (see app note)
byte response[] = {0,0,0,0,0,0,0};  //create an array to store the response
S8_UART *sensor_S8;
S8_sensor sensor;

// Initialize CCS811 object
Adafruit_CCS811 ccs;
// Initialize AHT10 object
Adafruit_AHT10 aht;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
String formattedDate;
String dayStamp;
String timeStamp;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int eCO2 = 0;
int TVOC = 0;
bool Error = 0;
const char* ssid     = "Linksys"; // Change this to your WiFi SSID
const char* password = "11111111s"; // Change this to your WiFi password
// Dummy function declaration for compiling
void printWelcome();
void printInitStatus();
void printHeadline(String Time);
void printCCS811(int eCO2, int TVOC, bool Error);
float getCO2(byte packet[]);
void printTemp();
void printHumid();
void printWifi(int Status);
String getTime();
void printSenseAir(int CO2);

void setup() {
  int Status = 0;
  int Counter = 0;
  Serial.begin(19200);
  // put your setup code here, to run once:
  Serial2.begin(9600, SERIAL_8N1, 16, 17); // Init SerialPort for S8 sensor;
  sensor_S8 = new S8_UART(Serial2);
  Serial.println(">>> SenseAir S8 NDIR CO2 sensor <<<");
  printf("Firmware version: %s\n", sensor.firm_version);
  sensor.sensor_id = sensor_S8->get_sensor_ID();
  Serial.print("Sensor ID: 0x"); printIntToHex(sensor.sensor_id, 4); Serial.println("");

  Serial.println("Setup done!");
  Serial.flush();
  sensor_S8->get_firmware_version(sensor.firm_version);
  int len = strlen(sensor.firm_version);
  if (len == 0) {
      Serial.println("SenseAir S8 CO2 sensor not found!");
      while (1) { delay(1); };
  }
  //SSD1306 OLED init

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  printWelcome();
  
  // WiFi init section
  display.clearDisplay();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      Counter++;
      delay(100);
      Status = 1;
      printWifi(Status);
      if (Counter >= 50) {
        Status = 3;
        break;
      }
    }
  // WiFi Init success
  if (WiFi.status() == WL_CONNECTED) {
    Status = 2;
  }
  printWifi(Status);

  // Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(25200);

  // AHT10 Temp and Humid sensor init
    if (! aht.begin()) {
    Serial.println("Could not find AHT10? Check wiring");
    while (1) delay(10);
  }
    Serial.println("AHT10 found");
    
  // CCS811 eCO2 and TVOC start
    Serial.println("CCS811 test");
  if(!ccs.begin()){
    Serial.println("Failed to start sensor! Please check your wiring.");
    while(1);
  }
  // Wait for the sensor to be ready
  while(!ccs.available());
  // Initialize ThingSpeak connection
  ThingSpeak.begin(client);  // Initialize ThingSpeak
  // Init OK Status
  printInitStatus();
}

void loop() {
  // put your main code here, to run repeatedly:
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);// populate temp and humidity objects with fresh data
  //Serial.print("Temperature: "); Serial.print(temp.temperature); Serial.println(" degrees C");
  //Serial.print("Humidity: "); Serial.print(humidity.relative_humidity); Serial.println("% rH");
  // Start reading sensors values and print on the OLED
  String Time = getTime();
  display.clearDisplay();
  // Print the Yellow Headline
  printHeadline(Time);
  // Print the temp from AHT10
  printTemp(temp.temperature);
  // Print the humid from AHT10
  printHumid(humidity.relative_humidity);
  // Print CS811
  int eCO2 = ccs.geteCO2();
  int TVOC = ccs.getTVOC();
  printCCS811(eCO2, TVOC, ccs.readData());
  sensor.co2 = sensor_S8->get_co2();
  int CO2 = sensor.co2;
  printSenseAir(CO2);
  display.display();

  // ThingSpeak related code, will be updated in v1.3
  if (millis() - lastTime > messageTimer) {
  ThingSpeak.setField(1, temp.temperature);
  ThingSpeak.setField(2, CO2);
  ThingSpeak.setField(3, humidity.relative_humidity);  
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  lastTime = millis();
  }
  delay(500);
}
/* Welcome Function */
void printWelcome(void) {
  display.clearDisplay();
  display.setTextSize(2);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(10,20);             // Start at top-left corner
  display.println("AirLicker");
  display.setTextSize(0.5);
  display.setCursor(70,50);
  display.println("Majik.bee");
  display.display();
  delay(1000);
}

/* Wifi init Function */
void printWifi(int Status) {
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(10,20);
  display.println("WiFi Connecting");             // Start at top-left corner
  if (Status == 1) {
    display.print(".");
  }
  else if (Status == 2) {
    display.clearDisplay();
    display.println("WiFi Connected");
    display.print("IP:"); display.println(WiFi.localIP());
    display.display();
    delay(1000);
  }
  else if (Status == 3) {
    display.clearDisplay();
    display.println("WiFi Connection Fail");
    delay(1000); 
  }

  display.display();
}

/* getTime Function
    This function will use NTP Client to get the time from the internet */
String getTime(void) {
  if(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  // The formattedDate comes with the following format:
  // 2018-05-28T16:00:13Z
  // We need to extract date and time
  formattedDate = timeClient.getFormattedTime();
  formattedDate = formattedDate.substring(0,5);
  return formattedDate;
}

/* InitStatus Function
    This function will only run of init status is OK. Update will be for ver3 */
void printInitStatus(void) {
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,45);             // Start at top-left corner
  display.println("All sensors init ok");
  display.display();
  delay(1000);
}
/* Headline Function 
    @brief This function will generate the top yellow line of the OLED. Current version is v1.1
    Integrated CCS811 into the OLED screen, handled few error cases */
void printHeadline(String Time) {
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,0);             // Start at top-left corner
  display.print("AirLicker v1.2  ");
  display.println(Time);  
}

/* Temp Function */
void printTemp(float temp) {
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,20);             // Start at top-left corner
  display.print("Temp: "); display.print(temp); display.println(" C");
}

/* Humid Function */
void printHumid(float humidity) {

  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,30);             // Start at top-left corner
  display.print("Humidity: "); display.print(humidity); display.println("% rH");
}

/* CCS811 Function */
void printCCS811(int eCO2, int TVOC, bool Error) {
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,40);             // Start at top-left corner
  if (!Error)
  {
    display.print("eCO2: "); display.print(eCO2); display.println("ppm");
   // display.setCursor(0,50);
   // display.print("TVOC: "); display.print(TVOC); display.println("ppb");      
  }
  else
  {
    display.println("CCS811 disconnected, please check");
  }
}

/* printSenseAir Function */
void printSenseAir(int CO2)
{
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,50);
  display.print("CO2: "); display.print(CO2); display.println("ppm");  
}
