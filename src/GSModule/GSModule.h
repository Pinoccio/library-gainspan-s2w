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

#ifndef GS_MODULE_H
#define GS_MODULE_H

#include "GSCore.h"
#include <IPAddress.h>

/**
 * This class allows talking to a Gainspan Serial2Wifi module. It's
 * intended for the GS1011MIPS module, but might also work with other
 * variants.
 *
 * This class defines some higher level methods for sending commands,
 * @see GSCore for the begin/end and lower level methods.
 */
class GSModule : public GSCore {
public:
  enum GSAuth {
    GS_AUTH_NONE = 0,
    GS_AUTH_OPEN = 1,
    GS_AUTH_SHARED = 2,
  };

  /**
   * Set the WEP authentication mode. Set to None for WPA.
   */
  bool setAuth(GSAuth auth) { return writeCommandCheckOk("AT+WAUTH=%d", auth); }

  enum GSSecurity {
    GS_SECURITY_AUTO = 0,
    GS_SECURITY_OPEN = 1,
    GS_SECURITY_WEP = 2,
    GS_SECURITY_WPA1_PSK = 4,
    GS_SECURITY_WPA2_PSK = 8,
    GS_SECURITY_WPA1_ENTERPRISE = 16,
    GS_SECURITY_WPA2_ENTERPRISE = 32,
    // TODO: Not sure what this one means exactly...
    GS_SECURITY_WPA2_AES_TKIP = 64,

    // Some convenience constants:
    GS_SECURITY_WPA_PSK = GS_SECURITY_WPA1_PSK | GS_SECURITY_WPA2_PSK,
    GS_SECURITY_WPA_ENTERPRISE = GS_SECURITY_WPA1_ENTERPRISE | GS_SECURITY_WPA2_ENTERPRISE,
  };

  /**
   * Set the security mode.
   *
   * Either pass GS_SECURITY_AUTO to let the hardware autodetect, or
   * pass a bitwise or of one or more of the other values to restrict to
   * those options.
   *
   * TODO: Double quotes and backslashes in the passphrase should be
   * backslash-escaped
   */
  bool setSecurity(GSSecurity sec)
  {
    return writeCommandCheckOk("AT+WSEC=%d", sec);
  }

  /**
   * Set the WPA / WPA2 PSK passhrase to use.
   */
  bool setWpaPassphrase(const char *passphrase)
  {
    return writeCommandCheckOk("AT+WWPA=\"%s\"", passphrase);
  }

  /**
   * Set the WPA / WPA2 PSK passhrase to use and precalculate the PSK.
   * The PSK is always calculated from the SSID and the passphrase and
   * this command allows it to be precalculated. If later connecting to
   * another SSID, a new PSK will be calculated also using this
   * passphrase but the new SSID. That new PSK will replace the
   * precalculated PSK as well.
   *
   * TODO: Double quotes and backslashes in the SSID and passphrase
   * should be backslash-escaped
   */
  bool setPskPassphrase(const char *passphrase, const char *ssid)
  {
    return writeCommandCheckOk("AT+WPAPSK=\"%s\",\"%s\"", ssid, passphrase);
  }

  /**
   * Associate to the given SSID.
   *
   * @param ssid      the SSID to connect to
   * @param bssid     the BSSID (MAC address) of the access point. Should be
   *                  a string of the form "12:34:56:78:9a:bc"
   * @param channel   Only connect to access points on this channel.
   *                  Channel 0 means "any channel".
   * @param best_rssi When multiple possible access points are available,
   *                  use the one with the best rssi, or just use an
   *                  arbitrary one.
   *
   * TODO: Double quotes and backslashes in the passphrase should be
   * backslash-escaped
   */
  bool associate(const char *ssid, const char *bssid = NULL, uint8_t channel = 0, bool best_rssi = true)
  {
    return writeCommandCheckOk("AT+WA=\"%s\",%s,%d,%d", ssid, bssid ?: "", channel, best_rssi);
  }

  /**
   * Set DHCP status and hostname.
   *
   * When executing the command, the following happens:
   * - Any current DHCP lease is forgotten.
   * - When a hostname is given, it saved and used for all future DHCP
   * - The enable status is saved and used for all future associations.
   * - When associated and enable is true, a DHCP request is performed.
   * - When associated and enable is false, the static IP configuration
   *   is applied.
   */
  bool setDhcp(bool enable, const char *hostname = NULL);

  /**
   * Set the static IP configuration.
   *
   * When associated and DHCP is disabled, the new configuration is
   * applied immediately.
   */
  bool setStaticIp(const IPAddress& ip, const IPAddress& netmask, const IPAddress& gateway);

  /**
   * Setup a new TCP connection to the given ip and port.
   *
   * @returns the cid of the new connection if succesful, INVALID_CID
   * otherwise.
   */
  int connectTcp(const IPAddress& ip, uint16_t port);
};

#endif // GS_MODULE_H

// vim: set sw=2 sts=2 expandtab:
