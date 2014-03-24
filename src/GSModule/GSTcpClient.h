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

#ifndef _GS_TCP_CLIENT_H
#define _GS_TCP_CLIENT_H

#include "GSClient.h"

class GSTcpClient : public  GSClient {
  public:
    GSTcpClient(GSModule &gs) : GSClient(gs) { } ;

    /****************************************************************
     * Stuff from Client that is not implemented by GSClient yet
     ****************************************************************/
    virtual int connect(IPAddress ip, uint16_t port);
    virtual int connect(const char *host, uint16_t port);


    /****************************************************************
     * Gainspan-specific stuff
     ****************************************************************/
    virtual uint8_t sslConnected();
    virtual bool enableTls(const char *certname);

    // Explicitely inherit operator=, since the default assignment
    // operator shows it.
    using GSClient::operator=;
};

#endif // _GS_TCP_CLIENT_H
