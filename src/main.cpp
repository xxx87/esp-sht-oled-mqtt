// #include "button.h"
#include "iot_iconset_16x16.h"
#include <Adafruit_GFX.h>     // OLED graphics lib
#include <Adafruit_SSD1306.h> // OLED lib
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <EepromRWU.h>
#include <EncButton.h>
// #include <PubSubClient.h> // MQTT lib
#include <TimerMs.h>
#include <Wire.h> // i2c lib

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define INIT_ADDR 500            // номер резервной ячейки для ключа первого запуска
#define INIT_KEY 50              // ключ первого запуска. 0-254, на выбор
#define WIFI_DATA_START_ADDR 100 // адрес для начала записи/чтения данных о сохранённом ВайФай

unsigned long period_time = (long)5000;
unsigned long my_timer;
// const char *ssid = "ax55_is_home";
// const char *password = "@lk@tr@s_2020";
EncButton<EB_TICK, 12> resetBtn;
String wifi_ssid = "Thermostat_ISA";     // имя точки доступа
String wifi_password = "12345678";       // пароль точки доступа
ESP8266WebServer server(80);             // веб-сервер на 80 порту
EepromRWU rwu(512, INIT_ADDR, INIT_KEY); // EEPROM size;
TimerMs wifiTmr(500, 1, 0);              // Таймер подключения к ВайФай
TimerMs dataTmr(1000, 1, 0);             // Таймер опроса данных
TimerMs resetTmr(5000, 1, 0);            // Таймер для кнопки RESET
int ledPin = 2;                          // led статуса
int ledState = LOW;                      // Статус диода (по-умолчанию диод выключен)

String html_header = "<html>\
  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\
  <head>\
    <title>ESP8266 Settings</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>";

void send_Data(String body) {
  String str = "";
  str += html_header;
  str += body;
  str += "</body></html>";
  server.send(200, "text/html", str);
}
// Перезагрузка устройства.
void handle_Reload(bool reset = false) {
  if (reset)                   // если reset = true, сбросить настройки и имитировать "первый запуск"
    rwu.write(INIT_ADDR, 255); //
  delay(3000);                 // ждём 3 сек.

  ESP.deepSleep(3e6); // глубокий сон на 3 сек. имитация перезагрузки
}

void handle_Reset() {
  if (resetBtn.held()) {
    Serial.println("reset device..."); // однократно вернёт true при удержании
    display.clearDisplay();            // Clear display buffer
    display.setCursor(0, 18);
    display.println("Device reset");
    display.println(" ");
    display.println("...REBOOT!");
    display.display();

    handle_Reload(true);
  }
}

void handle_PageNotFound() { server.send(404, "text/plain", "Not found"); }

void handle_SettingsHtmlPage() {
  String str = "<form method=\"POST\" action=\"ok\">\
      <input name=\"ssid\"> WIFI Net</br>\
      <input name=\"pswd\"> Password</br></br>\
      <input type=SUBMIT value=\"Save settings\">\
    </form>";
  send_Data(str);
}

void handle_AccessPoint() {
  display.clearDisplay(); // Clear display buffer
  display.setCursor(0, 18);
  display.println("Settings saved");
  display.println("successfully!");
  display.println(" ");
  display.println("...REBOOT!");
  display.display();

  String wifi_name = server.arg(0); // имя сети из get запроса
  String wifi_pass = server.arg(1); // пароль сети из get запроса

  String str = "";

  if (server.args() > 0) { // if first call
    if (wifi_name != "") {
      int put_wifi_ssid = rwu.write(WIFI_DATA_START_ADDR, wifi_name);
      if (wifi_pass != "") {
        rwu.write(put_wifi_ssid, wifi_pass);
      }

      rwu.write(INIT_ADDR, INIT_KEY);

      str = "Configuration saved in FLASH</br>\
             Changes applied after reboot</p></br></br>\
             <a href=\"/\">Return</a> to settings page</br>";
    } else {
      str = "No WIFI Net</br>\
             <a href=\"/\">Return</a> to settings page</br>";
    }
  };

  send_Data(str);
  handle_Reload();
};

void handle_WebServerOnConnect() {
  String str = "WELCOME IN MAGIC WORLD</br>\
             <p>Bla bla bla</p></br></br>\
             <a href=\"google.com\">Return</a> to google page</br>";
  send_Data(str);
}

// Если это первый запуск, запускаем модуль как точку доступа,
// чтоб юзер мог ввести имя и пароль от своей wifi сети:
void runAsAp() {
  display.setCursor(10, 0);
  display.println("Configuring AP");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(wifi_ssid, wifi_password);
  IPAddress myIP = WiFi.softAPIP();

  display.setCursor(0, 18);
  display.println("AP IP address: ");
  display.println(myIP);
  display.println(" ");
  display.print("HTTP server started");
  display.display();

  server.on("/", handle_SettingsHtmlPage);
  server.on("/ok", handle_AccessPoint);
  server.begin();
}

void runWebServer() {
  WiFi.mode(WIFI_STA);

  String ssid;
  String pass;
  int ssidAddrOffset = rwu.read(WIFI_DATA_START_ADDR, &ssid);
  rwu.read(ssidAddrOffset, &pass);

  WiFi.begin(ssid, pass);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    if (wifiTmr.tick()) {
      Serial.print(".");
      ledState = !ledState;
      digitalWrite(ledPin, ledState);
      display.drawBitmap(111, 1, wifi1_icon16x16, 16, 16, ledState);
      display.display();
    }
  }

  ledState = HIGH;
  //
  display.setCursor(0, 5);
  display.print(WiFi.localIP());
  display.drawBitmap(111, 1, wifi1_icon16x16, 16, 16, 1);
  display.drawLine(0, 16, 124, 16, SSD1306_WHITE);
  display.display();

  digitalWrite(ledPin, ledState);

  server.on("/", handle_WebServerOnConnect);
  server.onNotFound(handle_PageNotFound);
  server.begin();

  Serial.println(ssid);
  Serial.println(pass);
}

void setup() {
  my_timer = millis(); // "сбросить" таймер
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  resetBtn.setHoldTimeout(5000); // для сброса устройства, держать кнопку 5 сек.

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    //    Serial.println("SSD1306 allocation failed");
    for (;;)
      ; // Don't proceed, loop forever
  }
  display.setTextSize(1); // указываем размер шрифта
  display.setTextColor(WHITE);
  display.clearDisplay();

  display.drawBitmap(31, 18, temperature_icon16x16, 16, 16, 1);
  display.drawBitmap(93, 18, humidity2_icon16x16, 16, 16, 1);
  // for (byte i = 0; i < 10; i++) {
  //   display.setCursor(0, 0); // установка курсора в позицию X = 0; Y = 0
  //   display.print(i);        // записываем в буфер памяти дисплея нашу цифру
  //   display.display();       // и её выводим на экран
  //   delay(1000);             // ждём 0.5 секунды
  // }

  display.display();
  // int sda = 2;  // qui SDC del sensore
  // int sdc = 14; // qui SDA del sensore
  // Wire.begin(sda, sdc);

  if (rwu.isFirstRun()) { // первый запуск
    runAsAp();
  } else {
    runWebServer();
  }
}

void loop() {
  server.handleClient();
  handle_Reset();
  // Если кнопка сброса зажата 5 сек, сбрасываем устройство.

  resetBtn.tick();

  if (resetBtn.press())
    Serial.println("press");
  if (resetBtn.click())
    Serial.println("click");
  if (resetBtn.release())
    Serial.println("release");

  // if (dataTmr.tick()) {
  //   Serial.print(".");
  //   ledState = !ledState;
  //   Serial.print(ledState);
  // digitalWrite(ledPin, 1);
  // }

  // if (millis() - my_timer >= period_time) {
  //   my_timer = millis(); // "сбросить" таймер
  // display.clearDisplay(); // Clear display buffer

  // // display.setTextSize(1);

  // // display.setTextColor(WHITE);
  // display.setCursor(0, 0);
  // display.println("Temp: Hum:");
  // display.drawBitmap(111, 1, wifi1_icon16x16, 16, 16, 1);
  // display.drawLine(0, 17, 124, 17, SSD1306_WHITE);

  // //  display.setTextColor(WHITE);
  // display.setCursor(0, 20);
  // display.print(23.3, 1);
  // display.print(" ");
  // display.print(" ");
  // display.print(33, 1);
  // display.drawLine(0, 36, 124, 36, SSD1306_WHITE);
  // display.setCursor(0, 39);
  // display.print("P: ");
  // display.print(66, 0);
  // display.display();
  // }
}