#include <GS.h>
#include <SPI.h>

GSModule gs;

// Run with this define to provision the network manager. Afterwards,
// run without and just use the NCM settings already present.
//#define GS_INIT

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

  // Use SPI at 2Mhz (GS1500 supports up to 3.5Mhz)
  SPI.setClockDivider(SPI_CLOCK_DIV8);
  SPI.begin();
  gs.begin(7);

#ifdef GS_INIT
  // Enable DHCP
  gs.setDhcp(true, "pinoccio");

  // Associate
  gs.setAuth(GSModule::GS_AUTH_NONE);
  gs.setSecurity(GSModule::GS_SECURITY_WPA_PSK);
  gs.setWpaPassphrase(PASSPHRASE);


  gs.setAutoAssociate(SSID);
  // TCP connect (google.com)
  IPAddress ip(173, 194, 65, 101);
  gs.setAutoConnectClient(ip, 80);
  gs.setNcm(true, false, true);
  gs.saveProfile(0);
  gs.setDefaultProfile(0);
#endif

  while (!gs.isAssociated()) {
    Serial.println("Not associated yet, waiting...");
    delay(500);
  }

  GSCore::cid_t cid;
  while ((cid = gs.getNcmCid()) == GSModule::INVALID_CID) {
    Serial.println("Not connected yet, waiting...");
    delay(500);
  }

  Serial.println("Connected");

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

