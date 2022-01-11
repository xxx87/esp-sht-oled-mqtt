/*
    Библиотека для Чтения и Записи в EEPROM (энергонезависимую память)

    Isaev Ilya xxx_87@ukr.net
    MIT License

    Версии:
    v1.0 - релиз
*/

#ifndef EepromRWU_h
#define EepromRWU_h
#include <Arduino.h>
#include <EEPROM.h>

class EepromRWU {
public:
  EepromRWU(int eepromSize, int initAddr, int initKey);
  int read(int addrOffset, String *strToRead);
  int write(int addrOffset, const String &strToWrite);
  void write(int addrOffset, const int val);
  void update(int addr, int val);
  bool isFirstRun();

private:
  int _esize;
  int _initAddr;
  int _initKey;
};

#endif
