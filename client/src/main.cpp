/** @file GameBTComms.cpp
 *
 *  Class that provides games with Bluetooth comms.
 *
 *  Note that it is the game's responsibility to provide the UI to
 *  accompany this (with the exception of the BT device selection UI)
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it.
#endif

BluetoothSerial SerialBT;

void setup()
{
    Serial.begin(115200);
    SerialBT.begin("GameCommsHub");
}

void loop() {
    if (Serial.available())
    {
       SerialBT.write(Serial.read());
    }
    if (SerialBT.available())
    {
        Serial.write(SerialBT.read());
    }
    delay(20);
}
