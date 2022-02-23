#include <Arduino.h>          // Main Arduino library
#include <ESP8266WebServer.h> // For WebServer
#include <ESP8266WiFi.h>      // ESP8266 Wi-Fi
#include <EepromRWU.h>        // My custom lib for EEPROM operations
#include <EncButton.h>        // Lib for buttons handling
#include <GyverOLED.h>        // OLED graphics lib by Gyver
#include <PubSubClient.h>     // MQTT lib
#include <SHT2x.h>            // Lib for Temp/Hum sensor
#include <TimerMs.h>          // Lib for timers
#include <Wire.h>             // i2c lib

GyverOLED<SSD1306_128x64, OLED_BUFFER> oled; // Инициализация OLED-экрана SSD1306 (128x64)

#define INIT_ADDR 500            // номер резервной ячейки для ключа первого запуска
#define INIT_KEY 50              // ключ первого запуска. 0-254, на выбор
#define WIFI_DATA_START_ADDR 100 // адрес для начала записи/чтения данных о сохранённом ВайФай
/* BUTTONS: */
EncButton<EB_TICK, 0> resetBtn; // Кнопка сброса
EncButton<EB_TICK, 4> down;     //
EncButton<EB_TICK, 5> up;       //
EncButton<EB_TICK, 13> ok;      //
/* TIMERS: */
TimerMs wifiTmr(500, 1, 0);   // Таймер подключения к ВайФай
TimerMs dataTmr(3000, 1, 0);  // Таймер опроса данных
TimerMs resetTmr(5000, 1, 0); // Таймер для кнопки RESET

SHT2x sht;                               // Инициализация датчика температуры и влажности GY-21 HTU21 SI7021
IPAddress WiFiIP;                        // IP-адрес Wi-Fi
String wifi_ssid = "Thermostat_ISA";     // имя точки доступа
String wifi_password = "12345678";       // пароль точки доступа
ESP8266WebServer server(80);             // веб-сервер на 80 порту
EepromRWU rwu(512, INIT_ADDR, INIT_KEY); // EEPROM size;

bool setTemp = false;
float tempMin = 21.3;
float tempMax = 24.0;

float temperature, humidity; // Temp, Humidity
// byte relayPin = 15;          // пин Реле
byte relayPin = 15;    // Статус реле (по-умолчанию Реле выключено)
byte relayState = LOW; // Статус реле (по-умолчанию Реле выключено)
// byte ledPin = 2;             // led статуса
byte ledState = LOW; // Статус диода (по-умолчанию диод выключен)

static uint8_t sel_mode_pointer = 2;
static uint8_t main_menu_pointer = 2;
static uint8_t set_min_max_pointer = 3;
static uint8_t sel_menu = 0;

const uint8_t bitmap_16x16[] PROGMEM = {
    0x10, 0x38, 0x18, 0xCC, 0x6C, 0x6E, 0x6E, 0x6E, 0x6E, 0x6E, 0x6E, 0x6C, 0xCC, 0x18, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x0B, 0x0B, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
};

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

    // oled.clear();
    oled.roundRect(10, 16, 116, 50, OLED_CLEAR);  // аналогично скруглённый прямоугольник
    oled.roundRect(11, 17, 117, 51, OLED_STROKE); // аналогично скруглённый прямоугольник
    oled.setCursor(22, 3);
    oled.print("Сброс устройства");

    for (int i = 3; i >= 0; i--) {
      oled.setCursor(22, 4);
      oled.print("Перезагрузка: ");
      oled.print(i);
      oled.update();
      delay(500);
    }
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
  oled.roundRect(10, 16, 116, 50, OLED_CLEAR);  // аналогично скруглённый прямоугольник
  oled.roundRect(11, 17, 117, 51, OLED_STROKE); // аналогично скруглённый прямоугольник
  oled.setCursor(22, 3);
  oled.print("Сохранено.");

  for (int i = 3; i >= 0; i--) {
    oled.setCursor(22, 4);
    oled.print("Перезагрузка: ");
    oled.print(i);
    oled.update();
    delay(500);
  }

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
  WiFi.mode(WIFI_AP);
  WiFi.softAP(wifi_ssid, wifi_password);
  IPAddress myIP = WiFi.softAPIP();

  oled.clear();
  oled.home();
  oled.print("Точка доступа:");
  oled.setCursor(10, 2);
  oled.print("IP точки:");
  oled.setCursor(10, 3);
  oled.print(myIP);
  oled.update();

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
      // digitalWrite(ledPin, ledState);
      oled.drawBitmap(109, 1, bitmap_16x16, 16, 16, ledState);
      oled.update();
    }
  }
  WiFiIP = WiFi.localIP();

  // digitalWrite(ledPin, HIGH);

  server.on("/", handle_WebServerOnConnect);
  server.on("/config", handle_SettingsHtmlPage);
  server.on("/ok", handle_SaveSettingsHtmlPage);
  server.onNotFound(handle_PageNotFound);
  server.begin();
}

