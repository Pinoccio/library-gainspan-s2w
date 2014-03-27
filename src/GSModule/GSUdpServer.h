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
 */

#ifndef _GS_UDP_SERVER_H
#define _GS_UDP_SERVER_H

#include <Arduino.h>
#include <Udp.h>

#include "GSModule.h"

class GSUdpServer : public UDP {
  public:
    GSUdpServer(GSModule &gs) : gs(gs), cid(GSModule::INVALID_CID) { } ;

    /****************************************************************
     * Stuff from Udp / Stream / Print
     ****************************************************************/

    // udp specific calls based on http://arduino.cc/en/Tutorial/UDPSendReceiveString
    virtual uint8_t begin(uint16_t port);
    virtual int parsePacket(); // returns size, populates remoteX
    virtual IPAddress remoteIP();
    virtual uint16_t remotePort();
    virtual int beginPacket(IPAddress ip, uint16_t port);
    virtual int beginPacket(const char *host, uint16_t port);
    virtual int endPacket();
    virtual void stop();

    virtual size_t write(uint8_t);
    virtual size_t write(const uint8_t *buf, size_t size);
    virtual int available();
    virtual int read();
    virtual int read(char *buf, size_t size) { return read((unsigned char*)buf, size); };
    virtual int read(unsigned char *buf, size_t size);
    virtual int peek();
    virtual void flush();

    // Include other overloads of write
    using Print::write;

  protected:
    GSModule &gs;
    GSModule::cid_t cid = GSModule::INVALID_CID;
    // Packet currently being received. When length is 0, the other
    // fields might be invalid.
    GSCore::RXFrame rx_frame = {0};

    // IP and port of the packet being prepared for sending (if any)
    IPAddress tx_ip = INADDR_NONE;
    uint16_t tx_port = 0;
    // Buffer into which we're accumulating the next packet.
    uint8_t *tx_buf = NULL;
    // Length of data in sendBuf
    size_t tx_len = 0;

};

#endif // _GS_UDP_SERVER_H

// vim: set sw=2 sts=2 expandtab:
