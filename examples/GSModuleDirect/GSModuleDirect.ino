#include <GS.h>
#include <SPI.h>

GSModule gs;

#define SSID "Foo"
#define PASSPHRASE "Bar"

void setup() {
  Serial.println("Gainspan Serial2Wifi demo");
  #ifdef VCC_ENABLE // For the Pinoccio scout
  pinMode(VCC_ENABLE, OUTPUT);
  digitalWrite(VCC_ENABLE, HIGH);
  #endif
  delay(2000);
  Serial.begin(115200);

  // Use an UART
  //Serial1.begin(115200);
  //gs.begin(Serial1);

  // Use SPI
  SPI.setClockDivider(SPI_CLOCK_DIV16);
  SPI.begin();
  gs.begin(7);

  // Write a custom command
  gs.writeCommand("AT");
  gs.readResponse();

  // Write command and read any extra data returned
  Serial.println("Displaying configuration...");
  Serial.println();
  uint8_t buf[1024];
  uint16_t len = sizeof(buf);
  gs.writeCommand("AT&V");
  gs.readResponse(buf, &len, NULL);
  Serial.write((char*)buf, len);

  // Enable DHCP
  gs.setDhcp(true, "pinoccio");

  // Associate
  gs.setAuth(GSModule::GS_AUTH_NONE);
  gs.setSecurity(GSModule::GS_SECURITY_WPA_PSK);
  gs.setWpaPassphrase(PASSPHRASE);
  while(!gs.associate(SSID))
    Serial.println("Association failed, retrying...");

  Serial.println("Associated to " SSID);

  // TCP connect (google.com)
  IPAddress ip(173, 194, 65, 101);
  GSModule::cid_t cid;
  while ((cid = gs.connectTcp(ip, 80)) == GSModule::INVALID_CID) {
    Serial.println("Connection failed, retrying...");
    delay(500);
  }

  Serial.print("Connected to ");
  Serial.println(ip);

  uint8_t request[] = "GET / HTTP/1.0\r\n\r\n";
  gs.writeData(cid, request, sizeof(request) - 1);

  while(gs.getConnectionInfo(cid).connected) {
    int c = gs.readData(cid);
    if (c >= 0)
      Serial.write(c);
  }

  Serial.println("setup() done");
}

void loop() {
  // Allow interactive command sending
  int c = gs.readRaw();
  if (c >= 0)
    Serial.write(c);

  if (Serial.available()) {
    uint8_t b = Serial.read();
    gs.writeRaw(&b, 1);
  }
}

/* vim: set filetype=cpp softtabstop=2 shiftwidth=2 expandtab: */

