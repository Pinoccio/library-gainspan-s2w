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
  Serial.begin(115200);
  Serial.println("Gainspan UDP Server demo");
  #ifdef VCC_ENABLE // For the Pinoccio scout
  pinMode(VCC_ENABLE, OUTPUT);
  digitalWrite(VCC_ENABLE, HIGH);
  #endif
  delay(2000);

  // Use an UART
  //Serial1.begin(115200);
  //gs.begin(Serial1);

  // Use SPI at 2Mhz (GS1500 supports up to 3.5Mhz)
  SPI.setClockDivider(SPI_CLOCK_DIV8);
  SPI.begin();
  gs.begin(7);

  // Disable the NCM, just in case it was set to autostart. Wait a bit
  // before doing so, because it seems that if the NCM is configured to
  // start on boot and we try to disable it within the first second or
  // so, the module locks up...
  delay(1000);
  gs.setNcm(false);

  // Enable DHCP
  gs.setDhcp(true, "pinoccio");
  //gs.setStaticIp(IPAddress(192, 168, 2, 2), IPAddress(255, 255, 255, 0), IPAddress(192, 168, 2, 1));

  // Associate
  //gs.setAuth(GSModule::GS_AUTH_NONE);
  gs.setSecurity(GSModule::GS_SECURITY_WPA_PSK);
  gs.setWpaPassphrase(PASSPHRASE);
  while(!gs.associate(SSID)) {
    Serial.println("Association failed, retrying...");
    gs.loop();
  }

  Serial.println("Associated to " SSID);

  // UDP server
  GSUdpServer server(gs);
  if(!server.begin(42424))
    Serial.println("Bind failed");

  // Wait for a single UDP packet
  size_t len;
  while (!len)
   len = server.parsePacket();

  Serial.print("packet len ");
  Serial.print(len);
  Serial.print(" from ");
  Serial.print(server.remoteIP());
  Serial.print(":");
  Serial.print(server.remotePort());
  Serial.println();

  while (server.available()) {
    int c = server.read();
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

