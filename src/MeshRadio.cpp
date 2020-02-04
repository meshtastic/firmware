#include <SPI.h>
#include <RH_RF95.h>
#include <RHMesh.h>
#include <assert.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "MeshRadio.h"
#include "configuration.h"
#include "NodeDB.h"

// Change to 434.0 or other frequency, must match RX's freq!  FIXME, choose a better default value
#define RF95_FREQ_US 915.0f

RadioConfig radioConfig = RadioConfig_init_zero;

MeshRadio::MeshRadio(MemoryPool<MeshPacket> &_pool, PointerQueue<MeshPacket> &_rxDest)
    : rf95(NSS_GPIO, DIO0_GPIO),
      manager(rf95, nodeDB.getNodeNum()),
      pool(_pool),
      rxDest(_rxDest),
      txQueue(MAX_TX_QUEUE)
{
  radioConfig.tx_power = 23;
  radioConfig.center_freq = RF95_FREQ_US; // FIXME, pull this config from flash
}

bool MeshRadio::init()
{
  pinMode(RESET_GPIO, OUTPUT); // Deassert reset
  digitalWrite(RESET_GPIO, HIGH);

  // pulse reset
  digitalWrite(RESET_GPIO, LOW);
  delay(10);
  digitalWrite(RESET_GPIO, HIGH);
  delay(10);

  if (!manager.init())
  {
    DEBUG_MSG("LoRa radio init failed\n");
    DEBUG_MSG("Uncomment '#define SERIAL_DEBUG' in RH_RF95.cpp for detailed debug info\n");
    return false;
  }

  DEBUG_MSG("LoRa radio init OK!\n");

  // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
  if (!rf95.setFrequency(radioConfig.center_freq))
  {
    DEBUG_MSG("setFrequency failed\n");
    assert(0); // fixme panic
  }
  DEBUG_MSG("Set Freq to: %f\n", radioConfig.center_freq);

  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then
  // you can set transmitter powers from 5 to 23 dBm:
  // FIXME - can we do this?  It seems to be in the Heltec board.
  rf95.setTxPower(radioConfig.tx_power, false);

  return true;
}

ErrorCode MeshRadio::send(MeshPacket *p)
{
  DEBUG_MSG("enquing packet for sending on mesh\n");
  return txQueue.enqueue(p, 0); // nowait
}

ErrorCode MeshRadio::sendTo(NodeNum dest, const uint8_t *buf, size_t len)
{
  DEBUG_MSG("mesh sendTo %d bytes to %d\n", len, dest);
  // FIXME - for now we do all packets as broadcast
  dest = NODENUM_BROADCAST;

  assert(len <= 255); // Make sure we don't overflow the tiny max packet size

  // Note: we don't use sendToWait here because we don't want to wait and for the time being don't require
  // reliable delivery
  // return manager.sendtoWait((uint8_t *) buf, len, dest);
  return manager.sendto((uint8_t *)buf, len, dest) ? ERRNO_OK : ERRNO_UNKNOWN;
}

/// enqueue a received packet in rxDest
void MeshRadio::handleReceive(MeshPacket *mp)
{
  int res = rxDest.enqueue(mp, 0); // NOWAIT - fixme, if queue is full, delete older messages
  assert(res == pdTRUE);
}


void MeshRadio::loop()
{
  // FIXME read from radio with recvfromAckTimeout

#if 0
static int16_t packetnum = 0;  // packet counter, we increment per xmission

  char radiopacket[20] = "Hello World #      ";
  sprintf(radiopacket, "hello %d", packetnum++);
  
  assert(sendTo(NODENUM_BROADCAST, (uint8_t *)radiopacket, sizeof(radiopacket)) == ERRNO_OK);
#endif

  /// A temporary buffer used for sending/receving packets, sized to hold the biggest buffer we might need
  static uint8_t radiobuf[SubPacket_size];
  uint8_t rxlen;
  uint8_t srcaddr, destaddr, id, flags;

  // Poll to see if we've received a packet
  if (manager.recvfromAckTimeout(radiobuf, &rxlen, 0, &srcaddr, &destaddr, &id, &flags))
  {
    // We received a packet
    DEBUG_MSG("Received packet from mesh src=%d,dest=%d,id=%d,len=%d\n", srcaddr, destaddr, id, rxlen);

    MeshPacket *mp = pool.allocZeroed();
    assert(mp); // FIXME

    SubPacket *p = &mp->payload;

    mp->from = srcaddr;
    mp->to = destaddr;
    if (!pb_decode_from_bytes(radiobuf, rxlen, SubPacket_fields, p))
    {
      pool.release(mp);
    }
    else
    {
      // parsing was successful, queue for our recipient
      mp->has_payload = true;
      handleReceive(mp);
    }
  }

  // Poll to see if we need to send any packets
  MeshPacket *txp = txQueue.dequeuePtr(0); // nowait
  if (txp)
  {
    DEBUG_MSG("sending queued packet on mesh\n");
    assert(txp->has_payload);

    size_t numbytes = pb_encode_to_bytes(radiobuf, sizeof(radiobuf), SubPacket_fields, &txp->payload);

    int res = sendTo(txp->to, radiobuf, numbytes);
    assert(res == ERRNO_OK);

    bool loopbackTest = false; // if true we will pretend to receive any packets we just sent
    if (loopbackTest)
      handleReceive(txp);
    else
      pool.release(txp);
  }
}
