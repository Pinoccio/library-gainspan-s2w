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

#include "GSModule.h"

int GSModule::connectTcp(const IPAddress& ip, uint16_t port)
{
  uint8_t buf[16];
  snprintf((char*)buf, sizeof(buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  writeCommand("AT+NCTCP=%s,%d", buf, port);
  cid_t cid = INVALID_CID;
  if (readResponse(&cid) != GS_SUCCESS || cid > MAX_CID)
    return INVALID_CID;

  // TODO: Until https://github.com/arduino/Arduino/pull/1798 is merged,
  // we have to remove the constness here.
  this->connections[cid].remote_ip = const_cast<IPAddress&>(ip);
  this->connections[cid].remote_port = port;
  this->connections[cid].local_port = 0;
  this->connections[cid].error = 0;
  this->connections[cid].connected = 1;

  return cid;
}

bool GSModule::setDhcp(bool enable, const char *hostname)
{
  if (hostname)
    return writeCommandCheckOk("AT+NDHCP=%d,\"%s\"", enable, hostname);
  else
    return writeCommandCheckOk("AT+NDHCP=%d", enable);
}
