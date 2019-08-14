# Hayward pool heater MQTT bridge

This is a little project I've been working for a while now. I'm the owner of a Hayward pool heater (*Trevium HP55TR, which is also the same exact model of Hayward energy line pro* )
This heat pump use a controller called **PC1000**.

I have decoded the data using a small logic sniffer.

This version of the sketch is working on a **wemos d1 mini** (using the arduino IDE with **arduino core** installed)

*Note: Actually, this skecth only **get** the data. I don't know yet how to set the data.*

The schema is very simple.

You have to connect the `NET` pin of the PC1000 controller to your esp8266 via a voltage divider, them, connect the PC1000 `GND` to the `GND` of your esp8266.

```
        PC 1000
+---------------+
|               |
|       NET pin +--------------+
|               |              |
|               |              |
|               |            +-+-+                  WEMOS D1 lite / ESP8266
|               |            |   |
|               |            |   |  10K              +----------------+
|               |            |   |                   |                |
|               |            +-+-+                   |                |
|               |              |                     |                |
|               |              +---------------------+  D6 pin        |
|               |              |                     |                |
|               |            +-+-+                   |                |
|               |            |   |                   |                |
|               |            |   |  20K              |                |
|               |            |   |                   |                |
|               |            +-+-+                   |                |
|               |              |                     |                |
|               |              |                     |                |
|       GND pin +--------------+---------------------+  GND pin       |
|               |                                    |                |
|               |                                    +----------------+
+---------------+

```


**MQTT topics**

Data will be published on your MQTT server every few seconds using this topics:

- `pool/power`  (true / false)
- `pool/mode` (heat / cool)
- `pool/automatic_mode` (true / false) Automatic = heat or cold according to the programmed temp and the out temperature
- `pool/temp_out`  (temperature out in celcius)
- `pool/temp_prog`  (programmed temperature in celcius)
