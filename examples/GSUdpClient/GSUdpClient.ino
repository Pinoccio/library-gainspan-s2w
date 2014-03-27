#include <GS.h>
#include <SPI.h>

GSModule gs;
GSUdpClient client(gs);

#define SSID "Foo"
#define PASSPHRASE "Bar"

static void print_line(const uint8_t *buf, uint16_t len, void *data) {
  static_cast<Print*>(data)->write(buf, len);
  static_cast<Print*>(data)->println();
}


void setup() {
  Serial.begin(115200);
  Serial.println("Gainspan UDP Client demo");
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
  gs.writeCommand("AT+NSTAT=?");
  gs.readResponse(print_line, &Serial);

  // clock-1.cs.cmu.edu, daytime port
  if (!client.connect(IPAddress(128, 2, 201, 216), 13))
    Serial.println("Connect failed");

  gs.writeCommandCheckOk("AT");
  Serial.println("setup() done");
}

void loop() {
  static uint32_t last = 0;
  gs.loop();

  while(client.available())
    Serial.write(client.read());

  if ((uint32_t)(millis() - last) > 2000) {
    // Send a dummy packet to solicit a reply
    client.write("X");
    last = millis();
  }
}

/* vim: set filetype=cpp softtabstop=2 shiftwidth=2 expandtab: */

