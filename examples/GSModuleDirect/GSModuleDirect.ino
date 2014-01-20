#include <GS.h>
#include <SPI.h>

GSModule gs;

#define SSID "Foo"
#define PASSPHRASE "Bar"

static void print_line(const uint8_t *buf, uint16_t len, void *data) {
  static_cast<Print*>(data)->write(buf, len);
  static_cast<Print*>(data)->println();
}

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

  // Use SPI at 2Mhz (GS1500 supports up to 3.5Mhz)
  SPI.setClockDivider(SPI_CLOCK_DIV8);
  SPI.begin();
  gs.begin(7);

  // Write a custom command
  gs.writeCommand("AT");
  gs.readResponse();

  // Disable the NCM, just in case it was set to autostart. Wait a bit
  // before doing so, because it seems that if the NCM is configured to
  // start on boot and we try to disable it within the first second or
  // so, the module locks up...
  delay(1000);
  gs.setNcm(false);

  // Write command and read any extra data returned
  Serial.println("Displaying configuration...");
  Serial.println();
  gs.writeCommand("AT&V");
  gs.readResponse(print_line, &Serial);

  // TODO: Add a proper API for scanning
  Serial.println("Scanning...");
  gs.writeCommand("AT+WS");
  gs.readResponse(print_line, &Serial);

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