void mainScreen(float temp, float hum) {
  oled.clear();
  oled.home();
  oled.print(WiFiIP);

  oled.setScale(2);
  oled.setCursor(1, 3); // курсор в (пиксель X, строка Y)
  oled.print("Т: ");
  oled.print(temp);
  oled.setCursor(1, 6); // курсор в (пиксель X, строка Y)
  oled.print("H: ");
  oled.print(hum);
  oled.setScale(1);
  // oled.circle(60, 40, 10, OLED_STROKE);
  oled.circle(116, 40, 10, relayState ? OLED_FILL : OLED_STROKE);
  // bool flag = true;
  // for (size_t i = 0; i < 5; i++) {
  //   flag ? oled.drawBitmap(111, 1, bitmap_16x16, 16, 16) : oled.clear(111, 1, 127, 17);
  //   ;
  //   oled.update();
  //   flag = !flag;
  //   delay(1000);
  // }

  oled.drawBitmap(109, 1, bitmap_16x16, 16, 16);
  oled.update();
  if (ok.held()) {
    sel_menu = 0;
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);

  int sda = 12; // qui SDC del sensore
  int sdc = 14; // qui SDA del sensore
  Wire.begin(sda, sdc);
  Wire.setClock(400000L);

  // pinMode(ledPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  // digitalWrite(ledPin, ledState);
  digitalWrite(relayPin, LOW);

  oled.init();                   // инициализация
  oled.setScale(1);              // Задать размер шрифта
  down.setHoldTimeout(1000);     // установка таймаута удержания кнопки
  ok.setHoldTimeout(1000);       // установка таймаута удержания кнопки
  up.setHoldTimeout(1000);       // установка таймаута удержания кнопки
  resetBtn.setHoldTimeout(5000); // для сброса устройства, держать кнопку 5 сек.

  sht.begin();

  if (rwu.isFirstRun()) { // Если первый запуск
    runAsAp();            // запускаем как Точку Доступа
  } else {
    runWebServer();
    mainScreen(0, 0);
  }
}

// Печатает курсор для меню.
void printPointer(uint8_t ptr) {
  oled.setCursor(0, ptr);
  oled.print(">");
}

// Главное меню.
void mainMenu() {
  if (down.click()) {
    main_menu_pointer += 2;
    if (main_menu_pointer > 6)
      main_menu_pointer = 2;
  }
  if (ok.click()) {
    sel_menu = main_menu_pointer;
  }
  if (up.click()) {
    if (main_menu_pointer == 2)
      main_menu_pointer = 6;
    else
      main_menu_pointer -= 2;
  }

  oled.clear();
  oled.home();
  oled.print("Главное меню:");
  oled.setCursor(10, 2);
  oled.print("Выбрать режим раб.");
  oled.setCursor(10, 4);
  oled.print("Изменить мин/макс Т");
  oled.setCursor(10, 6);
  oled.print("Сбросить настройки");

  printPointer(main_menu_pointer);
  oled.update();
}

