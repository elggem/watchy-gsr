// ralfmayet @elggem september 2021
// TODO
// turn off timestamp in append call like https://github.com/fabianoriccardi/esp-logger/blob/286a9fefb47c1a9f56b70cad47447cb7863084d4/src/logger_spiffs.h#L45

#include <logger_spiffs.h>
#include <WiFiMulti.h>
#include <InfluxDbClient.h>
#include <Ticker.h>
#include <SPIFFS.h>

#define WIFI_SSID "SSID"
#define WIFI_PASSWORD "PW"

#define NTP_SERVER "de.pool.ntp.org"
#define TZ_INFO "WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00" // Western European Time

#define INFLUXDB_URL "xxx"
#define INFLUXDB_DB_NAME "xxx"
#define INFLUXDB_USER "xxx"
#define INFLUXDB_PASSWORD "xxx"

#define WRITE_PRECISION WritePrecision::S
#define MAX_BATCH_SIZE 1000
#define WRITE_BUFFER_SIZE 2000

#define DEVICE "ESP32"

WiFiMulti wifiMulti;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_DB_NAME);
LoggerSPIFFS myLog("/log/mylog.log");

// Data point
Point sensor("wifi_status");
Point storage("storage_status");



// Event generation period, in seconds
Ticker flushTicker;
float flushPeriod = 10 * 60;

void setup() {
  Serial.begin(115200);
  while(!Serial);
  Serial.println();
  Serial.println("GSR DEMO SKETCH");

  // Connect WiFi
  Serial.println("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
  }

  // Synchronize time with NTP servers and set timezone
  configTzTime(TZ_INFO, NTP_SERVER);
  
  //Set InfluxDB 1 authentication params
  client.setConnectionParamsV1(INFLUXDB_URL, INFLUXDB_DB_NAME, INFLUXDB_USER, INFLUXDB_PASSWORD);
  // Set time precision
  client.setWriteOptions(WriteOptions().writePrecision(WRITE_PRECISION).batchSize(MAX_BATCH_SIZE).bufferSize(WRITE_BUFFER_SIZE));

  // Add constant tags - only once
  sensor.addTag("device", DEVICE);
  storage.addTag("device", DEVICE);
  sensor.addTag("SSID", WiFi.SSID());

//  // Check server connection
//  if (client.validateConnection()) {
//    Serial.print("Connected to InfluxDB: ");
//    Serial.println(client.getServerUrl());
//  } else {
//    Serial.print("InfluxDB connection failed: ");
//    Serial.println(client.getLastErrorMessage());
//  }

  //setup Log
  myLog.begin();
  myLog.setSizeLimit(500000); //over 24 hours, does it fit into memory is the question..

  myLog.setFlusherCallback(flushCallback);

  // flushing mechanism
  flushTicker.attach(flushPeriod, flushTrigger);
}

void flushTrigger() {
  myLog.flush();
}

void loop() {
  time_t now;
  time(&now);
  
  String record = now + String(" ") + WiFi.RSSI();
  myLog.append(record.c_str());

  Serial.println(String("Now the log takes ") + myLog.getActualSize() + "/" + myLog.getSizeLimit());
  Serial.println(String("SPIFFS used ") + SPIFFS.usedBytes() + "/" + SPIFFS.totalBytes());

  storage.clearFields();
  storage.addField("usedLog", myLog.getActualSize());
  storage.addField("totalLog", myLog.getSizeLimit());
  storage.addField("usedSPI", SPIFFS.usedBytes());
  storage.addField("totalSPI", SPIFFS.totalBytes());
  client.writePoint(storage);
  client.flushBuffer();
  
  //wait 5s
  delay(5000);

}

bool flushCallback(char* buffer, int n){
  int index=0;
  // Check if there is another string to print
  while(index<n && strlen(&buffer[index])>0){
    String line = String(&buffer[index]);
    index += line.length()+1;

    // get timestamp
    String timestampString = split(line, ' ',1);
    unsigned long timestamp = atol(timestampString.c_str());

    // get measurement
    String valueString = split(line, ' ',2);
    int value = atol(valueString.c_str());

    // debug print
    Serial.println("TS: " + String(timestamp) + " VAL: " + String(value));

    sensor.clearFields();
    sensor.setTime(timestampString); //unsigned long long or string
    sensor.addField("rssi", value);
    
    // If no Wifi signal, try to reconnect it
    if (wifiMulti.run() != WL_CONNECTED) {
      Serial.println("Wifi connection lost");
    }
    // Write point
    if (!client.writePoint(sensor)) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
      client.flushBuffer();
      return false;
    }
  }
  if (!client.flushBuffer()) {
    Serial.print("InfluxDB flush failed: ");
    Serial.println(client.getLastErrorMessage());
    Serial.print("Full buffer: ");
    Serial.println(client.isBufferFull() ? "Yes" : "No");
  }
  return true;
}


//helpers
String split(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length();

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}  // END
