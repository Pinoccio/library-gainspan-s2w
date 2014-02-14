#include "GSClient.h"
#include "util.h"

int GSClient::connect(IPAddress ip, uint16_t port)
{
  if (connected())
    return false;

  GSModule::cid_t cid = gs.connectTcp(ip, port);
  if (cid == GSModule::INVALID_CID)
    return false;

  this->cid = cid;
  return true;
}

int GSClient::connect(const char *host, uint16_t port)
{
  // TODO
  return false;
}

size_t GSClient::write(uint8_t c)
{
  return write(&c, sizeof(c));
}

size_t GSClient::write(const uint8_t *buf, size_t size)
{
  if (!gs.writeData(this->cid, buf, size))
    return 0;
  return size;
}

int GSClient::available()
{
  return gs.availableData(this->cid);
}

int GSClient::read()
{
  return gs.readData(this->cid);
}

int GSClient::read(uint8_t *buf, size_t size)
{
  return gs.readData(this->cid, buf, size);
}

int GSClient::peek()
{
  return gs.peekData(this->cid);
}

void GSClient::flush()
{
  // Nothing todo, we don't keep any buffers
}

void GSClient::stop()
{
  gs.disconnect(this->cid);
}

uint8_t GSClient::connected()
{
  if (this->cid == GSModule::INVALID_CID)
    return false;
  return gs.getConnectionInfo(this->cid).connected && gs.getConnectionInfo(this->cid).ssl;
}

GSClient::operator bool()
{
  return (this->cid != GSModule::INVALID_CID);
}

GSClient& GSClient::operator =(GSCore::cid_t cid)
{
  this->cid = cid;
  return *this;
}

bool GSClient::enableTls(const char *certname)
{
  return gs.enableTls(this->cid, certname);
}
// vim: set sw=2 sts=2 expandtab:
