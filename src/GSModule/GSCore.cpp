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
#include "static_assert.h"

#if defined(GS_DUMP_BYTES) || defined(GS_DUMP_SPI) || defined(GS_LOG_ERRORS) || defined(GS_LOG_ERRORS_VERBOSE)
static void dump_byte(const char *prefix, int c, bool newline = true) {
  if (c >= 0) {
    SERIAL_PORT_MONITOR.print(prefix);
    SERIAL_PORT_MONITOR.print("0x");
    if (c < 0x10) SERIAL_PORT_MONITOR.print("0");
    SERIAL_PORT_MONITOR.print(c, HEX);
    if (isprint(c)) {
      SERIAL_PORT_MONITOR.print(" (");
      SERIAL_PORT_MONITOR.write(c);
      SERIAL_PORT_MONITOR.print(")");
    }
    if (newline)
      SERIAL_PORT_MONITOR.println();
    // Needed to work around some buffer overflow problem in a part of
    // the serial output.
    SERIAL_PORT_MONITOR.flush();
  }
}
#endif

/*******************************************************
 * Methods for setting up the module
 *******************************************************/

GSCore::GSCore()
{
  this->ss_pin = INVALID_PIN;
  this->onNcmConnect = NULL;
  this->onNcmDisconnect = NULL;
  this->onAssociate = NULL;
  this->onDisassociate = NULL;
  this->initializing = false;

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
  if (this->serial || this->ss_pin != INVALID_PIN)
    return false;

  this->initializing = true;
  this->serial = &serial;
  bool res = _begin();
  this->initializing = false;
  return res;
}

bool GSCore::begin(uint8_t ss, uint8_t data_ready)
{
  if (this->serial || this->ss_pin != INVALID_PIN || ss == INVALID_PIN)
    return false;

  this->initializing = true;
  this->ss_pin = ss;
  this->data_ready_pin = data_ready;

  pinMode(ss, OUTPUT);
  digitalWrite(ss, HIGH);

  bool res = _begin();
  this->initializing = false;
  return res;
}

bool GSCore::_begin()
{
  this->rx_state = GS_RX_IDLE;
  this->rx_data_head = this->rx_data_tail = 0;
  this->tail_frame.length = 0;
  this->spi_prev_was_esc = false;
  this->spi_xoff = false;
  this->ncm_auto_cid = INVALID_CID;
  this->events = 0;
  this->spi_poll_time = micros() - MINIMUM_POLL_INTERVAL;

  // TODO: Query AT+NSTAT=? to see if we are aready connected (in case
  // the NCM already connected before we were initialized).
  this->associated = false;

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

  // Enable enhanced asynchronous messages
  if (!writeCommandCheckOk("AT+ASYNCMSGFMT=1"))
    return false;

  memset(this->connections, 0, sizeof(connections));

  return true;
}


void GSCore::end()
{
  this->serial = NULL;
  if (this->ss_pin != INVALID_PIN)
    pinMode(this->ss_pin, INPUT);
  this->ss_pin = INVALID_PIN;
  this->data_ready_pin = INVALID_PIN;
}

void GSCore::loop()
{
  readAndProcessAsync();

  if (this->onNcmDisconnect && (this->events & EVENT_NCM_DISCONNECTED)) {
    this->events &= ~EVENT_NCM_DISCONNECTED;
    this->onNcmDisconnect(this->eventData);
  }
  if (this->onDisassociate && (this->events & EVENT_DISASSOCIATED)) {
    this->events &= ~EVENT_DISASSOCIATED;
    this->onDisassociate(this->eventData);
  }
  if (this->onAssociate && (this->events & EVENT_ASSOCIATED)) {
    this->events &= ~EVENT_ASSOCIATED;
    this->onAssociate(this->eventData);
  }
  if (this->onNcmConnect && (this->events & EVENT_NCM_CONNECTED)) {
    this->events &= ~EVENT_NCM_CONNECTED;
    this->onNcmConnect(this->eventData, this->ncm_auto_cid);
  }
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
    this->tail_frame.length -= len;
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
        this->rx_state = GS_RX_IDLE;
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
    processIncoming(readRaw());

  uint16_t len = (this->rx_data_head - this->rx_data_tail) % sizeof(this->rx_data);
  if (len > this->tail_frame.length)
    len = this->tail_frame.length;
  return len;
}

bool GSCore::writeData(cid_t cid, const uint8_t *buf, uint16_t len)
{
  if (cid > MAX_CID)
    return false;

  // Hardware doesn't support more than 1400, according to SERIAL-TO-WIFI ADAPTER
  // APPLICATION PROGRAMMING GUIDE, section 3.4.1 ("Bulk data Tx and Rx")
  if (len > 1400)
    return writeData(cid, buf, 1400) && writeData(cid, buf + 1400, len - 1400);

  #ifdef GS_DUMP_LINES
  SERIAL_PORT_MONITOR.print(">>| Writing bulk data frame for cid ");
  SERIAL_PORT_MONITOR.print(cid);
  SERIAL_PORT_MONITOR.print(" containing ");
  SERIAL_PORT_MONITOR.print(len);
  SERIAL_PORT_MONITOR.println(" bytes");
  #endif

  uint8_t header[8]; // Including a trailing 0 that snprintf insists to write
  // TODO: Also support UDP server
  snprintf((char*)header, sizeof(header), "\x1bZ%x%04d", cid, len);
  // First, write the escape sequence up to the cid. After this, the
  // module responds with <ESC>O or <ESC>F.
  writeRaw(header, 3);
  if (!readDataResponse()) {
    #ifdef GS_LOG_ERROR
    SERIAL_PORT_MONITOR.println("Sending bulk data frame failed");
    #endif
    return false;
  }

  // Then, write the rest of the escape sequence (-1 to not write the
  // trailing 0)
  writeRaw(header + 3, sizeof(header) - 1 - 3);
  // And write the actual data
  writeRaw(buf, len);
  return false;
}

bool GSCore::writeData(cid_t cid, IPAddress ip, uint16_t port, const uint8_t *buf, uint16_t len)
{
  if (cid > MAX_CID)
    return false;

  // Hardware doesn't support more than 1400, according to SERIAL-TO-WIFI ADAPTER
  // APPLICATION PROGRAMMING GUIDE, section 3.4.1 ("Bulk data Tx and Rx")
  if (len > 1400)
    return writeData(cid, buf, 1400) && writeData(cid, buf + 1400, len - 1400);

  uint8_t ipbuf[16];
  snprintf((char*)ipbuf, sizeof(ipbuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

  #ifdef GS_DUMP_LINES
  SERIAL_PORT_MONITOR.print(">>| Writing UDP server bulk data frame for cid ");
  SERIAL_PORT_MONITOR.print(cid);
  SERIAL_PORT_MONITOR.print(" to ");
  SERIAL_PORT_MONITOR.print((const char*)ipbuf);
  SERIAL_PORT_MONITOR.print(":");
  SERIAL_PORT_MONITOR.print(port);
  SERIAL_PORT_MONITOR.print(" containing ");
  SERIAL_PORT_MONITOR.print(len);
  SERIAL_PORT_MONITOR.println(" bytes");
  #endif

  uint8_t header[28]; // Including a trailing 0 that snprintf insists to write
  // TODO: Also support UDP server
  size_t headerlen = snprintf((char*)header, sizeof(header), "\x1bY%x%s %u\t%04d", cid, ipbuf, port, len);

  // First, write the escape sequence up to the cid. After this, the
  // module responds with <ESC>O or <ESC>F.
  writeRaw(header, 3);
  if (!readDataResponse()) {
    #ifdef GS_LOG_ERROR
    SERIAL_PORT_MONITOR.println("Sending UDP server bulk data frame failed");
    #endif
    return false;
  }

  // Then, write the rest of the escape sequence
  writeRaw(header + 3, headerlen - 3);
  // And write the actual data
  writeRaw(buf, len);
  return false;
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
  SERIAL_PORT_MONITOR.print(">>= ");
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

GSCore::GSResponse GSCore::readResponseInternal(uint8_t *buf, uint16_t* len, cid_t *connect_cid, bool keep_data, line_callback_t callback, void *data)
{
  uint16_t read = 0;
  uint16_t line_start = 0;
  bool dropped_data = false;
  bool skip_line = false;
  GSResponse res;
  unsigned long start = millis();
  while(true) {
    int c = readRaw();
    if (c == -1) {
      if ((unsigned long)(millis() - start) > RESPONSE_TIMEOUT) {
        #ifdef GS_LOG_ERRORS
        SERIAL_PORT_MONITOR.println("Response timeout");
        #endif
        return GS_RESPONSE_TIMEOUT;
      }
      continue;
    }

    if (this->rx_state != GS_RX_IDLE || c == 0x1b) {
      // We're currently handling connection or async data, or are about
      // to. Let processIncoming sort that out.
      processIncoming(c);
    } else if ((c == '\r' || c == '\n')) {
      // This normalizes all sequences of line endings into a single
      // \r\n and strips leading \r\n sequences, because responses tend
      // to use a lot of extra \r\n (or \n or even \n\r :-S) sequences.
      // As a side effect, this removes empty lines from output, but
      // that's ok.
      if (read - line_start == 0)
        continue;

      if (skip_line) {
        // Data from this line has been dropped because the buffer was
        // full, and it was too long for a response anyway, so further
        // ignore this line.
        skip_line = false;
        // Remove the line from the buffer
        read = line_start;
        #ifdef GS_DUMP_LINES
        SERIAL_PORT_MONITOR.println("<<| Skipped uninteresting long line");
        #endif
        continue;
      }
      skip_line = false;

      res = processResponseLine(buf + line_start, read - line_start, connect_cid);
      // When we get a GS_LINK_LOST, we're apparently not associated
      // when we thought we would be. Call processDisassciation() to fix
      // that.
      if (res == GS_LINK_LOST)
        processDisassociation();

      if (keep_data && !callback && !dropped_data && res == GS_UNKNOWN_RESPONSE) {
        // Unknown response, so it's probably actual data that the
        // caller will want to have. Leave it in the buffer, and
        // terminate it with \r\n.
        if (read < *len) buf[read++] = '\r';
        if (read < *len) buf[read++] = '\n';
        line_start = read;
      } else {
        // If we have a callback, pass any unknown response to it
        if (keep_data && callback && res == GS_UNKNOWN_RESPONSE)
          callback(&buf[line_start], read - line_start, data);

        // Remove the line from the buffer since we either handled it
        // already, or we're not interested in the data
        read = line_start;

        if (res != GS_UNKNOWN_RESPONSE && res != GS_CON_SUCCESS) {
          // All other responses indicate the end of the reply
          *len = read;
          return res;
        }
      }
    } else {
      if (read < *len) {
        buf[read++] = c;
      } else if ((read - line_start) >= MAX_RESPONSE_SIZE ) {
        // The buffer is full. However, the line is too long for a
        // response, so there is no danger in just discarding the byte.
        #ifdef GS_LOG_ERRORS
        if (keep_data)
          dump_byte("Response buffer too small, dropped byte: ", c);
        #endif
        // Make sure we won't try to parse the few bytes we have as a
        // response.
        skip_line = true;
        dropped_data = true;
      } else {
        // The buffer is full, but we can't just discard the byte: It
        // might be part of the final response we're waiting for.
        // Instead, drop the last byte of the previous line to make
        // room, and move any data in the current line accordingly.
        if (line_start > 0) {
          #ifdef GS_LOG_ERRORS
          if (keep_data)
            dump_byte("Response buffer too small, removed byte: ", buf[line_start - 1]);
          #endif
          memmove(&buf[line_start - 1], &buf[line_start], (read - line_start));
          line_start--;
          buf[read] = c;
        } else {
          // line_start == 0 should only happen if len <
          // MAX_RESPONSE_SIZE, but better be safe than sorry.
          #ifdef GS_LOG_ERRORS
          if (keep_data)
            dump_byte("Response buffer tiny? Dropped byte: ", c);
          #endif
        }

        // Once we threw away a byte of data, don't store any new ones
        // (to make sure the returned data is cleanly truncated instead
        // of having gaps).
        dropped_data = true;
      }
    }
  }
}

GSCore::GSResponse GSCore::readResponse(uint8_t *buf, uint16_t* len, cid_t *connect_cid) {
  return readResponseInternal(buf, len, connect_cid, true, NULL, NULL);
}

GSCore::GSResponse GSCore::readResponse(cid_t *connect_cid)
{
  uint8_t buf[MAX_RESPONSE_SIZE];
  uint16_t len = sizeof(buf);
  return readResponseInternal(buf, &len, connect_cid, false, NULL, NULL);
}

GSCore::GSResponse GSCore::readResponse(line_callback_t callback, void *data, cid_t *connect_cid)
{
  uint8_t buf[MAX_DATA_LINE_SIZE];
  uint16_t len = sizeof(buf);
  return readResponseInternal(buf, &len, connect_cid, true, callback, data);
}

bool GSCore::readDataResponse()
{
  unsigned long start = millis();
  while(true) {
    int c = readRaw();

    if (c == -1) {
      if ((unsigned long)(millis() - start) > RESPONSE_TIMEOUT) {
        #ifdef GS_LOG_ERRORS
        SERIAL_PORT_MONITOR.println("Data response timeout");
        #endif
        return false;
      }
      continue;
    }

    if (this->rx_state == GS_RX_ESC && c == 'O') {
      #ifdef GS_DUMP_LINES
      SERIAL_PORT_MONITOR.println("<<| Read data OK response");
      #endif
      this->rx_state = GS_RX_IDLE;
      return true;
    } else if (this->rx_state == GS_RX_ESC && c == 'F') {
      #ifdef GS_DUMP_LINES
      SERIAL_PORT_MONITOR.println("<<| Read data FAIL response");
      #endif
      this->rx_state = GS_RX_IDLE;
      return false;
    } else {
      processIncoming(c);
    }
  }
}


uint8_t GSCore::transferSpi(uint8_t out)
{
  // Note that we need to toggle SS for every byte, otherwise the module
  // will ignore subsequent bytes and return 0xff
  digitalWrite(this->ss_pin, LOW);
  uint8_t in = SPI.transfer(out);
  digitalWrite(this->ss_pin, HIGH);
  #ifdef GS_DUMP_SPI
  if (in != SPI_SPECIAL_IDLE || out != SPI_SPECIAL_IDLE) {
    dump_byte("SPI: >> ", out, false);
    dump_byte(" << ", in);
  }
  #endif
  return in;
}

void GSCore::writeRaw(const uint8_t *buf, uint16_t len)
{
  if (this->serial) {
    #ifdef GS_DUMP_BYTES
    for (uint16_t i = 0; i < len; ++i)
      dump_byte(">= ", buf[i]);
    #endif
    this->serial->write(buf, len);
  } else if (this->ss_pin) {
    while (len) {
      if (this->spi_xoff) {
        // Module sent XOFF, so send IDLE bytes until it reports it has
        // buffer space again.
        processIncoming(processSpiSpecial(transferSpi(SPI_SPECIAL_IDLE)));
      } else {
        #ifdef GS_DUMP_BYTES
        dump_byte(">= ", *buf);
        #endif
        if (isSpiSpecial(*buf)) {
          processIncoming(processSpiSpecial(transferSpi(SPI_SPECIAL_ESC)));
          processIncoming(processSpiSpecial(transferSpi(*buf ^ SPI_ESC_XOR)));
        } else {
          processIncoming(processSpiSpecial(transferSpi(*buf)));
        }
        buf++;
        len--;
      }
    }
  }
}

int GSCore::readRaw()
{
  int c;
  if (this->serial) {
    c = this->serial->read();
    #ifdef GS_DUMP_BYTES
    if (c >= 0)
      dump_byte("<= ", c);
    #endif
  } else if (this->ss_pin != INVALID_PIN) {

    // When the data ready pin (GPIO28) is low, there is no point in
    // trying to read, we'll read idle bytes for sure.
    if (this->data_ready_pin != INVALID_PIN && !digitalRead(this->data_ready_pin))
      return -1;

    int tries;
    if (this->data_ready_pin != INVALID_PIN) {
      // If the data ready pin is high, the documentation says we should
      // just keep reading until the pin goes low. In practice, it turns
      // out that when the pin is high, we can still read idle bytes, so
      // we should just keep reading until we get actual data. To
      // prevent accidental deadlock when the module messes up, we stop
      // trying if we keep reading idle bytes.
      // It appears that when the module is idle, it nearly fills up its
      // SPI buffer with 63 idle bytes, which stay in there even when
      // real data becomes available. So whenever the data ready pin
      // goes high, we first have to chew away 63 idle bytes before we
      // get the real data. Using 64 tries should thus be a useful
      // value.
      tries = 64;
    } else {
      // When we do not have a data ready pin available, we'll have to
      // resort to polling. However, because of those 63 idle bytes,
      // we'll have to read 64 idle bytes before we can be sure that
      // there is really no data available. It's cumbersome, but it'll
      // work...
      // Since our callers might go to sleep or otherwise won't repeat a
      // readRaw() call directly, we have to really be sure there is no
      // data available before we return -1.
      //
      // However, this can introduce a lot of overhead and since
      // it's unlikely that new data is available when there wasn't any
      // a few microseconds ago, we should be smart about when to do a
      // full poll.
      uint16_t new_time = micros();
      uint16_t diff = new_time - this->spi_poll_time;
      if (diff < MINIMUM_POLL_INTERVAL) {
        // We recently did polling, so no need to do a full poll.
        // However, we'll always read at least one byte, so that when we
        // get called continously, new data can arrive before
        // MINIMUM_POLL_INTERVAL has passed.
        tries = 1;

        // Update the the poll timestamp. even though we didn't do a
        // full poll now, we read 1/64th of a full poll, so progress the
        // timestamp by that amount (taking care to not progress it past
        // the current timestamp).
        if (diff < MINIMUM_POLL_INTERVAL / 64)
          this->spi_poll_time = new_time;
        else
          this->spi_poll_time += (MINIMUM_POLL_INTERVAL / 64);
      } else {
        // We haven't done enough polling recently, so do a full poll
        // now.
        tries = 64;
        this->spi_poll_time = new_time;
      }
    }

    do {
      c = processSpiSpecial(transferSpi(SPI_SPECIAL_IDLE));
    } while (c == -1 && --tries > 0);
  } else {
    #ifdef GS_LOG_ERRORS
      SERIAL_PORT_MONITOR.println("Begin() not called!");
    #endif
    return -1;
  }
  return c;
}

/*******************************************************
 * Helper methods
 *******************************************************/

bool GSCore::parseIpAddress(IPAddress *ip, const char *str, uint16_t len)
{
  *ip = (uint32_t)0;
  int i = 0;
  const char *end = (len ? str + len : NULL);
  for (const char *p = str; *p && (end == NULL || p < end); ++p) {
    if (*p == '.') {
      ++i;
      if (i >= 4)
        return false;

      (*ip)[i] = 0;
      continue;
    }

    if (*p < '0' || *p > '9')
      return false;

    if ((*ip)[i] >= 100 || ((*ip)[i] == 25 && *p > '5'))
      return false;

    (*ip)[i] *= 10;
    (*ip)[i] += (*p - '0');
  }
  return true;
}

/*******************************************************
 * Internal helper methods
 *******************************************************/

int GSCore::processSpiSpecial(uint8_t c)
{
  int res = -1;
  if (this->spi_prev_was_esc) {
    // Previous byte was an escape byte, so unescape this byte but don't
    // interpret any special characters inside.
    this->spi_prev_was_esc = false;
    res = c ^ SPI_ESC_XOR;
  } else {
    switch(c) {
      case SPI_SPECIAL_ALL_ONE:
        // TODO: Handle these? Flag an error? Wait for SPI_SPECIAL_ACK?
        #ifdef GS_LOG_ERRORS
        SERIAL_PORT_MONITOR.println("SPI 0xff?");
        #endif
        break;
      case SPI_SPECIAL_ALL_ZERO:
        // TODO: Handle these? Flag an error? Wait for SPI_SPECIAL_ACK?
        // Seems these happen when saving the current profile to flash
        // (probably because the APP firmware is too busy to refill the
        // SPI buffer in the module).
        #ifdef GS_LOG_ERRORS_VERBOSE
        SERIAL_PORT_MONITOR.println("SPI 0x00?");
        #endif
        break;
      case SPI_SPECIAL_ACK:
        // TODO: What does this one mean exactly?
        #ifdef GS_LOG_ERRORS
        SERIAL_PORT_MONITOR.println("SPI ACK received?");
        #endif
        break;
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
  }
  #ifdef GS_DUMP_BYTES
  if (res >= 0)
    dump_byte("<= ", res);
  #endif
  return res;
}

bool GSCore::isSpiSpecial(uint8_t c)
{
  switch(c) {
    case SPI_SPECIAL_ALL_ONE:
    case SPI_SPECIAL_ALL_ZERO:
    case SPI_SPECIAL_ACK:
    case SPI_SPECIAL_IDLE:
    case SPI_SPECIAL_XOFF:
    case SPI_SPECIAL_XON:
    case SPI_SPECIAL_ESC:
      return true;
    default:
      return false;
  }
}

bool GSCore::processIncoming(int c)
{
  if (c < 0)
    return false;

  switch(this->rx_state) {
    case GS_RX_IDLE:
      if (c == 0x1b) {
        // Escape character, incoming data
        this->rx_state = GS_RX_ESC;
      } else {
        #ifdef GS_LOG_ERRORS_VERBOSE
          // Don't log \r\n, since the synchronous response parsing
          // often leaves a \n behind. Only log in VERBOSE, since some
          // async responses also have data preceding them (like
          // NWCONN-SUCCESS that prints info about the IP configuration
          // _before_ the actual async response...).
          if (c != '\n' && c != '\r')
            dump_byte("Discarding non-escaped byte, no synchronous response expected: ", c);
        #endif
      }
      break;

    case GS_RX_ESC:
      // Note: <Esc>O and <Esc>F are handled in readDataResponse, since
      // they should never be received asynchronously
      switch (c) {
        case 'Z':
          // Incoming TCP client/server or UDP client data
          // <Esc>Z<CID><Data Length xxxx 4 ascii char><data>
          this->rx_state = GS_RX_ESC_Z;
          this->rx_async_left = 5;
          this->rx_async_len = 0;
          break;

        case 'A':
          // Asynchronous response
          // <ESC>A<Subtype><length 2 ascii char><data>
          this->rx_state = GS_RX_ESC_A;
          this->rx_async_left = 3;
          this->rx_async_len = 0;
          break;

        case 'y':
          // Incoming UDP server data
          // <Esc>y<CID><IP address><space><port><horizontal tab><Data Length xxxx 4 ascii char><data>
          this->rx_state = GS_RX_ESC_y_1;
          this->rx_async_len = 0;
          break;
          // fallthrough for now
        default:
          // Unknown escape sequence? Revert to GS_RX_IDLE and hope for
          // the best...
          this->rx_state = GS_RX_IDLE;
          #ifdef GS_LOG_ERRORS
            SERIAL_PORT_MONITOR.print("Unknown escape sequence: <Esc>");
            SERIAL_PORT_MONITOR.write(c);
            SERIAL_PORT_MONITOR.println();
          #endif
      }
      break;
    case GS_RX_ESC_y_1:
    case GS_RX_ESC_y_2:
    case GS_RX_ESC_y_3:
    case GS_RX_ESC_Z:
    case GS_RX_ESC_A:
    case GS_RX_ASYNC:
      if (this->rx_async_len < sizeof(this->rx_async)) {
        this->rx_async[this->rx_async_len++] = c;
      } else {
        #ifdef GS_LOG_ERRORS
          SERIAL_PORT_MONITOR.println("rx_async is full");
        #endif
      }

      // Finished reading the header or body, find out out what to do with it
      switch(this->rx_state) {
        case GS_RX_ESC_Z:
          if (--this->rx_async_left == 0) {
            // <CID><Data Length xxxx 4 ascii char><data>
            if (parseNumber(&this->head_frame.cid, this->rx_async, 1, 16) &&
                parseNumber(&this->head_frame.length, this->rx_async + 1, 4, 10)) {
              this->head_frame.udp_server = false;
              #ifdef GS_DUMP_LINES
              SERIAL_PORT_MONITOR.print("<<| Read bulk data frame for cid ");
              SERIAL_PORT_MONITOR.print(this->head_frame.cid);
              SERIAL_PORT_MONITOR.print(" containing ");
              SERIAL_PORT_MONITOR.print(this->head_frame.length);
              SERIAL_PORT_MONITOR.println(" bytes");
              #endif
              // Store the frame header and prepare to read data
              bufferFrameHeader(&this->head_frame);
              this->rx_state = GS_RX_BULK;
            } else {
              #if defined(GS_LOG_ERRORS) || defined(GS_DUMP_LINES)
                SERIAL_PORT_MONITOR.print("Invalid escape sequence: <ESC>Z");
                SERIAL_PORT_MONITOR.write(this->rx_async, this->rx_async_len);
                SERIAL_PORT_MONITOR.println();
              #endif
              // Revert to GS_RX_IDLE and hope for the best...
              this->rx_state = GS_RX_IDLE;
            }
          }
          break;

        case GS_RX_ESC_y_1:
          if (c == ' ')
            this->rx_state = GS_RX_ESC_y_2;

          break;

        case GS_RX_ESC_y_2:
          if (c == '\t') {
            this->rx_state = GS_RX_ESC_y_3;
            this->rx_async_left = 4;
          }
          break;

        case GS_RX_ESC_y_3:
        {
          if (--this->rx_async_left == 0) {
            #ifdef GS_DUMP_LINES
              SERIAL_PORT_MONITOR.print("<<| Read async header: <ESC>y");
              SERIAL_PORT_MONITOR.write(this->rx_async, this->rx_async_len);
              SERIAL_PORT_MONITOR.println();
            #endif

            // <cid><ip> <port>\t<length 4 ascii char><data>
            uint8_t * const ipstart = this->rx_async + 1;
            uint8_t iplen = 0;
            while (ipstart[iplen] != ' ')
              ++iplen;

            uint8_t * const portstart = ipstart + iplen + 1;
            uint8_t portlen = 0;
            while (portstart[portlen] != '\t')
              ++portlen;

            uint8_t * const lengthstart = portstart + portlen + 1;

            if (parseNumber(&this->head_frame.cid, this->rx_async, 1, 16) &&
                parseIpAddress(&this->head_frame.ip, (char*)ipstart, iplen) &&
                parseNumber(&this->head_frame.port, portstart, portlen, 10) &&
                parseNumber(&this->head_frame.length, lengthstart, 4, 10)) {

              this->head_frame.udp_server = true;

              #ifdef GS_DUMP_LINES
              SERIAL_PORT_MONITOR.print("<<| Read bulk UDP server data frame for cid ");
              SERIAL_PORT_MONITOR.print(this->head_frame.cid);
              SERIAL_PORT_MONITOR.print(" from ");
              SERIAL_PORT_MONITOR.write(ipstart, iplen);
              SERIAL_PORT_MONITOR.print(":");
              SERIAL_PORT_MONITOR.write(portstart, portlen);
              SERIAL_PORT_MONITOR.print(" containing ");
              SERIAL_PORT_MONITOR.print(this->head_frame.length);
              SERIAL_PORT_MONITOR.println(" bytes");
              #endif

              // Store the frame header and prepare to read data
              bufferFrameHeader(&this->head_frame);
              this->rx_state = GS_RX_BULK;
            } else {
              #ifdef GS_LOG_ERRORS
                SERIAL_PORT_MONITOR.print("Invalid escape sequence: <ESC>y");
                SERIAL_PORT_MONITOR.write(this->rx_async, this->rx_async_len);
                SERIAL_PORT_MONITOR.println();
              #endif
              // Revert to GS_RX_IDLE and hope for the best...
              this->rx_state = GS_RX_IDLE;
            }
          }
          break;
        }

        case GS_RX_ESC_A:
          if (--this->rx_async_left == 0) {
            #ifdef GS_DUMP_LINES
              SERIAL_PORT_MONITOR.print("<<| Read async header: <ESC>A");
              SERIAL_PORT_MONITOR.write(this->rx_async, this->rx_async_len);
              SERIAL_PORT_MONITOR.println();
            #endif
            // <Subtype><length 2 ascii char><data>
            if (parseNumber(&this->rx_async_subtype, this->rx_async, 1, 16) &&
                parseNumber(&this->rx_async_left, this->rx_async + 1, 2, 10)) {
              this->rx_state = GS_RX_ASYNC;
              this->rx_async_len = 0;
            } else {
              #ifdef GS_LOG_ERRORS
                SERIAL_PORT_MONITOR.print("Invalid escape sequence: <ESC>A");
                SERIAL_PORT_MONITOR.write(this->rx_async, this->rx_async_len);
                SERIAL_PORT_MONITOR.println();
              #endif
              // Revert to GS_RX_IDLE and hope for the best...
              this->rx_state = GS_RX_IDLE;
            }
          }
          break;

        case GS_RX_ASYNC:
          if (--this->rx_async_left == 0) {
            this->rx_state = GS_RX_IDLE;
            #ifdef GS_DUMP_LINES
              SERIAL_PORT_MONITOR.print("<<| Read async data: ");
              SERIAL_PORT_MONITOR.write(this->rx_async, this->rx_async_len);
              SERIAL_PORT_MONITOR.println();
            #endif
            if (!processAsync()) {
              #ifdef GS_LOG_ERRORS
                SERIAL_PORT_MONITOR.print("Unknown async reponse: subtype=");
                SERIAL_PORT_MONITOR.print(this->rx_async_subtype);
                SERIAL_PORT_MONITOR.print(", length=");
                SERIAL_PORT_MONITOR.print(this->rx_async_len);
                SERIAL_PORT_MONITOR.print(", data=");
                SERIAL_PORT_MONITOR.write(this->rx_async, this->rx_async_len);
                SERIAL_PORT_MONITOR.println();
              #endif
            }
            break;
          }

        // keep the compiler happy
        default:
          break;
      }
      break;

    case GS_RX_BULK:
      bufferIncomingData(c);
      if(--this->head_frame.length == 0)
        this->rx_state = GS_RX_IDLE;
      break;
  }
  return true;
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
  this->rx_data_tail += sizeof(*frame);
}

GSCore::RXFrame GSCore::getFrameHeader(cid_t cid)
{
  if (this->tail_frame.length == 0) {
    if (this->rx_data_tail != this->rx_data_head) {
      // The current frame is empty, but there is still data in rx_data.
      // Load the next frame.
     loadFrameHeader(&this->tail_frame);
    } else {
      // The buffer is empty. See if we can read more data from the
      // module.
      while (this->tail_frame.length == 0) {
        // Don't block
        if (!processIncoming(readRaw()))
          return RXFrame();
      }
    }
  }

  if (cid == ANY_CID || this->tail_frame.cid == cid)
    return this->tail_frame;
  else
    return RXFrame();
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
        this->rx_state = GS_RX_IDLE;
    }
    return c;
  }
}

void GSCore::readAndProcessAsync()
{
  // Read and process bytes until:
  //  - There are no more bytes to read.
  //  - We end up in a data packet (which we don't want to read all the
  //    way through, since it'll likely fill up our buffers.
  //
  //  Note that we always read at least one byte, so if we start out in
  //  a data packet, we'll always advance it by one byte to prevent
  //  deadlocking ourselves.
  while (processIncoming(readRaw())) {
    switch (this->rx_state) {
      case GS_RX_ESC_Z:
      case GS_RX_BULK:
        return;
      default:
        continue;
    }
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

  // This function parses a response line and has to decide wether the
  // line contains a reponse code (with special meaning) or is just a
  // line of data. There is no perfect way to do this, consider a reply
  // like "2.5.1" indicate the firmware version. If you're not careful,
  // that looks like a "2" response with "5.1" as arguments". For this
  // reason, we're very conservative with matching a response: If
  // anything is different from what we expect, return
  // GS_UNKNOWN_RESPONSE assuming that it is just arbitrary data.

  #ifdef GS_DUMP_LINES
  SERIAL_PORT_MONITOR.print("<<= ");
  SERIAL_PORT_MONITOR.write(buf, len);
  SERIAL_PORT_MONITOR.println();
  #endif

  // In non-verbose mode, command responses are an (string containing a)
  // number from "0" to "18"
  static_assert(GS_RESPONSE_MAX == 18, "processResponseLine cannot parse all responses");
  if (len >= 2 && buf[0] == '1' && buf[1] >= '0' && buf[1] <= '8') {
    args = buf + 2;
    code = (GSResponse)(10 + buf[1] - '0');
  } else if (len >= 1 && buf[0] >= '0' && buf[0] <= '9') {
    args = buf + 1;
    code = (GSResponse)(buf[0] - '0');
  } else if (len == 2 && buf[0] == 'O' && buf[1] == 'K') {
    // Also process the "OK" response, since even in non-verbose mode,
    // sending a certificate (using <ESC>W) replies with "OK" instead
    // of "0"...
    args = buf + 2;
    code = GS_SUCCESS;
  } else {
    return GS_UNKNOWN_RESPONSE;
  }

  uint8_t arg_len = buf + len - args;

  // After the digits, there should either be a space or nothing,
  // anything else indicates it is not a proper reply
  if (arg_len != 0 && args[0] != ' ')
    return GS_UNKNOWN_RESPONSE;

  switch (code) {
    // These are replies without arguments
    case GS_SUCCESS:
    case GS_FAILURE:
    case GS_EINVAL:
    case GS_ENOCID:
    case GS_EBADCID:
    case GS_ENOTSUP:
    case GS_LINK_LOST:
    case GS_ENOIP:
      // No arguments
      if (arg_len != 0)
        return GS_UNKNOWN_RESPONSE;

      return code;

    // This is a reply to a connect command with an argument. Only
    // consider it a valid reply when we're expecting it.
    case GS_CON_SUCCESS:
      // The Network Connection Manager set up its connection
      // CONNECT <CID>

      // 2 bytes of arguments
      if (arg_len != 2)
        return GS_UNKNOWN_RESPONSE;

      // No CONNECT reply expected?
      if (!connect_cid)
        return GS_UNKNOWN_RESPONSE;

      if (!parseNumber(connect_cid, args + 1, 1, 16))
        return GS_UNKNOWN_RESPONSE;

      return code;

    #ifdef GS_LOG_ERRORS
    // These are asynchronous responses and with AT+ASYNCMSGFMT=1, we
    // shouldn't be receiving them here...
    case GS_DISASSO_EVT:
    case GS_STBY_TMR_EVT:
    case GS_STBY_ALM_EVT:
    case GS_DPSLEEP_EVT:
    case GS_BOOT_UNEXPEC:
    case GS_BOOT_INTERNAL:
    case GS_BOOT_EXTERNAL:
    case GS_NWCONN_SUCCESS:
      if (arg_len > 0)
        return GS_UNKNOWN_RESPONSE;
      // fallthrough //
    case GS_SOCK_FAIL:
    case GS_ECIDCLOSE:
      if (arg_len > 2)
        return GS_UNKNOWN_RESPONSE;
        SERIAL_PORT_MONITOR.print("Received asynchronous response synchronously: ");
        SERIAL_PORT_MONITOR.write(buf, len);
        SERIAL_PORT_MONITOR.println();
      return GS_UNKNOWN_RESPONSE;
    #endif

    // Make the compiler happy
    default:
      return GS_UNKNOWN_RESPONSE;
  }
}

enum GSAsync {
  // These are asynchronous responses sent by the module. With
  // AT+ASYNCMSGFMT=1, the (ascii-hex equivalents of the) values of this
  // enum are sent as the "subtype" in <ESC>A responses.
  GS_ASYNC_SOCK_FAIL = 0x0, // "\r\nERROR: SOCKET FAILURE <CID>\r\n"
  GS_ASYNC_CON_SUCCESS = 0x1, // "\r\nCONNECT <CID>\r\n\r\nOK\r\n”
                            // or "\r\nCONNECT <server CID> <new CID> <ip> <port>\r\n"
  GS_ASYNC_ECIDCLOSE = 0x2, // "\r\nDISCONNECT <CID>\r\n"
  GS_ASYNC_DISASSO_EVT = 0x3, // “\r\n\r\nDisassociation Event\r\n\r\n”
  GS_ASYNC_STBY_TMR_EVT = 0x4, // "\r\nOut of StandBy-Timer\r\n"
  GS_ASYNC_STBY_ALM_EVT = 0x5, // "\r\n\n\rOut of StandBy-Alarm\r\n\r\n"
  GS_ASYNC_DPSLEEP_EVT = 0x6, // "\r\n\r\nOut of Deep Sleep\r\n\r\n\r\nOK\r\n"
  GS_ASYNC_BOOT_UNEXPEC = 0x7, // "\r\n\r\nUnExpected Warm Boot(Possibly Low Battery)\r\n\r\n"
  GS_ASYNC_ENOIP = 0x8, // "\r\nERROR: IP CONFIG FAIL\r\n"
  GS_ASYNC_BOOT_INTERNAL = 0x9, // "\r\nSerial2WiFi APP\r\n"
  GS_ASYNC_BOOT_EXTERNAL = 0xa, // "\r\nSerial2WiFi APP-Ext.PA\r\n"
  GS_ASYNC_FAILURE = 0xb, // "\r\nERROR\r\n"
  GS_ASYNC_NWCONN_SUCCESS = 0xc, // "\r\nNWCONN-SUCCESS\r\n"

  GS_ASYNC_MAX = GS_ASYNC_NWCONN_SUCCESS,
};


bool GSCore::processAsync()
{
  cid_t cid;
  if (this->rx_async_subtype > GS_ASYNC_MAX)
    return false;

  if (this->rx_async_len < 1)
    return false;

  // A asynchronous response looks like:
  // <ESC>A<subtype><length><data>
  // When in verbose mode, <data> is a string with the response. In
  // non-verbose mode, <data> is the subtype followed by any
  // space-separated arguments (note that with AT+ASYNCMSGFMT=0,
  // asynchronous replies use the response code from GSResponse
  // instead...). In any case, the subtype in <data> should be the
  // same as the first one, so verify that here.
  uint8_t subtype;
  if (!parseNumber(&subtype, this->rx_async, 1, 16))
    return false;

  if (subtype != this->rx_async_subtype)
    return false;

  // "arguments" to the response
  uint8_t arg_len = this->rx_async_len - 1;
  uint8_t *args = this->rx_async + 1;

  // After the digit, there should either be a space or nothing,
  // anything else indicates it is not a proper reply
  if (arg_len != 0 && args[0] != ' ')
    return false;

  switch(this->rx_async_subtype) {
    case GS_ASYNC_CON_SUCCESS:
      if (arg_len < 2)
        return false;

      if (arg_len == 2) {
        // The Network Connection Manager set up its connection
        // CONNECT <CID>
        if (!parseNumber(&cid, &args[1], 1, 16))
          return false;

        // Set connection info, even though we really only know it's
        // connected
        processConnect(cid, 0, 0, 0, true);
        return true;
      } else {
        // Incoming connection on a TCP server
        // CONNECT <server CID> <new CID> <ip> <port>,
        // TODO
        return false;
      }
    case GS_ASYNC_SOCK_FAIL:
    case GS_ASYNC_ECIDCLOSE:
      if (arg_len != 2)
        return false;

      if (!parseNumber(&cid, &args[1], 1, 16))
        return false;

      if (this->rx_async_subtype == GS_SOCK_FAIL) {
        // ERROR: SOCKET: FAILURE <CID>
        // Documentation is unclear, but experimentation shows that when
        // this happens, some data might have been lost and the
        // connection is broken.
        #ifdef GS_LOG_ERRORS
          SERIAL_PORT_MONITOR.print("Socket error on cid ");
          SERIAL_PORT_MONITOR.println(cid);
        #endif
        this->connections[cid].error = true;
      }
      processDisconnect(cid);
      return true;

    default:
      // All others do not have arguments
      if (arg_len > 0)
        return false;

      switch (this->rx_async_subtype) {
        case GS_ASYNC_FAILURE:
          // Means the Network Connection Manager has used all it's
          // retries and is giving up on setting up a L4 (TCP/UDP)
          // connection (until the next (re)association). For now, just
          // ignore.
          return false;

        case GS_ASYNC_DISASSO_EVT:
          // TODO: This means the wifi association has broken. Update our
          // state.
          processDisassociation();
          return true;

        case GS_ASYNC_STBY_TMR_EVT:
        case GS_ASYNC_STBY_ALM_EVT:
        case GS_ASYNC_DPSLEEP_EVT:
          // TODO: These are given after the wifi module is told to go
          // into standby. How to handle these? Perhaps just print some
          // debug info for now and then ignore them?
          return false;

        case GS_ASYNC_BOOT_UNEXPEC:
        case GS_ASYNC_BOOT_INTERNAL:
        case GS_ASYNC_BOOT_EXTERNAL:
          // These indicate the hardware has just reset. During
          // initialization, we expect one of these, so we silently
          // ignore it.
          if (this->initializing)
            return true;

          // TODO: Reset our state to match the hardware. Also make sure
          // to stop waiting for a reply to a command, since it will never
          // come.
          return false;

        case GS_ASYNC_NWCONN_SUCCESS:
          // This means that the Network Connection Manager has
          // succesfully associated.
          processAssociation();
          return true;

        case GS_ASYNC_ENOIP:
          // ERROR: IP CONFIG FAIL
          // Sent the DHCP renew, or DHCP lease initiated by the Network
          // Connection Manager fails.
          // Afterwards, the hardware loses its address and does not
          // retry DHCP again.
          processDisassociation();
          return true;

        default:
          // This should never happen, but the compiler gives a warning if
          // we don't handle _all_ enumeration values...
          return false;
      }

  }
}

void GSCore::processAssociation()
{
  // Did we think we're still associated? Must have missed a
  // disassciation somewhere (it seems the module doesn't always send
  // them...). Process it now, to begin with a clean slate.
  if (this->associated)
    processDisassociation();

  this->associated = true;
  // Keep track of the associated event, even when there is already a
  // disassociated event (since a re-associate should not go by
  // unnoticed
  this->events |= EVENT_ASSOCIATED;
}

void GSCore::processDisassociation()
{
  if (!this->associated)
    return;

  // If there is still an unprocessed association event, just cancel
  // that.
  if (this->events & EVENT_ASSOCIATED)
    this->events &= ~EVENT_ASSOCIATED;
  else
    this->events |= EVENT_DISASSOCIATED;

  this->associated = false;
  for (cid_t cid = 0; cid <= MAX_CID; ++cid) {
    if (this->connections[cid].connected) {
      this->connections[cid].error = true;
      processDisconnect(cid);
    }
  }
}

void GSCore::processConnect(cid_t cid, uint32_t remote_ip, uint16_t remote_port, uint16_t local_port, bool ncm)
{
  // Did we think this cid is still connected? We must have missed a
  // disconnect somewhere.
  if (this->connections[cid].connected)
    processDisconnect(cid);

  if (ncm) {
    this->ncm_auto_cid = cid;
    // Keep track of the associated event, even when there is already a
    // disconnect event (since a reconnect should not go by unnoticed
    this->events |= EVENT_NCM_CONNECTED;
  }

  this->connections[cid].remote_ip = remote_ip;
  this->connections[cid].remote_port = remote_port;
  this->connections[cid].local_port = local_port;
  this->connections[cid].error = false;
  this->connections[cid].connected = true;
}

void GSCore::processDisconnect(cid_t cid)
{
  if (!this->connections[cid].connected)
    return;

  this->connections[cid].connected = false;
  this->connections[cid].ssl = false;
  if (cid == this->ncm_auto_cid) {
    this->ncm_auto_cid = INVALID_CID;
    // If there is still an unprocessed connect event, just cancel that.
    if (this->events & EVENT_NCM_CONNECTED)
      this->events &= ~EVENT_NCM_CONNECTED;
    else
      this->events |= EVENT_NCM_DISCONNECTED;
  }
}

/*******************************************************
 * Static helper methods
 *******************************************************/
bool GSCore::parseNumber(uint8_t *out, const uint8_t *buf, uint8_t len, uint8_t base)
{
  uint16_t tmp;
  if (!parseNumber(&tmp, buf, len, base))
    return false;

  if (tmp > max_for_type(__typeof__(*out)))
    return false;

  *out = tmp;
  return true;
}

bool GSCore::parseNumber(uint16_t *out, const uint8_t *buf, uint8_t len, uint8_t base)
{
  if (base < 2 || base > 36)
    return false;

  uint16_t result = 0;
  while (len--) {
    if (result > max_for_type(__typeof__(*out)) / 10)
      return false;

    result *= base;
    if (*buf >= '0' && *buf <= '9')
      result += (*buf - '0');
    else if (*buf >= 'a' && *buf <= 'z')
      result += 10 + (*buf - 'a');
    else if (*buf >= 'A' && *buf <= 'Z')
      result += 10 + (*buf - 'A');
    else
      return false;
    buf++;
  }
  *out = result;

  return true;
}

// vim: set sw=2 sts=2 expandtab:
