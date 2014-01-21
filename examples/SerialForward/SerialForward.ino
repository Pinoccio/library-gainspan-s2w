/*
 * Arduino library for Gainspan Wifi2Serial modules
 *
 * Copyright (C) 2014 Matthijs Kooijman <matthijs@stdin.nl>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * This example does not actually use the library, it just connects to
 * the gainspan module through SPI or UART and forwards all data to the
 * primary serial port. This allows interactive use of the module, or
 * for example programming its firmware through the gs_flashprogram tool.
 */


// Note: Programming of the Flash with gs_flashprogram only seems to
// work through UART and needs ESCAPE_NON_PRINT disabled
//#define USE_SPI
//#define ESCAPE_NON_PRINT

// SPI Slave Select pin to use
#define SPI_SS_PIN 7
// UART to use
#define UART Serial1

#ifdef USE_SPI
#include <SPI.h>
#endif

void display_byte(uint8_t c)
{
#ifdef ESCAPE_NON_PRINT
  if (!isprint(c) && !isspace(c)) {
    Serial.write('\\');
    if (c < 0x10) Serial.write('0');
    Serial.print(c, HEX);
    return;
  }
#endif
  Serial.write(c);
}
void setup() {
  Serial.begin(115200);
#ifdef USE_SPI
  // Max 3.5Mhz
  SPI.setClockDivider(SPI_CLOCK_DIV8);
  SPI.begin();
  pinMode(SPI_SS_PIN, OUTPUT);
  digitalWrite(SPI_SS_PIN, HIGH);
#else
  UART.begin(115200);
#endif
  // uncomment these if you want to put the WiFi module into firmware update mode
  //  pinMode(6, OUTPUT);
  //  digitalWrite(6, HIGH);

  pinMode(VCC_ENABLE, OUTPUT);
  digitalWrite(VCC_ENABLE, LOW);
  delay(1000);
  digitalWrite(VCC_ENABLE, HIGH);
  delay(1000);
}

#ifdef USE_SPI

#define  GS_SPI_ESC_CHAR               (0xFB)    /* Start transmission indication */
#define  GS_SPI_IDLE_CHAR              (0xF5)    /* synchronous IDLE */
#define  GS_SPI_XOFF_CHAR              (0xFA)    /* Stop transmission indication */
#define  GS_SPI_XON_CHAR               (0xFD)    /* Start transmission indication */      
#define  GS_SPI_INVALID_DATA_ALL_ONE   (0xFF)    /* Invalid character possibly recieved during reboot */
#define  GS_SPI_INVALID_DATA_ALL_ZERO  (0x00)    /* Invalid character possibly recieved during reboot */
#define  GS_SPI_READY_ACK              (0xF3)    /* SPI link ready */
#define  GS_SPI_ESC_XOR                (0x20)    /* Xor mask for escaped data bytes */

uint8_t send_receive(uint8_t c) {
  digitalWrite(SPI_SS_PIN, LOW);
  c = SPI.transfer(c);
  digitalWrite(SPI_SS_PIN, HIGH);
  return c;
}

// Send a byte as-is, don't do any additional stuffing
void send_byte(uint8_t c) {
  uint8_t in = send_receive(c);
  switch (in) {
   case GS_SPI_XON_CHAR:
    case GS_SPI_XOFF_CHAR:
    case GS_SPI_IDLE_CHAR:
    case GS_SPI_INVALID_DATA_ALL_ONE:
   case GS_SPI_INVALID_DATA_ALL_ZERO:
    case GS_SPI_READY_ACK:
      // Ignore these for now...
      break;
    case GS_SPI_ESC_CHAR:
      in = send_receive(GS_SPI_IDLE_CHAR) ^ GS_SPI_ESC_XOR;
      // fallthrough
    default:
      display_byte(in);
  }
}

// Send the given byte, adding stuffing if needed
void send_and_stuff_byte(uint8_t c) {
  if( (GS_SPI_ESC_CHAR  == c) ||
      (GS_SPI_XON_CHAR  == c) ||
      (GS_SPI_XOFF_CHAR == c) ||
      (GS_SPI_IDLE_CHAR == c) ||
      (GS_SPI_INVALID_DATA_ALL_ONE == c) ||
      (GS_SPI_INVALID_DATA_ALL_ZERO == c)||
      (GS_SPI_READY_ACK == c) ) {
          send_byte(GS_SPI_ESC_CHAR);
          send_byte(c ^ GS_SPI_ESC_XOR);
      } else {
          send_byte(c);
      }
}

void loop() {
  int out = Serial.read();
  if (out == -1)
    send_byte(GS_SPI_IDLE_CHAR);
  else
    send_and_stuff_byte(out);
}

#else // !SPI
void loop() {
  // read from port 1, send to port 0:
  if (UART.available()) {
    display_byte(UART.read());
  }

  // read from port 0, send to port 1:
  if (Serial.available()) {
    UART.write(Serial.read());
  }
}
#endif

/* vim: set filetype=cpp softtabstop=2 shiftwidth=2 expandtab: */
