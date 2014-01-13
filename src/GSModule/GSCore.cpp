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

#include <SPI.h>
#include <Arduino.h>
#include "GSCore.h"
#include "util.h"

/*******************************************************
 * Methods for setting up the module
 *******************************************************/

GSCore::GSCore()
{
  this->ss = SPI_DISABLED;

  static_assert( max_for_type(__typeof__(rx_async_len)) >= sizeof(rx_async) - 1, "rx_async_len is too small for rx_async" );
  static_assert( max_for_type(rx_data_index_t) >= sizeof(rx_data) - 1, "rx_data_index_t is too small for rx_data" );
  // Check that the buffer size is a power of two, which makes all
  // modulo operations efficient bitwise ands. Additionally, this also
  // guarantees that the size of rx_data (which is a power of two by
  // definition) is divisible by the buffer size, which is needed to
  // guarantee proper negative wraparound.
  static_assert( is_power_of_two(sizeof(rx_data)), "rx_data size is not a power of two" );
}

bool GSCore::begin(Stream &serial)
{
  if (this->serial || this->ss != SPI_DISABLED)
    return false;

  this->serial = &serial;
  return _begin();
}

bool GSCore::begin(uint8_t ss)
{
  if (this->serial || this->ss != SPI_DISABLED || ss == SPI_DISABLED)
    return false;

  this->ss = ss;

  pinMode(ss, OUTPUT);
  digitalWrite(ss, HIGH);

  return _begin();
}

bool GSCore::_begin()
{
  this->rx_state = GS_RX_RESPONSE;
  this->rx_data_head = this->rx_data_tail = 0;
  this->tail_frame.length = 0;
  this->spi_prev_was_esc = false;
  this->spi_xoff = false;

  // Flush any serial data still buffered
  while(readRaw() != -1) /* nothing */;

  // TODO: Do a reset?

  // Always start with disabling verbose mode, otherwise we won't be
  // able to interpret responses
  if (!writeCommandCheckOk("ATV0"))
    return false;

  // Disable echo mode
  if (!writeCommandCheckOk("ATE0"))
    return false;

  // Enable bulk mode
  if (!writeCommandCheckOk("AT+BDATA=1"))
    return false;

  memset(this->connections, 0, sizeof(connections));

  return true;
}


void GSCore::end()
{
  this->serial = NULL;
  if (this->ss != SPI_DISABLED)
    pinMode(this->ss, INPUT);
  this->ss = SPI_DISABLED;
}

/*******************************************************
 * Methods for reading and writing data
 *******************************************************/

int GSCore::peekData(cid_t cid)
{
  // If availableData returns non-zero, then at least one byte is
  // available in the buffer, so we can just return that without further
  // checking.
  if (availableData(cid) > 0)
    return this->rx_data[this->rx_data_tail];
  return -1;
}

int GSCore::readData(cid_t cid)
{
  // First, make sure we have a valid frame header
  if (!getFrameHeader(cid))
    return -1;

  return getData();
}

size_t GSCore::readData(cid_t cid, uint8_t *buf, size_t size)
{
  // First, make sure we have a valid frame header
  if (!getFrameHeader(cid))
    return 0;

  if (this->rx_data_tail != this->rx_data_head) {
    // There is data in the buffer. Find out how much data we can read
    // consecutively
    rx_data_index_t len;
    if (this->rx_data_head > this->rx_data_tail) {
      // Data can be read from the tail to the head
      len = this->rx_data_head - this->rx_data_tail;
    } else {
      // Data can be read from the tail to the end of the buffer
      len = sizeof(this->rx_data) - this->rx_data_tail;
    }
    // Don't read beyond the end of the frame
    if (len > this->tail_frame.length)
      len = this->tail_frame.length;
    // Don't write beyond the end of the buffer
    if (len > size)
      len = size;
    memcpy(buf, &this->rx_data[this->rx_data_tail], len);
    this->rx_data_tail = (this->rx_data_tail + len) % sizeof(this->rx_data);
    // If the buffer isn't full yet, call ourselves again to read more
    // data:
    //  - From the start of the buffer if we read up to the end of rx_data
    //  - From a next frame, if we read up to the end of the frame
    //  - From the module directly, if we emptied rx_data
    if (len != size)
      len += readData(cid, buf + len, size - len);
    return len;
  } else {
    // No data buffered, try reading from the module directly, as long
    // as it keeps sending us data
    size_t read = 0;
    while (read < size) {
      int c = readRaw();
      if (c == -1)
        break;
      buf[read++] = c;
      this->tail_frame.length--;
      if(--this->head_frame.length == 0) {
        this->rx_state = GS_RX_RESPONSE;
        break;
      }
    }
    return read;
  }
}

int GSCore::readData(cid_t *cid)
{
  // First, make sure we have a valid frame header
  if (!getFrameHeader(ANY_CID))
    return -1;

  int c = getData();
  if (c >= 0)
    *cid = this->tail_frame.cid;
  return c;
}

GSCore::cid_t GSCore::firstCidWithData()
{
  if (!getFrameHeader(ANY_CID))
    return INVALID_CID;
  return this->tail_frame.cid;
}

uint16_t GSCore::availableData(cid_t cid)
{
  if (!getFrameHeader(cid))
    return 0;

  // If we return a number here, we must be sure that that many bytes
  // can actually be read without blocking (e.g., applications should be
  // able to call readData() for this many times without it returning
  // -1).
  //
  // This means that we can only return the number of bytes actually
  // available in our buffer (even in SPI mode, we have no guarantee
  // that we can actually pull data directly after receiving the frame
  // header...).
  //
  // However, we should be careful with returning 0 here. A common
  // strategy is to poll available() and only call read() when
  // available() returns > 0. So we should only return 0 when really is
  // no data available. For this reason, if our buffer is empty, try to
  // read at least one byte from the module.
  if (this->rx_data_head == this->rx_data_tail)
    processIncomingAsyncOnly(readRaw());

  uint16_t len = (this->rx_data_head - this->rx_data_tail) % sizeof(this->rx_data);
  if (len > this->tail_frame.length)
    len = this->tail_frame.length;
  return len;
}

bool GSCore::writeData(cid_t cid, const uint8_t *buf, uint8_t len)
{
  if (cid > MAX_CID)
    return false;

  uint8_t header[8]; // Including a trailing 0 that snprintf insists to write
  // TODO: Also support UDP server
  snprintf((char*)header, sizeof(header), "\x1bZ%x%04d", cid, len);
  writeRaw(header, sizeof(header) - 1);
  writeRaw(buf, len);
  #ifdef GS_DUMP_LINES
  SERIAL_PORT_MONITOR.print("Written bulk data frame for cid ");
  SERIAL_PORT_MONITOR.print(cid);
  SERIAL_PORT_MONITOR.print(" containing ");
  SERIAL_PORT_MONITOR.print(len);
  SERIAL_PORT_MONITOR.println(" bytes");
  #endif
  GSResponse res = readResponse();
  #ifdef GS_LOG_ERRORS
  if (res != GS_DATA_SUCCESS && res != GS_DATA_FAILURE)
    SERIAL_PORT_MONITOR.println("Unexpected response to bulk data frame");
  #endif

  return (res == GS_DATA_SUCCESS);
}

/*******************************************************
 * Methods for writing commands / reading replies
 *******************************************************/

void GSCore::writeCommand(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  writeCommand(fmt, args);
  va_end(args);
}

void GSCore::writeCommand(const char *fmt, va_list args)
{
  uint8_t buf[128];
  size_t len = vsnprintf((char*)buf, sizeof(buf) - 2, fmt, args);
  if (len > sizeof(buf) - 2) {
    len = sizeof(buf) - 2;
    #ifdef GS_LOG_ERRORS
      SERIAL_PORT_MONITOR.print("Command truncated: ");
      SERIAL_PORT_MONITOR.write(buf, len);
      SERIAL_PORT_MONITOR.println();
    #endif
  }

  #ifdef GS_DUMP_LINES
  SERIAL_PORT_MONITOR.print("> ");
  SERIAL_PORT_MONITOR.write(buf, len);
  SERIAL_PORT_MONITOR.println();
  #endif

  buf[len++] = '\r';
  buf[len++] = '\n';

  this->writeRaw(buf, len);
}

bool GSCore::writeCommandCheckOk(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  writeCommand(fmt, args);
  va_end(args);

  return (readResponse() == GS_SUCCESS);
}

GSCore::GSResponse GSCore::readResponse(uint8_t *buf, uint16_t* len, cid_t *connect_cid)
{
  uint16_t read = 0;
  uint16_t line_start = 0;
  GSResponse res;
  while(true) {
    int c = readRaw();
    if (c == -1) {
      // TODO: timeout?
      continue;
    }

    if (this->rx_state != GS_RX_RESPONSE || c == 0x1b || this->rx_async_len) {
      // We're currently handling connection data, are about to get
      // connection data, or are halfway through handling an async
      // respons. Let bufferIncoming sort that out.
      processIncomingAsyncOnly(c);
    } else  if ((c == '\r' || c == '\n')) {
      // This normalizes all sequences of line endings into a single
      // \r\n and strips leading \r\n sequences, because responses tend
      // to use a lot of extra \r\n (or \n or even \n\r :-S) sequences.
      // As a side effect, this removes empty lines from output, but
      // that's ok.
      if ((read > 0 && buf[read - 1] == '\n') || read == 0)
        continue;

      res = processResponseLine(buf + line_start, read - line_start, connect_cid);
      if (res == GS_UNKNOWN_RESPONSE) {
        // Unknown response, so it's probably actual data that the
        // caller will want to have. Leave it in the buffer, and
        // terminate it with \r\n.
        if (read < *len) buf[read++] = '\r';
        if (read < *len) buf[read++] = '\n';
        line_start = read;
      } else {
        // The response was known, so back it out of the buffer
        read = line_start;
        if (res != GS_CON_SUCCESS && res != GS_ASYNC_HANDLED) {
          // All other responses indicate the end of the reply
          *len = read;
          return res;
        }
      }
    } else {
      if (read < *len) {
        buf[read++] = c;
      } else {
        // Ok, we were asked to store the reply, but the buffer wasn't
        // big enough. We can't just discard the rest of the data,
        // because then we have no buffer space to find out where the
        // response ends. To fix this, we resort to using readResponse()
        // for reading (and discarding) the rest of the response.
        // However, there is a small chance that we're currently halfway
        // through the last line of the response, so we copy the current
        // line into this->rx_async (which is used by readResponse) to
        // make this case work as well. If the line is already longer
        // than the rx_async buffer, don't bother, responses don't get
        // so long.
        uint16_t line_len = (read - line_start);
        if (line_len + 1 < sizeof(this->rx_async)) {
          memcpy(this->rx_async, buf + line_start, line_len);
          rx_async[line_len] = c;
          this->rx_async_len = line_len + 1;
          read = line_start;
        }
        #ifdef GS_LOG_ERRORS
        SERIAL_PORT_MONITOR.println("Response buffer too small");
        #endif
        *len = read;
        return readResponse();
      }
    }
  }
}

GSCore::GSResponse GSCore::readResponse(uint8_t *connect_id)
{
  while(true) {
    GSResponse res = processIncoming(readRaw(), connect_id);
    switch (res) {
      case GS_NO_RESPONSE:
      case GS_UNKNOWN_RESPONSE:
      case GS_ASYNC_HANDLED:
      case GS_CON_SUCCESS:
        // This is data the caller didn't ask for, so start a new
        // line
        break;
      default:
        // All other responses terminate the reply
        return res;
    }
    // TODO: timeout?
  }
}


void GSCore::writeRaw(const uint8_t *buf, uint8_t len)
{
  #ifdef GS_DUMP_BYTES
  for (uint8_t i = 0; i < len; ++i) {
    SERIAL_PORT_MONITOR.print(">> 0x");
    if (buf[i] < 0x10) SERIAL_PORT_MONITOR.print("0");
    SERIAL_PORT_MONITOR.print(buf[i], HEX);
    if (isprint(buf[i])) {
      SERIAL_PORT_MONITOR.print(" (");
      SERIAL_PORT_MONITOR.write(buf[i]);
      SERIAL_PORT_MONITOR.print(")");
    }
    SERIAL_PORT_MONITOR.println();
  }
  #endif
  if (this->serial) {
    this->serial->write(buf, len);
  } else if (this->ss) {
    while (len) {
      digitalWrite(this->ss, LOW);
      if (this->spi_xoff) {
        // Module sent XOFF, so send IDLE bytes until it reports it has
        // buffer space again.
        processIncomingAsyncOnly(processSpiSpecial(SPI.transfer(SPI_SPECIAL_IDLE)));
      } else {
        processIncomingAsyncOnly(processSpiSpecial(SPI.transfer(*buf)));
        buf++;
        len--;
      }
      // Module wants SS deasserted after every byte, otherwise it'll
      // ignore subsequent bytes and return 0xff
      digitalWrite(this->ss, HIGH);
    }
  }
}

int GSCore::readRaw()
{
  int c;
  if (this->serial) {
    c = this->serial->read();
  } else if (this->ss != SPI_DISABLED) {
    digitalWrite(this->ss, LOW);
    c = processSpiSpecial(SPI.transfer(SPI_SPECIAL_IDLE));
    digitalWrite(this->ss, HIGH);
  } else {
    #ifdef GS_LOG_ERRORS
      SERIAL_PORT_MONITOR.println("Begin() not called!");
    #endif
    return -1;
  }
  #ifdef GS_DUMP_BYTES
  if (c >= 0) {
    SERIAL_PORT_MONITOR.print("<< 0x");
    if (c < 0x10) SERIAL_PORT_MONITOR.print("0");
    SERIAL_PORT_MONITOR.print(c, HEX);
    if (isprint(c)) {
      SERIAL_PORT_MONITOR.print(" (");
      SERIAL_PORT_MONITOR.write(c);
      SERIAL_PORT_MONITOR.print(")");
    }
    SERIAL_PORT_MONITOR.println();
  }
  #endif
  return c;
}

/*******************************************************
 * Internal helper methods
 *******************************************************/

int GSCore::processSpiSpecial(uint8_t c)
{
  // Previous byte was an escape byte, so unescape this byte but don't
  // interpret any special characters inside.
  if (this->spi_prev_was_esc) {
    this->spi_prev_was_esc = false;
    return c ^ SPI_ESC_XOR;
  }

  int res = -1;
  switch(c) {
    case SPI_SPECIAL_ALL_ONE:
    case SPI_SPECIAL_ALL_ZERO:
      // TODO: Handle these? Flag an error? Wait for SPI_SPECIAL_ESC?
    case SPI_SPECIAL_ACK:
      // TODO: What does this one mean exactly?
      #ifdef GS_LOG_ERRORS
      SERIAL_PORT_MONITOR.println("SPI ACK received?");
      #endif
    case SPI_SPECIAL_IDLE:
      break;
    case SPI_SPECIAL_XOFF:
      this->spi_xoff = true;
      break;
    case SPI_SPECIAL_XON:
      this->spi_xoff = false;
      break;
    case SPI_SPECIAL_ESC:
      this->spi_prev_was_esc = true;
      break;
    default:
      res = c;
      break;
  }
  return res;
}


GSCore::GSResponse GSCore::processIncomingAsyncOnly(int c)
{
  GSResponse res = processIncoming(c);

  #ifdef GS_LOG_ERRORS
  if (res != GS_ASYNC_HANDLED && res != GS_NO_RESPONSE) {
    if (res == GS_UNKNOWN_RESPONSE) {
      SERIAL_PORT_MONITOR.println("Unknown response received");
    } else {
      SERIAL_PORT_MONITOR.print("Unexpected response received: ");
      SERIAL_PORT_MONITOR.println(res);
    }
  }
  #endif

  return res;
}

