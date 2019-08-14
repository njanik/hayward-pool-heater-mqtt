#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

#include "my_config.h";

const char * ssid = SSID;
const char * password = WIFIPASS;
const char * mqtt_server = MQTT_HOST;

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[10];

bool inter = false;

byte state = 0;
uint16_t highCpt = 0;

byte buffer = 0;
byte cptBuffer = 0;
byte wordCounter = 0;

byte trame[40] = {};

byte power = 0;
float temp_out = 0;
float temp_programmed = 0;
byte mode = 0;
byte automatic_mode = 0;

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  WiFi.hostname(HARDWARE_HOSTNAME);
}



void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(MQTT_CLIENT_NAME, MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
      client.publish("pool", "connected");
      client.loop();

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  Serial.println("Hayward-pool-heater-mqtt starting...");
  pinMode(D5, INPUT);
  delay(500);
  client.setServer(mqtt_server, 1883);
}

byte checksum(byte trame[], byte size) {

  int total = 0;
  for (int i = 0; i < (size - 1); i++) {
    total += Bit_Reverse(trame[i]);
  }
  return (total - 3) % 255;
}

bool checksumIsValid(byte trame[], byte size) {
  int value = 0;
  value = Bit_Reverse(trame[size - 1]) - checksum(trame, size);
  //I don't know why, but there often a difference of 1 even if it seems ok... So for the
  //moment, I accept 0 or 1 as difference.
  return (value == 0 || value == 1);
}

void loop() {

  //IMPORTANT NOTE: On an "Arduino UNO/NANO", you don't need this because at 8Mhz, the loop of this
  // sketch take 200 microseconds. I added this when I ported this skecth to run on an esp8266.
  delayMicroseconds(200);

  state = digitalRead(D5);
  if (!state) {
    //End of the HIGH state
    if (highCpt) {
      //5ms = 25 * 200us
      if (highCpt >= 22 && highCpt <= 28) {

        // START TRAME
        cptBuffer = 0;
        buffer = 0;
        wordCounter = 0;

        //3ms = 15 * 200us
      } else if (highCpt >= 12 && highCpt <= 18) {
        // BINARY O
        buffer = buffer << 1;
        cptBuffer++;

        //1ms = 5 * 200us
      } else if (highCpt >= 2 && highCpt <= 8) {
        // BINARY 1
        buffer = buffer << 1;
        buffer = buffer | 1;
        cptBuffer++;
      }

      if (cptBuffer == 8) {
        //we have captured 8 bits, so this 1 byte to add in our trame array
        trame[wordCounter] = buffer;
        if (wordCounter < 40) { //overflow verification
          wordCounter++;
        }

        cptBuffer = 0;
        buffer = 0;
      }

      //reset the counter for HIGH value
      highCpt = 0;
    }

  } else {

    highCpt++;
    if (highCpt > 40 && wordCounter) {

      if (trame[0] == B01001011 && checksumIsValid(trame, wordCounter)) {

        checksumIsValid(trame, wordCounter);
        // GET THE OUT TEMP
        unsigned char temp = Bit_Reverse(trame[4]);
        temp &= B00111110;
        temp = temp >> 1;
        temp = temp + 2;

        bool halfDegree = (trame[4] & B10000000) >> 7;

        float ftempOut = temp;
        if (halfDegree) {
          ftempOut = ftempOut + 0.5;
        }

        temp_out = ftempOut;

        Serial.print("TEMP OUT:");
        Serial.println(ftempOut);
      } else if (trame[0] == B10000001 && checksumIsValid(trame, wordCounter)) {

        // GET THE TEMPERATURE PROGRAMMED
        unsigned char temp = Bit_Reverse(trame[4]);
        temp &= B00111110;
        temp = temp >> 1;
        temp = temp + 2;

        bool halfDegree = (trame[4] & B10000000) >> 7;

        float ftemp = temp;
        if (halfDegree) {
          ftemp = ftemp + 0.5;
        }

        temp_programmed = ftemp;

        Serial.print("PROG TEMP:");
        Serial.println(ftemp);

        // CHECK THE POWER STATE
        power = (trame[2] & B10000000) >> 7;
        Serial.print("POWER:");
        Serial.println(power);

        // CHECK THE MODE (HEAT OR COLD)
        mode = (trame[2] & B00001000) >> 3;
        Serial.print("HEAT:");
        Serial.println(mode);

        // CHECK THE AUTOMATIC MODE
        automatic_mode = (trame[2] & B00000100) >> 2;
        //Serial.print("AUTOMATIC:"); Serial.println(automatic_mode);


        //Publish info to MQTT server
        if(client.connected() && temp_out > 0) {
          client.publish("pool/power", String(power).c_str());
          client.publish("pool/mode", String(mode).c_str());
          client.publish("pool/temp_out", String(temp_out).c_str());
          client.publish("pool/temp_prog", String(temp_programmed).c_str());
          client.publish("pool/automatic_mode", String(automatic_mode).c_str());
          Serial.println("Published!");
        }

      }
      wordCounter = 0;
    }
  }

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

}

// Reverse the order of bits in a byte.
// I.e. MSB is swapped with LSB, etc.
byte Bit_Reverse(unsigned char x) {
  x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
  x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
  x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);
  return x;
}
