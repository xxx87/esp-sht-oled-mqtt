/*

*/

#include "EepromRWU.h"
#include "Arduino.h"
#include "EEPROM.h"

EepromRWU::EepromRWU(int eepromSize, int initAddr, int initKey) {
  EEPROM.begin(eepromSize);
  _esize = eepromSize;
  _initAddr = initAddr;
  _initKey = initKey;
}

int EepromRWU::read(int addrOffset, String *strToRead) {
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++) {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0';
  *strToRead = String(data);
  return addrOffset + 1 + newStrLen;
}

int EepromRWU::write(int addrOffset, const String &strToWrite) {
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
  return addrOffset + 1 + len;
}

void EepromRWU::write(int addr, const int val) {
  EEPROM.put(addr, val); //
  EEPROM.commit();
  EEPROM.end();
}

bool EepromRWU::isFirstRun() { return (EEPROM.read(_initAddr) != _initKey); }
