/** @file main.cpp
 *
 *  GameComms BT interface client
 *
 *  Note that it is the game's responsibility to provide the UI to
 *  accompany this (with the exception of the BT device selection UI)
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "BluetoothSerial.h"

#define MAX_MESSAGE_LENGTH 512

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it.
#endif

BluetoothSerial SerialBT;

void setup()
{
    Serial.begin(115200);
    SerialBT.begin("GameCommsHub");
}

void loop()
{
    while (SerialBT.available() > 0)
    {
        static char         buffer[MAX_MESSAGE_LENGTH] = { 0 };
        static unsigned int index = 0;
        char                read_byte = SerialBT.read();

        if ((read_byte != '\n') && (index < MAX_MESSAGE_LENGTH - 1))
        {
            buffer[index] = read_byte;
            index += 1;
        }
        else
        {
            buffer[index] = '\0';
            index         = 0;

            if (buffer[2] == '\n')
            {
                Serial.printf("0x%2X 0x%2X\n", buffer[0], buffer[1]);
            }
            else
            {
                Serial.printf("%s\n", buffer);
            }
        }
    }
}
