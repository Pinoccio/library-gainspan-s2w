#include <GS.h>
#include <SPI.h>

GSModule gs;

// Run with this define to provision the network manager. Afterwards,
// run without and just use the NCM settings already present.
//#define GS_INIT
// Alternatively, run with this define to use the NCM without
// configuring it to auto-start.
#define GS_ONCE

#define SSID "Foo"
#define PASSPHRASE "Bar"

static void onNcmConnect(void *data, GSCore::cid_t cid) {
  ((Print*)data)->print("Event: NCM connected cid ");
  ((Print*)data)->println(cid);
}

void onNcmDisconnect(void *data) {
  ((Print*)data)->println("Event: NCM disconnected");
}

void onAssociate(void *data) {
  ((Print*)data)->println("Event: Associated");
}

void onDisassociate(void *data) {
  ((Print*)data)->println("Event: Disassociated");
}

void setup() {
  Serial.println("Gainspan Serial2Wifi demo");
  #ifdef VCC_ENABLE // For the Pinoccio scout
  pinMode(VCC_ENABLE, OUTPUT);
  digitalWrite(VCC_ENABLE, HIGH);
  #endif
  delay(2000);
  Serial.begin(115200);

  // Set event handlers and let them print to Serial
  gs.onNcmConnect = onNcmConnect;
  gs.onNcmDisconnect = onNcmDisconnect;
  gs.onAssociate = onAssociate;
  gs.onDisassociate = onDisassociate;
  gs.eventData = &Serial;

  // Use an UART
  //Serial1.begin(115200);
  //gs.begin(Serial1);

  // Use SPI with SS on pin 7
  gs.begin(7);

#if defined(GS_INIT) || defined(GS_ONCE)
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
#if defined(GS_INIT)
  gs.setNcm(/* enabled */ true, /* associate_only */ false, /* remember */ true);
  gs.saveProfile(0);
  gs.setDefaultProfile(0);
#else
  gs.setNcm(/* enabled */ true, /* associate_only */ false, /* remember */ false);
#endif
#endif

  while (!gs.isAssociated()) {
    Serial.println("Not associated yet, waiting...");
    delay(500);
    gs.loop();
  }

  GSCore::cid_t cid;
  while ((cid = gs.getNcmCid()) == GSModule::INVALID_CID) {
    Serial.println("Not connected yet, waiting...");
    delay(500);
    gs.loop();
  }

  Serial.println("Connected");

  uint8_t request[] = "GET / HTTP/1.0\r\n\r\n";
  gs.writeData(cid, request, sizeof(request) - 1);

  while(gs.getConnectionInfo(cid).connected) {
    int c = gs.readData(cid);
    if (c >= 0)
      Serial.write(c);
    gs.loop();
  }

  Serial.println("setup() done");
}

void loop() {
  gs.loop();
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

