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

#ifndef GS_CORE_H
#define GS_CORE_H

#include <stdint.h>
#include <Stream.h>

// TODO: Decide on buffer sizes needed
#define BUF_ASYNC_SIZE 128
#define BUF_DATA_SIZE 128

// Output debugging info on error conditions
#define GS_LOG_ERRORS

// Dump full lines of I/O
//#define GS_DUMP_LINES

// Dump individual hex bytes
//#define GS_DUMP_BYTES

#ifndef lengthof
#define lengthof(x) (sizeof(x) / sizeof(*x))
#endif

/**
 * This class allows talking to a Gainspan Serial2Wifi module. It's
 * intended for the GS1011MIPS module, but might also work with other
 * variants.
 *
 * This class contains the core code for communicating with the module.
 * You'll likely want to use GSModule instead, which adds some higher
 * level method for sending commands
 */
class GSCore {
public:
  /* Type to use for a (parsed) cid. Intended to make the distinction
   * between a raw cid as read from the module ('0' to 'f') and a parsed
   * cid (0 to 15) more clear. */
  typedef uint8_t cid_t;

  /** Guaranteed to never match a valid CID */
  static const uint8_t INVALID_CID = 0xff;

  /** Accepted by some functions to return data for any cid */
  static const uint8_t ANY_CID = 0xfe;

  /** Biggest valid CID */
  static const uint8_t MAX_CID = 0xf;

/*******************************************************
 * Methods for setting up the module
 *******************************************************/

  GSCore();

  /**
   * Set up this library to talk over a UART specified by the given
   * stream.
   */
  bool begin(Stream &serial);

  /**
   * Set up this library to talk over SPI, using the given SPI slave
   * select pin. Sets the given pin to output mode.
   */
  bool begin(uint8_t ss);

  /**
   * Clean up this library (for example to switch from UART to SPI).
   */
  void end();

/*******************************************************
 * Methods for reading and writing data
 *******************************************************/

  /**
   * Read a single byte of data for the given cid, without removing it
   * from the buffer.
   *
   * @param cid The cid to read data for. Can be an invalid cid, will
   *            return -1 then.
   *
   * @see the notes for readData(cid_t), which also apply here.
   */
  int peekData(cid_t cid);

  /**
   * Read a single byte of data for the given cid.
   *
   * Be careful when polling this method for data. If data is available
   * for another cid, then calling readData will _never_ return a data
   * byte, not until you read all the data for that other cid (no matter
   * how long you wait).
   *
   * @param cid The cid to read data for. Can be an invalid cid, will
   *            return -1 then.
   *
   * @returns the data byte, or -1 if no data is available.
   */
  int readData(cid_t cid);

  /**
   * Read up to size bytes of data.
   *
   * @param cid    The cid to read data for. Can be an invalid cid, will
   *               return -1 then.
   * @param buf    The buffer into which the data will be stored.
   * @param size   The number of bytes available in the buffer.
   *
   * @returns the number of bytes written to the buffer.
   *
   * @see the notes for readData(cid_t), which also apply here.
   */
  size_t readData(cid_t cid, uint8_t *buf, size_t size);

  /**
   * Read a single byte of data, for any cid.
   *
   * @param   cid   If the return value is not -1, the cid for which
   *                data was returned is returned through this pointer.
   *
   * @returns the data byte, or -1 if no data is available.
   */
  int readData(cid_t *cid);

  /**
   * @returns the cid for which data can be read, or INVALID_CID if no
   * data is currently avaiable.
   */
  cid_t firstCidWithData();

  /**
   * Return the number of bytes that can be read without blocking.
   *
   * @param cid The cid to get available data for. Can be an invalid
   *            cid, will return 0 then.
   */
  uint16_t availableData(cid_t cid);

  /**
   * Write connection data for the given cid.
   *
   * @returns wether the data could be succesfully written.
   */
  bool writeData(cid_t cid, const uint8_t *buf, uint8_t len);

/*******************************************************
 * Methods for getting connection info
 *******************************************************/

  struct ConnectionInfo {
    /** Is this connection currently open? */
    bool connected : 1;
    /**
     * When true, an error has occurred and data was likely lost (e.g., buffer
     * overflow or connection error). The connection might still be
     * open, but it is probably best to close it and try again.
     */
    bool error : 1;
    /** Remote IP. 0 means unknown */
    uint32_t remote_ip;
    /** Local port number. 0 means unknown. */
    uint16_t local_port;
    /** Remote port number. 0 means unknown. */
    uint16_t remote_port;
  };

