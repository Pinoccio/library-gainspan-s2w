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

#ifndef _GS_CLIENT_H
#define _GS_CLIENT_H

#include <Arduino.h>
#include <Client.h>

#include "GSModule.h"

class GSClient : public Client {
  public:
    GSClient(GSModule &gs) : gs(gs), cid(GSModule::INVALID_CID) { } ;

    /****************************************************************
     * Stuff from Client / Stream / Print
     ****************************************************************/
    virtual int connect(IPAddress ip, uint16_t port);
    virtual int connect(const char *host, uint16_t port);
    virtual size_t write(uint8_t);
    virtual size_t write(const uint8_t *buf, size_t size);
    virtual int available();
    virtual int read();
    virtual int read(uint8_t *buf, size_t size);
    virtual int peek();
    virtual void flush();
    virtual void stop();
    virtual uint8_t connected();
    virtual operator bool();
    GSClient& operator =(GSCore::cid_t cid);

    // Include other overloads of write
    using Print::write;

    /****************************************************************
     * Gainspan-specific stuff
     ****************************************************************/
    virtual uint8_t sslConnected();
    virtual bool enableTls(const char *certname);

  protected:
    GSModule &gs;
    GSModule::cid_t cid;

};

#endif // _GS_CLIENT_H

// vim: set sw=2 sts=2 expandtab:
