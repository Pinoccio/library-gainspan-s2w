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

    // Include other overloads of write
    using Print::write;

    /****************************************************************
     * Gainspan-specific stuff
     ****************************************************************/
    virtual bool enableTls(const char *certname);

  protected:
    GSModule &gs;
    GSModule::cid_t cid;

};

#endif // _GS_CLIENT_H

// vim: set sw=2 sts=2 expandtab:
