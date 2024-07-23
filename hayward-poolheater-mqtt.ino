
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include "my_config.h"


#define MAX_TEMP 33
#define MIN_TEMP 15


// http://192.168.0.100:8080/update
// lolin wemos D1 mini lite

String autodiscoverPayload = "{"
                             "\"name\": \"Pool heatpump\","
                             "\"uniq_id\": \"poolHeatpumpControllerPC1000\","
                             "\"dev\": {"
                             "\"ids\": [\"pool_heatpump_controller\"],"
                             "\"mf\": \"njanik/hayward-pool-heater-mqtt\","
                             "\"name\": \"Pool heatpump\""
                             "},"
                             "\"opt\": false,"
                             "\"temp_unit\": \"C\","
                             "\"precision\": 0.5,"
                             "\"step\": 0.5,"
                             "\"temp_step\": 0.5,"
                             "\"max_temp\": " + String(MAX_TEMP) + ","
                             "\"min_temp\": " + String(MIN_TEMP) + ","
                             "\"modes\": [\"off\", \"auto\", \"heat\", \"cool\"],"
                             "\"mode_cmd_t\": \"pool/set_mode\","
                             "\"temp_cmd_t\": \"pool/set_temp\","
                             "\"mode_stat_t\": \"pool/mode\","
                             "\"curr_temp_t\": \"pool/temp_in\","
                             "\"avty_t\": \"pool/available\","
                             "\"platform\": \"mqtt\","
                             "\"state_topic\": \"pool/data\","
                             "\"json_attr_t\": \"pool/data\""
                             "}";

#define DEBUG 1

const char *ssid = SSID;
const char *password = WIFIPASS;
const char *mqtt_server = MQTT_HOST;

WiFiClient espClient;
PubSubClient client(espClient);

bool initialAutoDiscoverPublished = false;
unsigned long lastPublishAutoDiscoverTime = 0;
unsigned long rePublishAutoDiscoverInterval = 5 * 60 * 1000; // 5 min

ESP8266WebServer server(8080);
ESP8266HTTPUpdateServer httpUpdater;

#define PIN D5
#define COOL B00000000
#define HEAT B00001000
#define AUTO B00000100



long rssi = 0;
float cmdTemp;
byte cmdMode;
boolean cmdPower;
boolean enablePublishingCurrentValues = true;

float currentProgTemp = 0;

float currentTempOut = 0;
float currentTempIn = 0;
byte currentMode = 255;
boolean currentPower = false;

boolean isProcessingCmd = false;

byte defaultMaskPowerMode = 96;
byte defaultMaskTemp = 2;

byte wrongValueIterationTempIn = 0;
byte wrongValueIterationTempOut = 0;

// static CMD trame values
unsigned char cmdTrame[12] = {
    129, // 0 - HEADER
    141,
    232, // 2 - POWER &MODE
    6,
    238, // 4 - TEMP
    30,
    188,
    188,
    188,
    188,
    0,
    0, // 11 - CHECKSUM
};

byte state = 0;
byte highCpt = 0;
byte buffer = 0;
byte cptBuffer = 0;
byte wordCounter = 0;
byte trame[40] = {};

String modeToString(byte mode)
{
    if (mode == HEAT)
    {
        return "heat";
    }
    else if (mode == COOL)
    {
        return "cool";
    }
    else if (mode == AUTO)
    {
        return "auto";
    }
    return "unknown_mode";
}

void setup_wifi()
{

    delay(10);
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    WiFi.hostname(HARDWARE_HOSTNAME);
}

void MQTT_reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        if (client.connect(MQTT_CLIENT_NAME, MQTT_USER, MQTT_PASS))
        {
            Serial.println("connected");
            client.publish("pool", "connected");
            client.loop();

            client.subscribe("pool/set_power_on");
            client.loop();
            // client.subscribe("pool/set_power_off");
            // client.loop();
            client.subscribe("pool/set_temp");
            client.loop();
            client.subscribe("pool/set_mode");
            client.loop();
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

void mqttMsgReceivedCallBack(char *topic, byte *payload, unsigned int length)
{

    enablePublishingCurrentValues = false;

    #ifdef DEBUG
    Serial.print("Message arrived[");
    Serial.print(topic);
    Serial.print("] ");
    Serial.print("length: ");
    Serial.println(length);
    Serial.print("Value: ");
    for (int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println();
    #endif

    if (isProcessingCmd)
    {
        // A processing cmd is already running. Ignoring this one.
        enablePublishingCurrentValues = true;
        return;
    }

    isProcessingCmd = true;

    cmdPower = true;
    cmdMode = currentMode;
    cmdTemp = currentProgTemp;

    if (strcmp(topic, "pool/set_power_on") == 0)
    {
        cmdPower = true;
    }
    else if (strcmp(topic, "pool/set_mode") == 0)
    {

        bool changedToOff = false;

        payload[length] = '\0';
        if (strcmp((char *)payload, "auto") == 0)
        {
            cmdMode = AUTO;
        }
        else if (strcmp((char *)payload, "cool") == 0)
        {
            cmdMode = COOL;
        }
        else if (strcmp((char *)payload, "heat") == 0)
        {
            cmdMode = HEAT;
        }
        else if (strcmp((char *)payload, "off") == 0)
        {
            changedToOff = true;
            cmdPower = false;
        }

        // for the UX to display the good cmd quickly
        if (!changedToOff)
        {
            client.publish("pool/mode", modeToString(cmdMode).c_str());
        } else {
            client.publish("pool/mode", "off");
        }
    }
    else if (strcmp(topic, "pool/set_temp") == 0)
    {
        payload[length] = '\0';
        String s = String((char *)payload);
        float temp = s.toFloat();

        if (temp >= MIN_TEMP && temp <= MAX_TEMP)
        {
            cmdTemp = temp;
        }
    }
    else
    {
        isProcessingCmd = false;
        enablePublishingCurrentValues = true;
        return;
    }

    #ifdef DEBUG
    Serial.print("cmdPower: ");
    Serial.println(cmdPower);
    Serial.print("cmdMode: ");
    Serial.println(modeToString(cmdMode));
    Serial.print("cmdTemp: ");
    Serial.println(cmdTemp);
    #endif


    if (currentMode == 255 || currentProgTemp == 0)
    {
        // 255 is a dummy value that i've used to init the mode
        // if we are here, we still don't have all of the current parameter. We can't send a cmd without knowing all current parameters
        isProcessingCmd = false;
        #ifdef DEBUG
        Serial.println("Can't send cmd. Waiting for all current parameters");
        #endif
        enablePublishingCurrentValues = true;
        return;
    }

    Serial.println("prepareCmdTrame");
    prepareCmdTrame();
    Serial.println("sendCmdTrame");
    sendCmdTrame();
    Serial.println("resetRecevingTrameProcess");
    resetRecevingTrameProcess();
    isProcessingCmd = false;
    enablePublishingCurrentValues = true;
}

void resetRecevingTrameProcess()
{
    pinMode(PIN, INPUT);
    state = 0;
    highCpt = 0;
    cptBuffer = 0;
    buffer = 0;
    wordCounter = 0;
    memset(trame, 0, sizeof(trame));
}

void resetTempAndPowerModeMask()
{
    cmdTrame[2] = defaultMaskPowerMode;
    cmdTrame[4] = defaultMaskTemp;
}

void prepareCmdTrame()
{
    resetTempAndPowerModeMask();
    setPowerInTrame(cmdPower);
    setModeInTrame(cmdMode);
    setTempInTrame(cmdTemp);
    generateChecksumInTrame();
}

void sendCmdTrame()
{

    pinMode(PIN, OUTPUT);
    // repeat the trame 8 times
    for (byte occurrence = 0; occurrence < 8; occurrence++)
    {
        yield();
        sendHeaderCmdTrame();
        for (byte trameIndex = 0; trameIndex <= 11; trameIndex++)
        {
            byte value = cmdTrame[trameIndex];
            for (byte bitIndex = 0; bitIndex < 8; bitIndex++)
            {
                byte bit = (value << bitIndex) & B10000000;
                if (bit) {
                    sendBinary1();
                } else {
                    sendBinary0();
                }
            }
        }

        if (occurrence < 7) {
            sendSpaceCmdTrame();
        } else {
            sendSpaceCmdTramesGroup();
            Serial.println("Cmd trame sent");
        }
    }
}

void sendBinary0()
{
    _sendLow(1);
    _sendHigh(1);
}

void sendBinary1()
{
    _sendLow(1);
    _sendHigh(3);
}

void sendHeaderCmdTrame()
{
    _sendLow(9);
    _sendHigh(5);
}

// between trame repetition
void sendSpaceCmdTrame()
{
    _sendLow(1);
    _sendHigh(100);
}

// after the 8th trame sent
void sendSpaceCmdTramesGroup()
{
    _sendLow(1);
    // to avoid software watchdog reset due to the long 2000ms delay, we cut the 2000ms in 4x500ms and feed the wdt each time.
    for (byte i = 0; i < 4; i++)
    {
        _sendHigh(500);
        ESP.wdtFeed();
    }
}

void _sendHigh(word ms)
{
    digitalWrite(PIN, HIGH);
    delayMicroseconds(ms * 1000);
}

void _sendLow(word ms)
{
    digitalWrite(PIN, LOW);
    delayMicroseconds(ms * 1000);
}

bool setTempInTrame(float temperature)
{
    byte temp = temperature;
    bool halfDegree = ((temperature * 10) - (temp * 10)) > 0;

    if (temp < MIN_TEMP || temp > MAX_TEMP)
    {
        Serial.println("Error setTemp: Value must be between " + String(MIN_TEMP) + " & " + String(MAX_TEMP));
        return false;
    }

    byte value = temp - 2;
    value = reverseBits(value);
    value = value >> 1;
    cmdTrame[4] = cmdTrame[4] | value;
    if (halfDegree)
    {
        cmdTrame[4] = cmdTrame[4] | B10000000;
    }
    return true;
}

bool setModeInTrame(byte mode)
{
    byte mask;

    switch (mode)
    {
    case HEAT:
        mask = HEAT;
        break;
    case COOL:
        mask = COOL;
        break;
    case AUTO:
        mask = AUTO;
        break;
    default:
        Serial.println("Error setMode: Unknown mode");
        return false;
    }
    cmdTrame[2] = cmdTrame[2] | mask;
    return true;
}

bool setPowerInTrame(bool power)
{
    byte mask = power ? B10000000 : B00000000;
    cmdTrame[2] = cmdTrame[2] | mask;
    return true;
}

byte generateChecksumInTrame()
{
    unsigned int total = 0;
    for (byte i = 0; i < 11; i++)
    {
        total += reverseBits(cmdTrame[i]);
    }
    byte checksum = total % 256;
    checksum = reverseBits(checksum);
    cmdTrame[11] = checksum;
    return checksum;
}

// void printCmdTrame() {
//   Serial.println("--------------------");
//   for(byte i=0; i<=11; i++) {
//     Serial.println(cmdTrame[i], BIN);
//   }
//   Serial.println("--------------------");
// }

bool checksumIsValid(byte trame[], byte size)
{
    int value = 0;
    value = reverseBits(trame[size - 1]) - checksum(trame, size);
    return value == 0;
}

byte checksum(byte trame[], byte size)
{
    int total = 0;
    for (int i = 0; i < (size - 1); i++)
    {
        total += reverseBits(trame[i]);
    }
    return total % 256;
}


float fixTempValue(float newValue, float previousValue, byte &wrongValueIteration)
{
    //sometime, not very often and I don't why, even if the checksum is correct, I received
    //wrong temp data. This function ignore the value if there is more than 1.5 degree of
    //difference with the previous temp value

    if (previousValue == 0) {
        return newValue;
    }

    if (abs(newValue - previousValue) <= 1.5 || wrongValueIteration >= 8)
    {
        wrongValueIteration = 0;
        return newValue;
    }

    wrongValueIteration++;
    return previousValue;
}

void publishCurrentParams()
{
    // Publish info to MQTT server
    if (enablePublishingCurrentValues && client.connected() && currentTempOut > 0)
    {
        client.publish("pool/available", "online");
        client.publish("pool/power", String(currentPower).c_str());
        if (currentPower)
        {
            client.publish("pool/mode", modeToString(currentMode).c_str());
        }
        else
        {
            client.publish("pool/mode", "off");
        }
        client.publish("pool/temp_in", String(currentTempIn).c_str());
        client.publish("pool/temp_out", String(currentTempOut).c_str());
        client.publish("pool/temp_prog", String(currentProgTemp).c_str());
        client.publish("pool/wifi_rssi", String(rssi = WiFi.RSSI()).c_str());

        // all the data in one json
        String jsonData = "{";

        jsonData += "\"available\":\"online\",";
        jsonData += "\"power\":\"" + String(currentPower) + "\",";
        if (currentPower)
        {
            jsonData += "\"mode\":\"" + String(modeToString(currentMode)) + "\",";
        }
        else
        {
            jsonData += "\"mode\":\"off\",";
        }
        jsonData += "\"temp_in\":\"" + String(currentTempIn) + "\",";
        jsonData += "\"temp_out\":\"" + String(currentTempOut) + "\",";
        jsonData += "\"temp_prog\":\"" + String(currentProgTemp) + "\",";
        jsonData += "\"wifi_rssi\":\"" + String(WiFi.RSSI()) + "\"";

        jsonData += "}";

        client.publish("pool/data", jsonData.c_str());

        Serial.println("params Published to mqtt server");
    }
}

// Reverse the order of bits in a byte.
// I.e. MSB is swapped with LSB, etc.
byte reverseBits(unsigned char x)
{
    x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
    x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
    x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);
    return x;
}

void setup()
{

    client.setBufferSize(650);
    Serial.begin(115200);
    setup_wifi();
    delay(500);

    // OTA webserver
    // httpUpdater.setup(&server);
    // server.on("/", HTTP_GET, []()
    //           { server.send(200, "text/plain", "Ok"); });
    // server.begin();

    client.setServer(mqtt_server, 1883);
    client.setCallback(mqttMsgReceivedCallBack);
    Serial.println("Setup completed");
}




void loop()
{
    if (!client.connected())
    {
        // Serial.println("Not connected to mqtt");
        MQTT_reconnect();
        resetRecevingTrameProcess();
    }
    client.loop();

    delayMicroseconds(200);

    if (!initialAutoDiscoverPublished || (millis() - lastPublishAutoDiscoverTime) >= rePublishAutoDiscoverInterval)
    {
        client.publish("pool/debug", "DEBUG");
        client.loop();
        client.publish("homeassistant/climate/PoolHeater/config", autodiscoverPayload.c_str());
        client.loop();
        lastPublishAutoDiscoverTime = millis();
        initialAutoDiscoverPublished = true;
    }

    state = digitalRead(PIN);
    if (!state)
    {
        if (highCpt)
        {

            if (highCpt >= 22 && highCpt <= 28)
            {
                // 5ms = 25 * 200us
                //  START TRAME
                cptBuffer = 0;
                buffer = 0;
                wordCounter = 0;
            }
            else if (highCpt >= 12 && highCpt <= 18)
            {
                // 3ms = 15 * 200us
                //  BINARY O
                buffer = buffer << 1;
                cptBuffer++;
            }
            else if (highCpt >= 2 && highCpt <= 8)
            {
                // 1ms = 5 * 200us
                //  BINARY 1
                buffer = buffer << 1;
                buffer = buffer | 1;
                cptBuffer++;
            }

            if (cptBuffer == 8)
            {
                // we have captured 8 bits = 1 complete byte
                trame[wordCounter] = buffer;
                if (wordCounter < 20)
                {
                    // overflow verification
                    wordCounter++;
                }
                cptBuffer = 0;
                buffer = 0;
            }

            highCpt = 0; // reset the counter for HIGH value
        }
    }
    else
    {

        highCpt++;
        if (highCpt > (50000 / 200) && wordCounter)
        {
            // If HIGH more than 50ms, trame is hardly ended

            if (trame[0] == B01001011 && checksumIsValid(trame, wordCounter))
            {
                // GET THE TEMP OUT
                unsigned char temp = reverseBits(trame[4]);
                temp &= B00111110;
                temp = temp >> 1;
                temp = temp + 2;

                bool halfDegree = (trame[4] & B10000000) >> 7;
                float ftempOut = temp;
                if (halfDegree)
                {
                    ftempOut = ftempOut + 0.5;
                }
                currentTempOut = fixTempValue(ftempOut, currentTempIn, wrongValueIterationTempOut);
                Serial.print("TEMP OUT:");
                Serial.println(currentTempOut);
            }
            else if (trame[0] == B10001011 && checksumIsValid(trame, wordCounter))
            {
                // GET THE TEMP IN
                unsigned char temp = reverseBits(trame[9]);
                temp &= B00111110;
                temp = temp >> 1;
                temp = temp + 2;

                bool halfDegree = (trame[9] & B10000000) >> 7;
                float ftempIn = temp;
                if (halfDegree)
                {
                    ftempIn = ftempIn + 0.5;
                }

                currentTempIn = fixTempValue(ftempIn, currentTempIn, wrongValueIterationTempIn);
                Serial.print("TEMP IN:");
                Serial.println(currentTempIn);
            }
            else if (trame[0] == B10000001 && checksumIsValid(trame, wordCounter))
            {

                // GET THE PROGRAMMED TEMP
                unsigned char temp = reverseBits(trame[4]);
                temp &= B00111110;
                temp = temp >> 1;
                temp = temp + 2;
                bool halfDegree = (trame[4] & B10000000) >> 7;
                float ftemp = temp;
                if (halfDegree)
                {
                    ftemp = ftemp + 0.5;
                }
                currentProgTemp = ftemp;
                Serial.print("PROG TEMP:");
                Serial.println(currentProgTemp);

                // CHECK THE POWER STATE
                currentPower = (trame[2] & B10000000) >> 7;
                Serial.print("POWER:");
                Serial.println(currentPower);

                // CHECK THE AUTOMATIC MODE
                byte automatic_mode = (trame[2] & B00000100) >> 2;
                if (automatic_mode)
                {
                    currentMode = AUTO;
                }
                else
                {
                    // CHECK HEAT OR COLD
                    boolean heat = (trame[2] & B00001000) >> 3;
                    currentMode = heat ? HEAT : COOL;
                }
                Serial.print("MODE:");
                Serial.println(modeToString(currentMode));

                publishCurrentParams();
            }
            wordCounter = 0;
        }
    }
    server.handleClient();
}