GSCore::GSResponse GSCore::processIncoming(int c, cid_t *connect_cid)
{
  if (c < 0)
    return GS_NO_RESPONSE;

  GSResponse res = GS_NO_RESPONSE;

  switch(this->rx_state) {
    case GS_RX_RESPONSE:
      if (c == 0x1b) {
        // Escape character, incoming data
        this->rx_state = GS_RX_ESC;
      } else if (c == '\n' || c == '\r') {
        // End of response line, process it
        if (this->rx_async_len > 0)
          res = processResponseLine(this->rx_async, this->rx_async_len, connect_cid);
        this->rx_async_len = 0;
      } else {
        if (this->rx_async_len < sizeof(this->rx_async))
          this->rx_async[this->rx_async_len++] = c;
        #ifdef GS_LOG_ERRORS
        else
          SERIAL_PORT_MONITOR.println("rx_async is full");
        #endif
      }
      break;

    case GS_RX_ESC:
      switch (c) {
        case 'Z':
          // Incoming TCP client/server or UDP client data
          // <Esc>Z<CID><Data Length xxxx 4 ascii char><data>
          this->rx_state = GS_RX_ESC_Z;
          break;

        case 'O':
          // OK response after sending data
          this->rx_state = GS_RX_RESPONSE;
          res = GS_DATA_SUCCESS;
          break;

        case 'F':
          // Failure response after sending data
          this->rx_state = GS_RX_RESPONSE;
          res = GS_DATA_SUCCESS;
          break;

        case 'y':
          // Incoming UDP server data
          // <Esc>y<CID><IP address><space><port><horizontal tab><Data Length xxxx 4 ascii char><data>
          // TODO
          // fallthrough for now
        default:
          // Unknown escape sequence? Revert to GS_RX_RESPONSE and hope for
          // the best...
          this->rx_state = GS_RX_RESPONSE;
          #ifdef GS_LOG_ERRORS
            SERIAL_PORT_MONITOR.print("Unknown escape character: ");
            SERIAL_PORT_MONITOR.write(c);
            SERIAL_PORT_MONITOR.println();
          #endif
      }
      break;

    case GS_RX_ESC_Z:
      this->head_frame.cid = parseCid(c);
      if (this->head_frame.cid != INVALID_CID) {
        this->rx_state = GS_RX_ESC_Z_LEN0;
        this->head_frame.length = 0;
      } else {
        // Invalid escape sequence? Revert to GS_RX_RESPONSE and hope
        // for the best...
        this->rx_state = GS_RX_RESPONSE;
        #ifdef GS_LOG_ERRORS
          SERIAL_PORT_MONITOR.print("Invalid cid received: <ESC>Z");
          SERIAL_PORT_MONITOR.write(c);
          SERIAL_PORT_MONITOR.println();
        #endif
      }
      break;

    case GS_RX_ESC_Z_LEN0:
    case GS_RX_ESC_Z_LEN1:
    case GS_RX_ESC_Z_LEN2:
    case GS_RX_ESC_Z_LEN3:
      // Parse the four-digit data length
      if (c < '0' || c > '9') {
        // Invalid escape sequence? Revert to GS_RX_RESPONSE and hope
        // for the best...
        this->rx_state = GS_RX_RESPONSE;
        #ifdef GS_LOG_ERRORS
          SERIAL_PORT_MONITOR.print("Invalid length byte in <ESC>Z sequence received: ");
          SERIAL_PORT_MONITOR.write(c);
          SERIAL_PORT_MONITOR.println();
        #endif
      }
      this->head_frame.length *= 10;
      this->head_frame.length += (c - '0');

      static_assert( (int)GS_RX_ESC_Z_LEN0 + 1 == (int)GS_RX_ESC_Z_LEN1, "GS_RX_ESC_Z_LENx values not consecutive?");
      static_assert( (int)GS_RX_ESC_Z_LEN1 + 1 == (int)GS_RX_ESC_Z_LEN2, "GS_RX_ESC_Z_LENx values not consecutive?");
      static_assert( (int)GS_RX_ESC_Z_LEN2 + 1 == (int)GS_RX_ESC_Z_LEN3, "GS_RX_ESC_Z_LENx values not consecutive?");

      if (this->rx_state != GS_RX_ESC_Z_LEN3) {
        this->rx_state = (RXState)((int)this->rx_state + 1);
      } else {
        #ifdef GS_DUMP_LINES
        SERIAL_PORT_MONITOR.print("Read bulk data frame for cid ");
        SERIAL_PORT_MONITOR.print(this->head_frame.cid);
        SERIAL_PORT_MONITOR.print(" containing ");
        SERIAL_PORT_MONITOR.print(this->head_frame.length);
        SERIAL_PORT_MONITOR.println(" bytes");
        #endif
        // Read escape sequence, so store the frame header and prepare
        // to read data
        bufferFrameHeader(&this->head_frame);
        this->rx_state = GS_RX_BULK;
      }
      break;

    case GS_RX_BULK:
      bufferIncomingData(c);
      if(--this->head_frame.length == 0)
        this->rx_state = GS_RX_RESPONSE;
      break;
  }
  return res;
}

void GSCore::bufferIncomingData(uint8_t c)
{
  rx_data_index_t next_head = (this->rx_data_head + 1) % sizeof(this->rx_data);
  if (next_head == this->rx_data_tail)
    dropData(1);

  this->rx_data[this->rx_data_head] = c;
  this->rx_data_head = next_head;
}

void GSCore::bufferFrameHeader(const RXFrame *frame)
{
  if (this->rx_data_head == this->rx_data_tail) {
    // Ringbuffer is empty, so this frame becomes the tail_frame
    // directly.
    this->tail_frame = this->head_frame;
  } else {
    // There is a previous frame in the ringbuffer, so put in the
    // frame info in the ringbuffer as well.
    if ( this->rx_data_head > sizeof(this->rx_data) - sizeof(*frame)) {
      // The RXFrame structure doesn't fit in the ringbuffer
      // consecutively. Skip a few bytes to skip back to the start.
      // But if we don't have room to do that, we'll have to drop bytes
      // from the tail to make room.
      if (this->rx_data_tail > this->rx_data_head)
        dropData(this->rx_data_tail - this->rx_data_tail);
      if (this->rx_data_tail == 0)
        dropData(1);

      this->rx_data_head = 0;
    }

    // Make sure there's enough space
    uint8_t free = (this->rx_data_tail - this->rx_data_head - 1) % sizeof(this->rx_data);
    if (free < sizeof(*frame))
      dropData(sizeof(*frame) - free);

    // Copy the frame header
    memcpy(&this->rx_data[this->rx_data_head], frame, sizeof(*frame));
    this->rx_data_head += sizeof(*frame);
  }
}

void GSCore::loadFrameHeader(RXFrame* frame)
{
  if (sizeof(this->rx_data) - this->rx_data_tail < sizeof(*frame)) {
    // The RXFrame structure didn't fit in the ringbuffer
    // consecutively. Skip a few bytes to skip back to the start
    this->rx_data_tail = 0;
  }
  memcpy(frame, &this->rx_data[this->rx_data_tail], sizeof(*frame));
}

bool GSCore::getFrameHeader(cid_t cid)
{
  // Already have a valid frame header, nothing to do
  if (this->tail_frame.length != 0)
    return true;

  if (cid != ANY_CID && this->tail_frame.cid != cid)
    return false;

  if (this->rx_data_tail != this->rx_data_head) {
    // The current frame is empty, but there is still data in rx_data.
    // Load the next frame.
   loadFrameHeader(&this->tail_frame);
  } else {
    // The buffer is empty. See if we can read more data from the
    // module.
    while (this->tail_frame.length == 0) {
      int c = readRaw();
      // Don't block
      if (c < 0)
        return false;
      processIncomingAsyncOnly(c);
    }
  }
  return true;
}

int GSCore::getData()
{
  if (this->rx_data_tail != this->rx_data_head) {
    // There is data in the buffer, read it
    int c = this->rx_data[this->rx_data_tail];
    this->rx_data_tail = (this->rx_data_tail + 1) % sizeof(this->rx_data);
    this->tail_frame.length--;
    return c;
  } else {
    // No data buffered, try reading from the module directly
    int c = readRaw();
    if (c >= 0) {
      this->tail_frame.length--;
      if(--this->head_frame.length == 0)
        this->rx_state = GS_RX_RESPONSE;
    }
    return c;
  }
}

void GSCore::dropData(uint8_t num_bytes) {
  while(num_bytes--) {
    cid_t cid;
    if (readData(&cid) >= 0) {
      #ifdef GS_LOG_ERRORS
        SERIAL_PORT_MONITOR.print("rx_data is full, dropped byte for cid ");
        SERIAL_PORT_MONITOR.println(cid);
      #endif
      this->connections[cid].error = true;
    }
  }
}

GSCore::GSResponse GSCore::processResponseLine(const uint8_t* buf, uint8_t len, cid_t *connect_cid)
{
  const uint8_t *args;
  GSResponse code = GS_UNKNOWN_RESPONSE;
  cid_t cid;

  #ifdef GS_DUMP_LINES
  SERIAL_PORT_MONITOR.print("< ");
  SERIAL_PORT_MONITOR.write(buf, len);
  SERIAL_PORT_MONITOR.println();
  #endif

  // In non-verbose mode, command responses are an (string containing a)
  // number from "0" to "18"
  if (len >= 2 && buf[0] == '1' && buf[1] >= '0' && buf[1] <= '8') {
    args = buf + 2;
    code = (GSResponse)(10 + buf[1] - '0');
  } else if (len >= 1 && buf[0] >= '0' && buf[0] <= '9') {
    args = buf + 1;
    code = (GSResponse)(buf[0] - '0');
  }

  if (code == GS_UNKNOWN_RESPONSE)
    return code;

  uint8_t arg_len = buf + len - args;
  switch (code) {
    case GS_CON_SUCCESS:
      if (arg_len < 2 || args[0] != ' ')
        return GS_UNKNOWN_RESPONSE;
      if (arg_len == 2) {
        // CONNECT <CID>
        cid_t cid = parseCid(args[1]);

        if (cid == INVALID_CID)
          return GS_UNKNOWN_RESPONSE;

        if (!connect_cid) // got CONNECT while not expecting it? -> unknown
          return GS_UNKNOWN_RESPONSE;

        *connect_cid = cid;
        return code;
      } else if (arg_len > 2 && args[0] == ' ') {
        // CONNECT <server CID> <new CID> <ip> <port>,
        // TODO
        return GS_ASYNC_HANDLED;
      }
    case GS_SOCK_FAIL:
    case GS_ECIDCLOSE:
      if (arg_len != 2 || args[0] != ' ')
        return GS_UNKNOWN_RESPONSE;

      cid = parseCid(args[1]);
      if (cid == INVALID_CID)
        return GS_UNKNOWN_RESPONSE;

      if (code == GS_SOCK_FAIL) {
        // ERROR: SOCKET: FAILURE <CID>
        // Documentation is unclear, but experimentation shows that when
        // this happens, some data might have been lost and the
        // connection is broken.
        #ifdef GS_LOG_ERRORS
          SERIAL_PORT_MONITOR.print("Socket error on cid ");
          SERIAL_PORT_MONITOR.println(cid);
        #endif
        this->connections[cid].error = true;
        this->connections[cid].connected = false;
        return GS_ASYNC_HANDLED;
      } else { // code == GS_ECIDCLOSE
        // DISCONNECT <CID>
        this->connections[cid].connected = false;
        return GS_ASYNC_HANDLED;
      }
    default:
      // All others do not have arguments
      if (arg_len > 0)
        return GS_UNKNOWN_RESPONSE;

      switch (code) {
        case GS_SUCCESS:
        case GS_FAILURE:
        case GS_EINVAL:
        case GS_ENOCID:
        case GS_EBADCID:
        case GS_ENOTSUP:
        case GS_LINK_LOST:
          // These terminate a reply, so no further action needed.
          return code;

        case GS_DISASSO_EVT:
          // TODO: This means the wifi association has broken. Update our
          // state.
          return GS_ASYNC_HANDLED;

        case GS_STBY_TMR_EVT:
        case GS_STBY_ALM_EVT:
        case GS_DPSLEEP_EVT:
          // TODO: These are given after the wifi module is told to go
          // into standby. How to handle these? Perhaps just print some
          // debug info for now and then ignore them?
          return GS_ASYNC_HANDLED;

        case GS_BOOT_UNEXPEC:
        case GS_BOOT_INTERNAL:
        case GS_BOOT_EXTERNAL:
          // These indicate the hardware has just reset
          // TODO: Reset our state to match the hardware. Also make sure
          // to stop waiting for a reply to a command, since it will never
          // come.
          return GS_ASYNC_HANDLED;

        case GS_NWCONN_SUCCESS:
          // TODO: When does this occur?
          return GS_ASYNC_HANDLED; // ?

        case GS_ENOIP:
          // ERROR: IP CONFIG FAIL
          // Sent asynchronously when the DHCP release fails, but also
          // as a reply to AT+NDHCP=1 or AT+WA=. Afterwards, the
          // hardware loses its address and does not retry DHCP again.
          // TODO: Handle the asynchronous case as well.
          return code;

        default:
          // This should never happen, but the compiler gives a warning if
          // we don't handle _all_ enumeration values...
          return GS_UNKNOWN_RESPONSE;
      }
  }
}

/*******************************************************
 * Static helper methods
 *******************************************************/

GSCore::cid_t GSCore::parseCid(uint8_t c)
{
  if (c >= '0' && c <= '9')
    return (c - '0');
  if (c >= 'a' && c <= 'f')
    return (c - 'a');
  return INVALID_CID;
}

// vim: set sw=2 sts=2 expandtab:
