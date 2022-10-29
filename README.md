# PYTES E-BOX 48100R to MQTT 

This application runs on an ESP32 with an RS232 level shifter attached to get battery details from an E-Box and provide them as MQTTT topics.

Ther are also Homeassitant MQTT discovery topics implemented. So Devices show up in Homeassistant automatically.

## Disclaimer

This is an hobby project.

Everbody who want's to help is welcome.

You need to know ESP32 PlatformIO and what an level shifter is to be successfull!

You need to know MQTT and Homeassistant!

If you are unfamiliar with the stuff you will get frustrated.

## Why?

I want to see Cell voltages.

## Focus of this Project

Provide info for solar hobbyists.

## Hardware connection

Serial 2 is used on ESP 32

    - Pin: 16 RXD2
    - Pin: 17 TXD2

It has to be connected via 3.3V to RS232C level shifter to Console port on E-BOX:

    - RJ45 Pin: 3 TXD
    - RJ45 Pin: 4 GND
    - RJ45 Pin: 6 RXD

like so:

    - ESP32 Pin: 16 RXD2 ------ level shifter ------ RJ45-Cable-Connector Pin: 3 TXD
    - ESP32 Pin: 17 TXD2 ------ level shifter ------ RJ45-Cable-Connector Pin: 6 RXD
    - ESP32 PIN:     GND ------ level shifter ------ RJ45-Cable-Connector Pin: 4 GND
    - ESP32 PIN:    3.3v ------ level shifter

If it's not workin just swap RXD and TXD on one side.
My level shifter swaps RXD and TXT by itself. This ended in brain fuck.


## WiFi

At first start, there is no WiFi config. Just join "PYTES-E-BOX-48100-R" and connect to ""192.168.4.1" via browser.

## Is there a detailed Info?

maybe later ;-)

