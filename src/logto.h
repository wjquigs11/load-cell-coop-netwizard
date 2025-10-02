
#ifndef LOGTO_H
#define LOGTO_H

#include <Arduino.h>
//#include <WebSerialPro.h>

class log {
public:
    static bool logToSerial;
    log() {}
    static void toAll(String s);
    static const int ASIZE = 20;
    static String commandList[ASIZE];
};
#endif