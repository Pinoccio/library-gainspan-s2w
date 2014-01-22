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
  bool associate(const char *ssid, const char *bssid = NULL, uint8_t channel = 0, bool best_rssi = true);

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
   * Set the DNS servers to use.
   *
   * These servers are only used when DHCP is disabled.
   *
   * When associated and DHCP is disabled, the new configuration is
   * applied immediately.
   */
  bool setDns(const IPAddress& dns1, const IPAddress& dns2);
  bool setDns(const IPAddress& dns);

  /**
   * Save the current settings (profile) to flash.
   *
   * @param profile The profile number in flash to use. Can be either 0
   *                or 1.
   */
  bool saveProfile(uint8_t profile)
  {
    return writeCommandCheckOk("AT&W%d", profile);
  }

  /**
   * Load settings from the given profile in flash.
   *
   * @param profile The profile number in flash to use. Can be either 0
   *                or 1.
   */
  bool loadProfile(uint8_t profile)
  {
    return writeCommandCheckOk("ATZ%d", profile);
  }

  /**
   * Sets the number of the default profile, i.e. the profile that is
   * automatically loaded from flash on power-on and reset.
   *
   * @param profile The profile number in flash to use. Can be either 0
   *                or 1.
   */
  bool setDefaultProfile(uint8_t profile)
  {
    return writeCommandCheckOk("AT&Y%d", profile);
  }

  /**
   * Perform TLS handshaking. Should be called after a connection is
   * opened, but before any data is sent. After this, all data sent will
   * be encrypted.
   *
   * The certname is the name of a certificate previously set through
   * addCert. The certificate should be a CA certificate. If the server
   * supplies a certificate that is signed by this particular CA, then
   * the TLS handshake succeeds. If the server certificate is not signed
   * by this CA (or is invalid for other reasons, like expiry date), the
   * connection is closed and 0 is returned.
   *
   * Note that no checking of the server certificate's commonName
   * happens! If you pass in a (commercial) CA certificate, _any_
   * certificate issued by that CA will be accepted, not just the ones
   * with a specific hostname inside.
   *
   * Also make sure that the current time is correctly set, otherwise
   * the server certificate will likely be considered expired or not yet
   * valid even when it isn't.
   */
  bool enableTls(cid_t cid, const char *certname);

  /**
   * Save the given certificate to the module's flash or RAM
   * (depending on to_flash). The name can be any string and should be
   * passed to enableTls later. The buffer should contain the ca
   * certificate in (binary) DER format. */
  bool addCert(const char *certname, bool to_flash, const uint8_t *buf, uint16_t len);

  /**
   * Remove the certificate with the given name from either the module's
   * flash or RAM (depending on where it is).
   */
  bool delCert(const char *certname)
  {
    return writeCommandCheckOk("AT+TCERTDEL=%s", certname);
  }

  /**
   * Do an SNTP timesync to an NTP server.
   * A one-shot sync is performed immediately and, if interval is
   * non-zero, more syncs are performed every interval seconds.
   *
   * @param server    The address of an NTP server to use.
   * @param interval  The number of seconds before doing another time
   *                  sync (or 0 for only a one-off timesync).
   * @param timeout   The number of seconds to wait for the server's response.
   *
   * @returns true when the time sync was succesful, false otherwise.
   */
  bool timeSync(const IPAddress&, uint32_t interval = 0, uint8_t timeout = 10);

  /**
   * Perform a DNS lookup.
   *
   * @param host     The hostname to look up.
   * @returns The IP address for the given host. If the host was not
   *          found, returns 0.0.0.0.
   */
  IPAddress dnsLookup(const char *name);

  /**
   * Setup a new TCP connection to the given ip and port.
   *
   * @returns the cid of the new connection if succesful, INVALID_CID
   * otherwise.
   */
  int connectTcp(const IPAddress& ip, uint16_t port);

  /**
   * Disconnect a connection.
   *
   * @param cid The cid to read data for. Can be an invalid cid, will
   *            return false then.
   */
  bool disconnect(cid_t cid);

/*******************************************************
 * Network connection manager
 *******************************************************/

  enum WMode {
    GS_INFRASTRUCTURE = 0,
    GS_ADHOC = 1,
    GS_LIMITED_AP = 2,
  };

  /**
   * Set up automatic association parameters. These are used by the
   * network connection manager and auto connect mode (transparent
   * passtrhough).
   *
   * This command just sets the info, it does not enable either
   * automatic mode itself.
   *
   * @param ssid      The SSID to connect to
   * @param bssid     The BSSID (MAC address) of the access point. Should be
   *                  a string of the form "12:34:56:78:9a:bc" or NULL
   *                  to connect to any BSSID.
   * @param channel   Only connect to access points on this channel.
   *                  Channel 0 means "any channel".
   * @param mode      The wireless network mode to use
   */
  bool setAutoAssociate(const char *ssid, const char *bssid = NULL, int channel = 0, WMode mode = GS_INFRASTRUCTURE)
  {
    return writeCommandCheckOk("AT+WAUTO=%d,\"%s\",%s,%d", mode, ssid, bssid ?: "", channel);
  }

  enum Protocol {
    GS_UDP = 0,
    GS_TCP = 1,
  };

  /**
   * Set up automatic connection parameters. These are used by the
   * network connection manager and auto connect mode (transparent
   * passtrhough) to set up a TCP or UDP client connection after
   * association is successful.
   *
   * This command just sets the info, it does not enable either
   * automatic mode itself.
   *
   * @param ip        The remote ip address to connect to.
   * @param port      The remote port to connect on.
   * @param protocol  Wether to use TCP or UDP
   */
  bool setAutoConnectClient(const IPAddress &ip, uint16_t port, Protocol protocol = GS_TCP);

  /**
   * Set up automatic connection parameters, but use a hostname (or an
   * ip address in string form) instead of an IPAddress. The connection
   * manager will take care of doing the DNS lookup (needs firmware
   * 2.5.1 or above).
   */
  bool setAutoConnectClient(const char *name, uint16_t port, Protocol protocol = GS_TCP);

  /**
   * Similar to setAutoConnectClient, but sets up a server connection
   * instead.
   *
   * @param port      The local port to listen on
   * @param protocol  Wether to use TCP or UDP
   */
  bool setAutoConnectServer(uint16_t port, Protocol protocol = GS_TCP);

  enum NCMMode {
    GS_NCM_STATION = 0,
    GS_NCM_LIMITED_AP = 1,
  };

  /**
   * Enable or disable the network connection manager.
   *
   * Before starting the NCM, be sure to configure other regular
   * settings like DHCP mode and WPA passphrase as well as the
   * various setAuto* parameters.
   *
   * Note that the connection manager only retries the authorization and
   * connection a limited number of times. For autoconnection, this
   * limit can be configured to 0 which _might_ mean infinite, but the
   * documentation is not clear on this (perhaps it'll mean 65536
   * instead). If the retry count is reached, the NCM stops trying to
   * setup the TCP/UDP connection, but it restarts on the next
   * (re)association.
   *
   * For the association retry count, the documentation says 0 is not
   * supported (but who knows...).
   *
   * @param enabled         Wether the the connection manager should be
   *                        started or stopped.
   * @param associate_only  When true, just associate. When false, also
   *                        set up a network connection using the info
   *                        set through setAutConnectClient or
   *                        setAutoConnectServer.
   * @param remember        When true, save these settings the current
   *                        profile, so the connection manager can be
   *                        autostarted on reset or power-on.
   *                        Note that this only works if the current
   *                        profile is actually saved to the (default)
   *                        stored profile after this command.
   *                        Also note that these settings are not
   *                        displayed in AT&V, but really are part of
   *                        the current/stored profiles.
   * @param mode            Wether to use station or limited ap mode.
   *                        This should probably match the value set
   *                        through setAutoAssociate.
   */
  bool setNcm(bool enabled, bool associate_only = true, bool remember = false, NCMMode mode = GS_NCM_STATION)
  {
    return writeCommandCheckOk("AT+NCMAUTO=%d,%d,%d,%d", mode, enabled, !associate_only, !remember);
  }
};

#endif // GS_MODULE_H

// vim: set sw=2 sts=2 expandtab:
