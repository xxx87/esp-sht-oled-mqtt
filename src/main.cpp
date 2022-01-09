// #include "button.h"
#include <Adafruit_GFX.h>     // OLED graphics lib
#include <Adafruit_SSD1306.h> // OLED lib
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <EepromRWU.h>
#include <EncButton.h>
#include <PubSubClient.h> // MQTT lib
#include <TimerMs.h>
#include <Wire.h> // i2c lib

#define INIT_ADDR 500            // номер резервной ячейки для ключа первого запуска
#define INIT_KEY 50              // ключ первого запуска. 0-254, на выбор
#define WIFI_DATA_START_ADDR 100 // адрес для начала записи/чтения данных о сохранённом ВайФай

// const char *ssid = "ax55_is_home";
// const char *password = "@lk@tr@s_2020";
EncButton<EB_TICK, 12> resetBtn;
String wifi_ssid = "Thermostat_ISA";     // имя точки доступа
String wifi_password = "12345678";       // пароль точки доступа
ESP8266WebServer server(80);             // веб-сервер на 80 порту
EepromRWU rwu(512, INIT_ADDR, INIT_KEY); // EEPROM size;
TimerMs dataTmr(1000, 1, 0);             // Таймер опроса данных
TimerMs resetTmr(5000, 1, 0);            // Таймер для кнопки RESET
int ledPin = 4;                          // led статуса
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
void reload(bool reset = false) {
  if (reset)                    // если reset = true, сбросить настройки и имитировать "первый запуск"
    rwu.update(INIT_ADDR, 255); //
  delay(3000);                  // ждём 3 сек.
  ESP.deepSleep(3e6);           // глубокий сон на 3 сек. имитация перезагрузки
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
  String wifi_name = server.arg(0); // имя сети из get запроса
  String wifi_pass = server.arg(1); // пароль сети из get запроса

  String str = "";

  if (server.args() > 0) { // if first call
    if (wifi_name != "") {
      int put_wifi_ssid = rwu.write(WIFI_DATA_START_ADDR, wifi_name);
      if (wifi_pass != "") {
        rwu.write(put_wifi_ssid, wifi_pass);
      }

      rwu.update(INIT_ADDR, INIT_KEY);

      str = "Configuration saved in FLASH</br>\
             Changes applied after reboot</p></br></br>\
             <a href=\"/\">Return</a> to settings page</br>";
    } else {
      str = "No WIFI Net</br>\
             <a href=\"/\">Return</a> to settings page</br>";
    }
  };

  send_Data(str);
  reload();
};
// Если это первый запуск, запускаем модуль как точку доступа,
// чтоб юзер мог ввести имя и пароль от своей wifi сети:
void runAsAp() {
  Serial.println("Configuring access point...");
  WiFi.softAP(wifi_ssid, wifi_password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  server.on("/", handle_SettingsHtmlPage);
  server.on("/ok", handle_AccessPoint);
  server.begin();
  Serial.println("HTTP server started");
}

void runWebServer() {
  Serial.println("WEB SERVER STARTED");
  String newStr1;
  String newStr2;
  int newStr1AddrOffset = rwu.read(WIFI_DATA_START_ADDR, &newStr1);
  rwu.read(newStr1AddrOffset, &newStr2);
  // int newStr2AddrOffset = rwu.read(newStr1AddrOffset, &newStr2);

  Serial.println(newStr1);
  Serial.println(newStr2);
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  resetBtn.setHoldTimeout(5000); // для сброса устройства, держать кнопку 5 сек.
  // int sda = 2;  // qui SDC del sensore
  // int sdc = 14; // qui SDA del sensore
  // Wire.begin(sda, sdc);
  // Serial.print("Setup init key: ");
  // Serial.println(EEPROM.read(INIT_ADDR));

  // Serial.print("Setup init key size: ");
  // Serial.println(sizeof(EEPROM.read(INIT_ADDR)));

  // runAsAp();

  if (rwu.isFirstRun()) { // первый запуск
    runAsAp();
  } else {
    runWebServer();
  }

  // EEPROM.put(address, cod); // сохраняем код телефона в памяти Ардуино
  // Serial.print("Size code: ");
  // Serial.print(sizeof(cod));
  // Serial.print(" ");
  // Serial.print("Readed code: ");
  // Serial.print(EEPROM.get(address, COD));
}

void loop() {
  resetBtn.tick();

  if (resetBtn.press())
    Serial.println("press");
  if (resetBtn.click())
    Serial.println("click");
  if (resetBtn.release())
    Serial.println("release");

  if (resetBtn.held()) {
    Serial.println("reset device..."); // однократно вернёт true при удержании
    reload(true);
  }

  server.handleClient();
  // if (dataTmr.tick()) {
  //   Serial.print(".");
  //   ledState = !ledState;
  //   Serial.print(ledState);
  digitalWrite(ledPin, 1);
  // }
}