  /**
   * Return information about the given cid.
   * Only valid cids should be passed.
   */
  const ConnectionInfo& getConnectionInfo(cid_t cid) {
    return this->connections[cid];
  }

/*******************************************************
 * Methods for writing commands / reading replies
 *******************************************************/
  enum GSResponse {
    // These are actual codes returned by the module. Their value
    // corresponds to the value sent by the module when verbose mode is
    // off, the comments list the corresponding reply in verbose mode.
    GS_SUCCESS = 0, // "\r\nOK\r\n"
    GS_FAILURE = 1, // "\r\nERROR\r\n"
    GS_EINVAL = 2, // "\r\nERROR: INVALID INPUT\r\n"
    GS_SOCK_FAIL = 3, // "\r\nERROR: SOCKET FAILURE <CID>\r\n"
    GS_ENOCID = 4, // "\r\nERROR: NO CID\r\n"
    GS_EBADCID = 5, // "\r\nERROR: INVALID CID\r\n"
    GS_ENOTSUP = 6, //"\r\nERROR: NOT SUPPORTED\r\n"
    GS_CON_SUCCESS = 7, // "\r\nCONNECT <CID>\r\n\r\nOK\r\n”
                        // or async "\r\nCONNECT <server CID> <new CID> <ip> <port>\r\n"
    GS_ECIDCLOSE = 8, // "\r\nDISCONNECT <CID>\r\n"
    GS_LINK_LOST = 9, // "\r\nDISASSOCIATED\r\n"
    GS_DISASSO_EVT = 10, // “\r\n\r\nDisassociation Event\r\n\r\n”
    GS_STBY_TMR_EVT = 11, // "\r\nOut of StandBy-Timer\r\n"
    GS_STBY_ALM_EVT = 12, // "\r\n\n\rOut of StandBy-Alarm\r\n\r\n"
    GS_DPSLEEP_EVT = 13, // "\r\n\r\nOut of Deep Sleep\r\n\r\n\r\nOK\r\n"
    GS_BOOT_UNEXPEC = 14, // "\r\n\r\nUnExpected Warm Boot(Possibly Low Battery)\r\n\r\n"
    GS_ENOIP = 15, // "\r\nERROR: IP CONFIG FAIL\r\n"
    GS_BOOT_INTERNAL = 16, // "\r\nSerial2WiFi APP\r\n"
    GS_BOOT_EXTERNAL = 17, // "\r\nSerial2WiFi APP-Ext.PA\r\n"
    GS_NWCONN_SUCCESS = 18, // "\r\nNWCONN-SUCCESS\r\n"

    // These are reponse sent in reply to data escape sequences. Their
    // values have no significant meaning
    GS_DATA_SUCCESS, // "<ESC>O"
    GS_DATA_FAILURE, // "<ESC>F"

    // These codes are never emitted by the hardware, but used in the
    // code to comunicate between different parts of the code.
    GS_NO_RESPONSE,
    GS_ASYNC_HANDLED,
    GS_UNKNOWN_RESPONSE,
  };

  /**
   * Send a command to the module. Accepts a format string and arguments
   * like printf. The string is sent as-is, so it should contain the
   * trailing \r\n already.
   */
  void writeCommand(const char *fmt, ...);
  void writeCommand(const char *fmt, va_list args);

  /**
   * Send a command to the module and reads a reply.
   *
   * Accepts a format string and arguments like printf. The string is
   * sent as-is, so it should contain the trailing \r\n already.
   *
   * @returns true when an OK response was received, false in all other
   *          cases.
   */
  bool writeCommandCheckOk(const char *fmt, ...);

  /**
   * Read a single reply from the module.
   *
   * This reads response lines from the module, until a line is found
   * that terminates the response (e.g., "OK", "ERROR", etc.).
   * The final line determines the return value. Any data read before
   * the final response line is returned in the buffer (with normalized
   * newlines).
   *
   * @param buf            A buffer to store the received data in.
   * @param len            The pointer to the length of the buffer. Will
   *                       be set to the number of bytes written to buf.
   * @param connect_cid    if passed, then a "CONNECT <CID>" reply is also
   *                       handled and *connect_cid get set to the
   *                       numerical cid sent by the module.
   */
  GSResponse readResponse(uint8_t *buf, uint16_t *len, cid_t *connect_cid = NULL);

  /**
   * Read a single reply from the module, discarding any extra data
   * read.
   *
   * @param connect_cid    if passed, then a "CONNECT <CID>" reply is also
   *                       handled and *connect_cid get set to the
   *                       numerical cid sent by the module.
   */
  GSResponse readResponse(cid_t *connect_id = NULL);

  /**
   * Write a raw sequence of bytes.
   *
   * You should not normally use this method, instead use either
   * writeCommand() or writeData().
   */
  void writeRaw(const uint8_t *buf, uint8_t len);

  /**
   * Reads a single byte from the module, or returns -1 when no byte is available.
   *
   * You should not normally use this method, instead use either
   * readResponse() or readData().
   */
  int readRaw();

/*******************************************************
 * Internal helper methods
 *******************************************************/
protected:
  enum RXState {
    /** Default state: expecting (more of) an async response */
    GS_RX_RESPONSE,
    /** Read an escape char, waiting for the type */
    GS_RX_ESC,
    /** Read an <esc>Z escape code, waiting for the rest of the sequence */
    GS_RX_ESC_Z,
    /** Read <esc>Z<cid>, waiting for byte 0 of the length */
    GS_RX_ESC_Z_LEN0,
    GS_RX_ESC_Z_LEN1,
    GS_RX_ESC_Z_LEN2,
    GS_RX_ESC_Z_LEN3,
    /** Reading bulk data */
    GS_RX_BULK,
  };

  struct RXFrame {
    cid_t cid;
    uint16_t length;
  };

  /**
   * Setup function common for UART and SPI modes.
   */
  bool _begin();

  /**
   * Processes special characters in the given byte, as received through
   * SPI. Returns the original byte, or -1 when there is none.
   */
  int processSpiSpecial(uint8_t c);

  /**
   * Processes an incoming byte read from the module.
   *
   * @return When this byte completes a response, its type is returned
   * (except for async responses, which return GS_ASYNC_HANDLED). When
   * this byte does not complete a response, GS_NO_RESPONSE is returned.
   *
   * @param c              The byte to process. If it is -1, does
   *                       nothing and retrusn GS_NO_RESPONSE.
   * @param connect_cid    if passed, then a "CONNECT <CID>" reply is also
   *                       handled and *connect_cid get set to the
   *                       numerical cid sent by the module.
   */
  GSResponse processIncoming(int c, cid_t *connect_id = NULL);

  /**
   * Identical to processIncoming, except that (when GS_LOG_ERRORS is
   * defined), it shows an error when a non-async response is received.
   */
  GSResponse processIncomingAsyncOnly(int c);

  /**
   * Put an incoming data byte into rx_data.
   */
  void bufferIncomingData(uint8_t c);

  /**
   * Puts a frame header into rx_data.
   */
  void bufferFrameHeader(const RXFrame *frame);

  /**
   * Loads a frame header from rx_data. Should not be called when
   * rx_data is empty.
   */
  void loadFrameHeader(RXFrame *frame);

  /**
   * Get the next data frame into tail_frame, without blocking. The
   * frame is loaded either from rx_data or by querying the module.
   *
   * @param cid    The cid the caller is interested in.
   *
   * @returns true if a frame was available and it contains data for the
   * given cid (or cid is CID_ANY), or false when no frame is available
   * or it is for the wrong cid.
   */
  bool getFrameHeader(cid_t cid);

  /**
   * Get the next data byte, without blocking. The frame is loaded
   * either from rx_data or by querying the module.
   *
   * @returns the data byte, or -1 when no data is available.
   */
  int getData();

  /**
   * Drop a byte from the tail of rx_data, to make room for incoming
   * data and mark the affected cid as broken.
   */
  void dropData(uint8_t num_bytes);

  /**
   * Look at the given response line and find out what kind of reponse
   * it is.
   *  - If the response is an asynchronous event, it is handled and
   *    GS_ASYNC_HANDLED is returned.
   *  - If the response does not look like a known response,
   *    GS_UNKNOWN_RESPONSE is returned.
   *  - Otherwise, the corresponding constant for the response returned.
   *
   * The connect_cid parameter will be used to store the CID from a
   * GS_CON_SUCCESS response. It will be modified if and only if the
   * return value is GS_CON_SUCCESS.
   */
  GSResponse processResponseLine(const uint8_t *buf, uint8_t len, cid_t *connect_cid);

/*******************************************************
 * Static helper methods
 *******************************************************/

  /**
   * Parses a CID from a character ('0' to '9' and 'a' to 'f') to the
   * equivalent integer (0 to 15).
   *
   * When the character given is not valid, returns INVALID_CID.
   */
  static cid_t parseCid(uint8_t c);

/*******************************************************
 * Instance variables
 *******************************************************/

  /** The serial port to use, in serial mode */
  Stream *serial;
  /** The SPI slave select pin to use, in SPI mode */
  uint8_t ss;
  /** When true, the module has sent xoff */
  bool spi_xoff;
  /** When true, the previous SPI byte was an escape character */
  bool spi_prev_was_esc;

  /**
   * Buffer for an (incomplete) asynchronous response, received while no
   * command is pending. Always contains at most 1 line of data,
   * excluding all newline characters. Once the trailing newline is
   * received, this response should be processed and cleared.
   */
  uint8_t rx_async[BUF_ASYNC_SIZE];
  /** Number of bytes in rx_async */
  uint8_t rx_async_len;

  /**
   * Ringbuffer for connection data, received while processing a command
   * (e.g., when we can't return this connection data to the
   * application).
   */
  uint8_t rx_data[BUF_DATA_SIZE];
  typedef uint8_t rx_data_index_t;

  /** Current state for the data stream read from the module */
  RXState rx_state;

  /** The offset into rx_data where the next byte should be written to. */
  rx_data_index_t rx_data_head;
  /** The offset into rx_data where the next byte should be read from. */
  rx_data_index_t rx_data_tail;

  /**
   * Data for the next frame to be put into the data buffer.
   * Length indicates the number of data bytes left to read from the
   * module.
   */
  RXFrame head_frame;

  /**
   * Data for the frame currently being read from the data buffer.
   * Length indicates the number of bytes left to read from the data
   * buffer.
   */
  RXFrame tail_frame;

  ConnectionInfo connections[MAX_CID + 1];

  #if __cplusplus >= 201103L
  static_assert( (1L << (sizeof(rx_async_len) * 8)) >= sizeof(rx_async), "rx_async_len is too small for rx_async" );
  static_assert( (1L << (sizeof(rx_data_index_t) * 8)) >= sizeof(rx_data), "rx_data_index_t is too small for rx_data" );
  // This is needed to guarantee proper negative wraparound. Also, it
  // guarantees that sizeof(rx_data) is a power-of-two, which makes all
  // modulo operations efficient bitwise ands.
  static_assert( (1L << (sizeof(rx_data_index_t) * 8)) % sizeof(rx_data) == 0, "rx_data size is not a divisor of the rx_data_index_t wraparound value (== not a power of two)" );
  #endif

  /** Value for the ss attribute when SPI is not enabled. */
  static const uint8_t SPI_DISABLED = 0xff;
  /** This byte is sent when there is no real data */
  static const uint8_t SPI_SPECIAL_IDLE = 0xf5;
  /** Indicates the buffer is full and no further data should be sent */
  static const uint8_t SPI_SPECIAL_XOFF = 0xfa;
  /** Indicates the buffer has room again */
  static const uint8_t SPI_SPECIAL_XON = 0xfd;
  /** This value is never sent (unescaped) to detect broken connection */
  static const uint8_t SPI_SPECIAL_ALL_ONE = 0xff;
  /** This value is never sent (unescaped) to detect broken connection */
  static const uint8_t SPI_SPECIAL_ALL_ZERO = 0x00;
  /** "Link ready indication", unclear what it means */
  static const uint8_t SPI_SPECIAL_ACK = 0xf3;
  /** Byte to escape special bytes during SPI */
  static const uint8_t SPI_SPECIAL_ESC = 0xfb;
  /** Escaped bytes are xored with this value */
  static const uint8_t SPI_ESC_XOR = 0x20;

};

#endif // GS_CORE_H

// vim: set sw=2 sts=2 expandtab:
