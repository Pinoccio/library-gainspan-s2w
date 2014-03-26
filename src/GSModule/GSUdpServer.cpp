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

#include "GSUdpServer.h"
#include "util.h"

uint8_t GSUdpServer::begin(uint16_t port)
{
  GSModule::cid_t cid = this->gs.listenUdp(port);
  if (cid == GSModule::INVALID_CID)
    return false;

  this->cid = cid;
  return true;
}

int GSUdpServer::parsePacket()
{
  // If there are still bytes pending from the previous packet, drop
  // them. If not all of them are directly available, don't block but
  // instead leave the rest and return no valid packet yet, our caller
  // will probably retry with another parsePacket call which will
  // continue dropping bytes.
  while(this->rx_frame.length) {
    if (this->gs.readData(this->cid) < 0)
      return 0;
    --this->rx_frame.length;
  }

  this->rx_frame = this->gs.getFrameHeader(this->cid);
  return this->rx_frame.length;
}

IPAddress GSUdpServer::remoteIP()
{
  return this->rx_frame.ip;
}

uint16_t GSUdpServer::remotePort()
{
  return this->rx_frame.ip;
}

int GSUdpServer::beginPacket(IPAddress ip, uint16_t port)
{
  this->tx_ip = ip;
  this->tx_port = port;

  return true;
}

int GSUdpServer::beginPacket(const char *host, uint16_t port)
{
  // TODO
  return false;
}

int GSUdpServer::endPacket()
{
  return this->gs.writeData(this->cid, this->tx_ip, this->tx_port, this->tx_buf, this->tx_len);
}

size_t GSUdpServer::write(uint8_t c)
{
  uint8_t *newbuf = (uint8_t*)realloc(this->tx_buf, this->tx_len + 1);
  if (!newbuf)
    return 0;

  this->tx_buf = newbuf;
  this->tx_buf[this->tx_len++] = c;
  return 1;
}

size_t GSUdpServer::write(const uint8_t *buf, size_t size)
{
  uint8_t *newbuf = (uint8_t*)realloc(this->tx_buf, this->tx_len + size);
  if (!newbuf)
    return 0;

  this->tx_buf = newbuf;
  memcpy(this->tx_buf + this->tx_len, buf, size);
  this->tx_len += size;

  return size;
}

int GSUdpServer::available()
{
  return this->rx_frame.length;
}

int GSUdpServer::read()
{
  if (!this->rx_frame.length)
    return 0;

  int c = gs.readData(this->cid);
  if (c >= 0)
    --this->rx_frame.length;
  return c;
}

int GSUdpServer::read(uint8_t *buf, size_t size)
{
  if (!this->rx_frame.length)
    return 0;

  size_t read = gs.readData(this->cid, buf, size);
  this->rx_frame.length -= read;
  return read;
}

int GSUdpServer::peek()
{
  if (!this->rx_frame.length)
    return 0;

  return gs.peekData(this->cid);
}

void GSUdpServer::flush()
{
  // Nothing todo, we can't write anything to the gainspan module
  // without also ending the packet
}

void GSUdpServer::stop()
{
  gs.disconnect(this->cid);
}

// vim: set sw=2 sts=2 expandtab:
