#include "iot_iconset_16x16.h" // Icons
#include <Adafruit_GFX.h>      // OLED graphics lib
#include <Adafruit_SSD1306.h>  // OLED lib
#include <Arduino.h>           // Main Arduino library
#include <ESP8266WebServer.h>  // For WebServer
#include <ESP8266WiFi.h>       // ESP8266 Wi-Fi
#include <EepromRWU.h>         // My custom lib for EEPROM operations
#include <EncButton.h>         // Lib for buttons handling
#include <PubSubClient.h>      // MQTT lib
#include <SHT2x.h>             // Lib for Temp/Hum sensor
#include <TimerMs.h>           // Lib for timers
#include <Wire.h>              // i2c lib

#define SCREEN_WIDTH 128                                                  // OLED display width, in pixels
#define SCREEN_HEIGHT 64                                                  // OLED display height, in pixels
#define OLED_RESET -1                                                     // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Инициализация OLED-экрана SSD1306 (128x64)

#define INIT_ADDR 500            // номер резервной ячейки для ключа первого запуска
#define INIT_KEY 50              // ключ первого запуска. 0-254, на выбор
#define WIFI_DATA_START_ADDR 100 // адрес для начала записи/чтения данных о сохранённом ВайФай

SHT2x sht;                               // Инициализация датчика температуры и влажности GY-21 HTU21 SI7021
IPAddress WiFiIP;                        // IP-адрес Wi-Fi
EncButton<EB_TICK, 12> resetBtn;         // Кнопка сброса
String wifi_ssid = "Thermostat_ISA";     // имя точки доступа
String wifi_password = "12345678";       // пароль точки доступа
ESP8266WebServer server(80);             // веб-сервер на 80 порту
EepromRWU rwu(512, INIT_ADDR, INIT_KEY); // EEPROM size;
TimerMs wifiTmr(500, 1, 0);              // Таймер подключения к ВайФай
TimerMs dataTmr(3000, 1, 0);             // Таймер опроса данных
TimerMs resetTmr(5000, 1, 0);            // Таймер для кнопки RESET

float temperature, humidity; // Temp, Humidity
byte relayPin = 14;          // пин Реле
byte relayState = HIGH;      // Статус реле (по-умолчанию Реле выключено)
byte ledPin = 2;             // led статуса
byte ledState = LOW;         // Статус диода (по-умолчанию диод выключен)

String html_header = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0' charset='utf-8' /> <title>ESP8266 Settings</title><style>body{background-color:#d6e5ff;font-family:Arial,Helvetica,Sans-Serif;color:#000088;height:100%;}.main-form{background-color:#ffffff;border-radius:25px;padding:30px;margin:0auto;max-width:600px;height:auto;}input{margin-bottom:10px;}label{display:inline-block;width:120px;text-align:right;}h2,p{text-align:center;}</style></head><body>";

void send_Data(String body) {
  String str = "";
  str += html_header;
  str += body;
  str += "</body></html>";
  server.send(200, "text/html", str);
}
// Перезагрузка устройства:
void device_reboot(bool reset = false) {
  if (reset)                   // если reset = true, сбросить настройки и имитировать "первый запуск"
    rwu.write(INIT_ADDR, 255); //
  delay(3000);                 // ждём 3 сек.
  ESP.deepSleep(3e6);          // глубокий сон на 3 сек. имитация перезагрузки
}
// Сброс устройства:
void device_full_reset() {
  if (resetBtn.held()) { // Если удерживаем кнопку, то сброс устройства
    Serial.println("reset device...");

    display.clearDisplay();
    display.setCursor(0, 18);
    display.println("Device reset");
    display.println(" ");
    display.println("...REBOOT!");
    display.display();

    device_reboot(true);
  }
}

void handle_PageNotFound() { server.send(404, "text/plain", "Not found"); }

void handle_SettingsHtmlPage() {
  String str = "<div class='main-form'><h2>Thermostat settings</h2><form method='POST' action='ok'><p>Wi-Fi settings:</p><label for='ssid'>WIFI SSID:</label><input type='text' name='ssid'/><br/><label for='pswd'>WIFI Password:</label><input type='password' name='pswd'/><p>MQTT settings:</p><label for='mqttIP'>MQTT IP:</label><input type='text' name='mqttIP'/><br/><label for='mqttUser'>MQTT user:</label><input type='text' name='mqttUser'/><br/><label for='mqttPass'>MQTT pass:</label><input type='password' name='mqttPass'/><p>Temperature settings:</p><label for='minTemp'>Min temp:</label><input name='minTemp' type='range'step='0.1'value='21.5'min='15'max='40'oninput='this.nextElementSibling.value=this.value'/><output>21.5</output><label for='maxTemp'>Max temp:</label><input name='maxTemp' type='range'step='0.1'value='24.5'min='15'max='40'oninput='this.nextElementSibling.value=this.value'/><output>24.5</output><br/><br/><input type='submit'value='Save settings'/><a href='/'><input "
               "type='button' value='Home' style='margin-left: 20px;' /></a></form></div>";
  send_Data(str);
}

void handle_SaveSettingsHtmlPage() {
  display.clearDisplay(); // Clear display buffer
  display.setCursor(0, 18);
  display.println("Settings saved");
  display.println("successfully!");
  display.println(" ");
  display.println("...REBOOT!");
  display.display();

  String wifi_name = server.arg(0); // имя сети из get запроса
  String wifi_pass = server.arg(1); // пароль сети из get запроса
  String mqtt_ip = server.arg(2);   // MQTT IP из get запроса
  String mqtt_user = server.arg(3); // MQTT Login из get запроса
  String mqtt_pass = server.arg(4); // MQTT Pass из get запроса
  String min_temp = server.arg(5);  // minTemperature из get запроса
  String max_temp = server.arg(6);  // maxTemperature сети из get запроса

  String str = "";

  if (server.args() > 0) { // if first call

    for (int i = 0; i <= server.args(); i++) {
      Serial.print(i);
      Serial.print(": ");
      Serial.println(server.arg(i));
    }

    if (wifi_name != "") {
      // int put_wifi_ssid = rwu.write(WIFI_DATA_START_ADDR, wifi_name);

      // if (wifi_pass != "") {
      //   rwu.write(put_wifi_ssid, wifi_pass);
      // }f

      rwu.write(INIT_ADDR, INIT_KEY);

      str = "<div class='main-form'><h2>Настройки сохранены!</h2><p>Устройство будет перезагружено автоматически.</p></div>";
    } else {
      str = "<div class='main-form'><h2>Ошибка!</h2><p>Не указано имя Wi-Fi сети</p></br><a href='/'>Вернуться</a> к странице настроек</div>";
    }
  };

  send_Data(str);
  device_reboot();
};

void handle_WebServerOnConnect() {
  String str = "<div class='main-form'>\
                  <h2>Привет!</h2>\
                  <p>Всё работает отлично</p></br>\
                  <a href='/config'>Перейти</a> на страницу настроек\
                </div>";
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
  server.on("/ok", handle_SaveSettingsHtmlPage);
  server.onNotFound(handle_PageNotFound);
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
  WiFiIP = WiFi.localIP();

  digitalWrite(ledPin, HIGH);

  server.on("/", handle_WebServerOnConnect);
  server.on("/config", handle_SettingsHtmlPage);
  server.on("/ok", handle_SaveSettingsHtmlPage);
  server.onNotFound(handle_PageNotFound);
  server.begin();
}

void prinMainInfo(float temp, float hum) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 5);
  display.print(WiFiIP);
  display.drawBitmap(111, 1, wifi1_icon16x16, 16, 16, 1);
  display.drawBitmap(87, 1, sun_icon16x16, 16, 16, !relayState);
  display.drawLine(0, 16, 124, 16, SSD1306_WHITE);
  display.drawBitmap(31, 20, temperature_icon16x16, 16, 16, 1);
  display.drawBitmap(93, 20, humidity2_icon16x16, 16, 16, 1);
  display.setTextSize(2);
  display.setCursor(12, 50); // cursor for TEMP
  display.print(temp, 1);
  display.setCursor(77, 50); // cursor for HUM
  display.print(hum, 1);
  display.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(ledPin, ledState);
  digitalWrite(relayPin, relayState);

  // int sda = 2; // qui SDC del sensore
  // int sdc = 14; // qui SDA del sensore
  // Wire.begin(sda, sdc);

  sht.begin();

  resetBtn.setHoldTimeout(5000); // для сброса устройства, держать кнопку 5 сек.

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    for (;;)
      ; // Don't proceed, loop forever
  }
  display.setTextSize(1); // указываем размер шрифта
  display.setTextColor(WHITE);
  display.clearDisplay();
  display.display();

  if (rwu.isFirstRun()) { // Если первый запуск
    runAsAp();            // запускаем как Точку Доступа
  } else {
    runWebServer();
    prinMainInfo(0, 0);
  }
}

void loop() {
  resetBtn.tick();
  server.handleClient();
  device_full_reset(); // Если кнопка сброса зажата 5 сек, сбрасываем устройство.
  sht.read();

  temperature = sht.getTemperature();
  humidity = sht.getHumidity();

  if (temperature < 24.00) {
    relayState = LOW;
    digitalWrite(relayPin, relayState);
  }
  if (temperature > 25.00) {
    relayState = HIGH;
    digitalWrite(relayPin, relayState);
  }
  // if (resetBtn.press())
  //   Serial.println("press");
  // if (resetBtn.click())
  //   Serial.println("click");
  // if (resetBtn.release())
  //   Serial.println("release");

  if (dataTmr.tick()) {
    prinMainInfo(sht.getTemperature(), sht.getHumidity());

    // uint8_t stat = sht.getStatus();
    // Serial.println("**********");
    // Serial.print("SHT Status: ");
    // Serial.print(stat);
    // Serial.println();
    // Serial.print(stat, HEX);
    // Serial.println("------------");
  }
}