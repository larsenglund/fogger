/*
  ESP8266 fog machine controller
  See github for more info https://github.com/larsenglund/fogger
*/

#include <Arduino.h>
#include <math.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <Fonts/Picopixel.h>
#include <Fonts/FreeSansBold9pt7b.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool heater = false;
bool pump = false;
bool button = false;
bool connected = false;
float fog_temp = 260.0;
float sys_temp = 24.0;
float fogjuice = 135.0;
char buff[16];
int16_t _x1, _y1;
uint16_t w, h;

void setup() {
  Serial.begin(115200);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.setRotation(1);
  display.setFont(&Picopixel);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

}

void loop() {
  display.clearDisplay();

  display.setCursor(4,5);
  display.println(F("FOGGER"));
  //display.println();
  display.drawLine(0,7,32,7,SSD1306_WHITE);
  display.setCursor(0,14);

  display.println(F("FOG TEMP"));
  display.println();
  display.setFont(&FreeSansBold9pt7b);
  display.print(fog_temp, 0);
  //display.print((char)247);
  //display.println("C");
  display.setFont(&Picopixel);
  display.println();
  display.println(F("SYS TEMP"));
  display.println();
  display.setFont(&FreeSansBold9pt7b);
  //sprintf(buff, "%d", (int)round(sys_temp));
  //display.getTextBounds(buff, 0, 0, &_x1, &_y1, &w, &h); //calc width of new string
  display.print(sys_temp, 0);
  //display.setCursor(display.getCursorX() + (display.width() - w) / 2, display.getCursorY());
  //display.print(buff);
  display.setFont(&Picopixel);
  display.println();
  display.println();

  display.print(F("HEAT "));
  display.println((heater?"ON":"OFF"));

  display.print(F("BTN "));
  display.println((button?"ON":"OFF"));

  display.print(F("PUMP "));
  display.println((pump?"ON":"OFF"));

  display.print(F("FOGJUICE"));
  //display.println();
  display.setFont();
  display.println();
  display.print(fogjuice, 0);
  display.setFont(&Picopixel);
  display.println();

  //display.print((char)247);
  //isplay.println("C");

  /*display.drawRect(104, 0, 15, 15, SSD1306_WHITE);
  if (connected) {
    display.fillRect(106, 2, 11, 11, SSD1306_WHITE);
  }
  display.setCursor(101,16);
  display.println("WiFi");*/
  display.println();
  display.println(F("WiFi"));
  display.println((connected?"ACTIVE":"WAITING"));

  display.display();
  delay(250);
}