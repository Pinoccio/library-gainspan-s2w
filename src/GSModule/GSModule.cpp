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
#include "util.h"

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
  processConnect(cid, const_cast<IPAddress&>(ip), port, 0, false);

  return cid;
}

bool GSModule::associate(const char *ssid, const char *bssid, uint8_t channel, bool best_rssi)
{
  bool ok = writeCommandCheckOk("AT+WA=\"%s\",%s,%d,%d", ssid, bssid ?: "", channel, best_rssi);
  if (ok)
    processAssociation();
  return ok;
}

bool GSModule::setDhcp(bool enable, const char *hostname)
{
  if (hostname)
    return writeCommandCheckOk("AT+NDHCP=%d,\"%s\"", enable, hostname);
  else
    return writeCommandCheckOk("AT+NDHCP=%d", enable);
}

bool GSModule::setStaticIp(const IPAddress& ip, const IPAddress& netmask, const IPAddress& gateway)
{
  uint8_t ip_buf[16], nm_buf[16], gw_buf[16];
  snprintf((char*)ip_buf, sizeof(ip_buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  snprintf((char*)nm_buf, sizeof(nm_buf), "%d.%d.%d.%d", netmask[0], netmask[1], netmask[2], netmask[3]);
  snprintf((char*)gw_buf, sizeof(gw_buf), "%d.%d.%d.%d", gateway[0], gateway[1], gateway[2], gateway[3]);
  return writeCommandCheckOk("AT+NSET=%s,%s,%s", ip_buf, nm_buf, gw_buf);
}

bool GSModule::setDns(const IPAddress& dns1, const IPAddress& dns2)
{
  uint8_t buf1[16], buf2[16];
  snprintf((char*)buf1, sizeof(buf1), "%d.%d.%d.%d", dns1[0], dns1[1], dns1[2], dns1[3]);
  snprintf((char*)buf2, sizeof(buf2), "%d.%d.%d.%d", dns2[0], dns2[1], dns2[2], dns2[3]);

  return writeCommandCheckOk("AT+DNSSET=%s,%s", buf1, buf2);
}

bool GSModule::setDns(const IPAddress& dns)
{
  uint8_t buf[16];
  snprintf((char*)buf, sizeof(buf), "%d.%d.%d.%d", dns[0], dns[1], dns[2], dns[3]);

  return writeCommandCheckOk("AT+DNSSET=%s", buf);
}

bool GSModule::disconnect(cid_t cid)
{
  if (cid > MAX_CID)
    return false;
  return writeCommandCheckOk("AT+NCLOSE=%x", cid);
}

bool GSModule::timeSync(const IPAddress& server, uint32_t interval, uint8_t timeout)
{
  uint8_t buf[16];
  snprintf((char*)buf, sizeof(buf), "%d.%d.%d.%d", server[0], server[1], server[2], server[3]);

  // First, send the command without an interval, to force a sync now
  if (!writeCommandCheckOk("AT+NTIMESYNC=1,%s,%d,0", buf, timeout))
    return false;

  if (interval) {
    // Then, schedule periodic syncs if requested
    if (!writeCommandCheckOk("AT+NTIMESYNC=1,%s,%d,1,%d", buf, timeout, interval))
      return false;
  }
  return true;
}

static void parse_ip_response(const uint8_t *buf, uint16_t len, void *data)
{
  if (len < 3 || strncmp((const char*)buf, "IP:", 3) != 0)
    return;
  IPAddress *ip = (IPAddress*)data;
  if (!GSCore::parseIpAddress(ip, (const char *)buf + 3, len - 3))
    *ip = INADDR_NONE;
}

IPAddress GSModule::dnsLookup(const char *name)
{
  IPAddress result = INADDR_NONE;
  writeCommand("AT+DNSLOOKUP=%s", name);
  if (readResponse(parse_ip_response, &result) != GS_SUCCESS)
    result = INADDR_NONE;
  return result;
}

bool GSModule::enableTls(cid_t cid, const char *certname)
{
  if (cid > MAX_CID)
    return false;

  if (writeCommandCheckOk("AT+SSLOPEN=%x,%s", cid, certname)) {
    // TODO: Keep track of SSL status?
    return true;
  } else {
    this->connections[cid].error = true;
    processDisconnect(cid);
    return false;
  }
}

bool GSModule::addCert(const char *certname, bool to_flash, const uint8_t *buf, uint16_t len) {
  if (!writeCommandCheckOk("AT+TCERTADD=%s,0,%d,%d", certname, len, !to_flash))
    return false;

  const uint8_t escape[] = {0x1b, 'W'};
  writeRaw(escape, sizeof(escape));
  writeRaw(buf, len);
  return readResponse() == GS_SUCCESS;
}

bool GSModule::setAutoConnectClient(const IPAddress &ip, uint16_t port, Protocol protocol)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

  return setAutoConnectClient(buf, port, protocol);
}

bool GSModule::setAutoConnectClient(const char *host, uint16_t port, Protocol protocol)
{
  return writeCommandCheckOk("AT+NAUTO=0,%d,%s,%d", protocol, host, port);
}

bool GSModule::setAutoConnectServer(uint16_t port, Protocol protocol)
{
  return writeCommandCheckOk("AT+NAUTO=1,%d,,%d", protocol, port);
}

// vim: set sw=2 sts=2 expandtab:
