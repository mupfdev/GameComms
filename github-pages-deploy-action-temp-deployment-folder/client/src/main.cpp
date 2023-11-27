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
    Serial.printf("GameCommsHub\n\n");
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

            switch (buffer[0])
            { 
                // Message to ..
                case 0x01: // Host
                case 0x02: // Client 1
                case 0x03: // Client 2
                case 0x04: // Client 3
                case 0x05: // all
                {
                    for (int i = 0; i < index; i += 1)
                    {
                        Serial.printf("%02X ", buffer[i]);
                    }
                    Serial.printf("\n");
                    buffer[0] = 0x00;
                    break;
                }
                default:
                    Serial.printf("%s\n", buffer);
                    break;
            }
            index = 0;
        }
    }
}