// Меню выбора режима работы. 1. Автономный; 2. Wi-Fi; 3. Wi-Fi + MQTT;
void selectModeMenu() {

  if (down.click()) {
    sel_mode_pointer += 2;
    if (sel_mode_pointer > 6)
      sel_mode_pointer = 2;
  }
  if (ok.click()) {
    sel_menu = 0;
  }

  if (up.click()) {
    if (sel_mode_pointer == 2)
      sel_mode_pointer = 6;
    else
      sel_mode_pointer -= 2;
  }

  oled.clear();
  oled.home();
  oled.print("Выбор режима работы:");
  oled.setCursor(10, 2);
  oled.print("1. Автономный");
  oled.setCursor(10, 4);
  oled.print("2. Wi-Fi");
  oled.setCursor(10, 6);
  oled.print("3. Wi-Fi + MQTT");

  printPointer(sel_mode_pointer);
  oled.update();

  if (ok.held()) {
    oled.roundRect(10, 16, 116, 50, OLED_CLEAR);  // аналогично скруглённый прямоугольник
    oled.roundRect(11, 17, 117, 51, OLED_STROKE); // аналогично скруглённый прямоугольник
    oled.setCursor(22, 3);
    oled.print("Сохранено.");

    for (int i = 3; i >= 0; i--) {
      oled.setCursor(22, 4);
      oled.print("Перезагрузка: ");
      oled.print(i);
      oled.update();
      delay(1000);
    }
    sel_menu = 20;
    delay(1000);
  }
}

// Меню изменения Макс и Мин температуры
void changeMinMaxTempMenu() {
  if (!setTemp)
    oled.clear();

  if (down.hold()) {
    if (set_min_max_pointer == 3)
      tempMin -= .1;
    if (set_min_max_pointer == 5)
      tempMax -= .1;
  }

  if (down.click()) {
    if (!setTemp) {
      set_min_max_pointer += 2;
      if (set_min_max_pointer > 6)
        set_min_max_pointer = 3;
    } else {
      if (set_min_max_pointer == 3)
        tempMin -= .1;
      if (set_min_max_pointer == 5)
        tempMax -= .1;
    }
  }

  if (ok.click()) {
    if (!setTemp)
      sel_menu = 0;
    else
      setTemp = false;
  }

  if (ok.held()) {
    setTemp = true;
    if (set_min_max_pointer == 3)
      oled.roundRect(78, 20, 115, 34, OLED_STROKE); // аналогично скруглённый прямоугольник
    if (set_min_max_pointer == 5)
      oled.roundRect(78, 37, 115, 50, OLED_STROKE); // аналогично скруглённый прямоугольник
  }

  if (up.hold()) {
    if (set_min_max_pointer == 3)
      tempMin += .1;
    if (set_min_max_pointer == 5)
      tempMax += .1;
  }

  if (up.click()) {
    if (!setTemp) {
      if (set_min_max_pointer == 3)
        set_min_max_pointer = 5;
      else
        set_min_max_pointer -= 2;
    } else {
      if (set_min_max_pointer == 3)
        tempMin += .1;
      if (set_min_max_pointer == 5)
        tempMax += .1;
    }
  }

  oled.home();
  oled.print("Установка температуры:");

  oled.setCursor(10, 3);
  oled.print("Задать MIN: ");
  oled.print(tempMin);
  oled.setCursor(10, 5);
  oled.print("Задать MAX: ");
  oled.print(tempMax);

  printPointer(set_min_max_pointer);
  oled.update();
}

void loop() {
  down.tick();
  ok.tick();
  up.tick();
  resetBtn.tick();

  server.handleClient();
  device_full_reset(); // Если кнопка сброса зажата 5 сек, сбрасываем устройство.
  sht.read();

  temperature = sht.getTemperature();
  humidity = sht.getHumidity();

  // if (temperature < 24.00) {
  //   relayState = LOW;
  //   digitalWrite(relayPin, relayState);
  // }
  // if (temperature > 25.00) {
  //   relayState = HIGH;
  //   digitalWrite(relayPin, relayState);
  // }

  switch (sel_menu) {
  case 0:
    mainMenu();
    break;
  case 2:
    selectModeMenu();
    break;
  case 4:
    changeMinMaxTempMenu();
    break;
  case 20:
    mainScreen(sht.getTemperature(), sht.getHumidity());
    break;
  }

  // if (resetBtn.press())
  //   Serial.println("press");
  if (resetBtn.click()) {
    digitalWrite(relayPin, !digitalRead(relayPin));
  }
  // if (resetBtn.release())
  //   Serial.println("release");

  // if (dataTmr.tick()) {
  //   mainScreen();

  // uint8_t stat = sht.getStatus();
  // Serial.println("**********");
  // Serial.print("SHT Status: ");
  // Serial.print(stat);
  // Serial.println();
  // Serial.print(stat, HEX);
  // Serial.println("------------");
}
