// ralfmayet @elggem september 2021
// TODO
// turn off timestamp in append call like https://github.com/fabianoriccardi/esp-logger/blob/286a9fefb47c1a9f56b70cad47447cb7863084d4/src/logger_spiffs.h#L45

#include <logger_spiffs.h>
#include <WiFiMulti.h>
#include <InfluxDbClient.h>
#include <Ticker.h>
#include <SPIFFS.h>

#define WIFI_SSID "xxx"
#define WIFI_PASSWORD "xxx"

#define NTP_SERVER "de.pool.ntp.org"
#define TZ_INFO "WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00" // Western European Time

#define INFLUXDB_URL "xxx"
#define INFLUXDB_DB_NAME "xxx"
#define INFLUXDB_USER "xxx"
#define INFLUXDB_PASSWORD "xxx"

#define WRITE_PRECISION WritePrecision::S
#define MAX_BATCH_SIZE 128
#define WRITE_BUFFER_SIZE 256

#define DEVICE "ESP32"

WiFiMulti wifiMulti;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_DB_NAME);
LoggerSPIFFS myLog("/log/mylog.log");

// Data point
Point sensor("wifi_status");
Point storage("storage_status");

void setup() {
  Serial.begin(115200);
  while (!Serial);
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
  // Set time precision batching and retry buffer
  client.setWriteOptions(WriteOptions().writePrecision(WRITE_PRECISION).batchSize(MAX_BATCH_SIZE).bufferSize(WRITE_BUFFER_SIZE));
  //client.setHTTPOptions(HTTPOptions().connectionReuse(true));

 
  // Add constant tags - only once
  storage.addTag("device", DEVICE);
  sensor.addTag("device", DEVICE);
  sensor.addTag("SSID", WiFi.SSID());

  //setup Log
  myLog.begin();
  myLog.setSizeLimit(500000); //over 24 hours, does it fit into memory is the question..
  myLog.setSizeLimitPerChunk(100000); // 100k bytes = 100kb, does this hit the memory limit?
  myLog.setFlusherCallback(flushCallback);
  // initial flush to make sure all is well.
  myLog.flush();

}

int counter = 0;

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
  delay(100);
  counter += 1;

  if (counter >= 20000) { // ca 230ms per cycle
    counter = 0;
    myLog.flush();
  }

}

bool flushCallback(char* buffer, int n) {
  Serial.println("flush CB called n = " + String(n)); 
  int index = 0;

  // Check if there is another string to print
  while (index < n && strlen(&buffer[index]) > 0) {
    String line = String(&buffer[index]);
    index += line.length() + 1;

    // get timestamp
    String timestampString = split(line, ' ', 1);
    // get measurement
    String valueString = split(line, ' ', 2);
    int value = atol(valueString.c_str());

    // debug print
    //Serial.println("TS: " + String(timestampString) + " VAL: " + String(value));

    sensor.clearFields();
    sensor.setTime(timestampString); //unsigned long long or string
    sensor.addField("rssi", value);

    // Write point
    if (!client.writePoint(sensor)) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
      return false;
    }
  }

  // If no Wifi signal, try to reconnect it
  if (wifiMulti.run() != WL_CONNECTED) {
    Serial.println("Wifi connection lost");
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
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}  // END
