#include <SPI.h>
#include <RH_RF95.h>
#include <RHMesh.h>
#include <assert.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "MeshRadio.h"
#include "configuration.h"

// Change to 434.0 or other frequency, must match RX's freq!
#define RF95_FREQ 915.0

/**
 * get our starting (provisional) nodenum from flash.  But check first if anyone else is using it, by trying to send a message to it (arping)
 */
NodeNum getDesiredNodeNum()
{
  uint8_t dmac[6];
  esp_efuse_mac_get_default(dmac);

  // FIXME not the right way to guess node numes
  uint8_t r = dmac[5];
  assert(r != 0xff); // It better not be the broadcast address
  return r;
}

MeshRadio::MeshRadio(MemoryPool<MeshPacket> &_pool, PointerQueue<MeshPacket> &_rxDest)
    : rf95(NSS_GPIO, DIO0_GPIO),
      manager(rf95, getDesiredNodeNum()),
      pool(_pool),
      rxDest(_rxDest),
      txQueue(MAX_TX_QUEUE)
{
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
    Serial.println("LoRa radio init failed");
    Serial.println("Uncomment '#define SERIAL_DEBUG' in RH_RF95.cpp for detailed debug info");
    return false;
  }

  Serial.println("LoRa radio init OK!");

  // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
  if (!rf95.setFrequency(RF95_FREQ))
  {
    Serial.println("setFrequency failed");
    while (1)
      ;
  }
  Serial.print("Set Freq to: ");
  Serial.println(RF95_FREQ);

  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then
  // you can set transmitter powers from 5 to 23 dBm:
  // FIXME - can we do this?  It seems to be in the Heltec board.
  rf95.setTxPower(23, false);

  return true;
}

ErrorCode MeshRadio::send(MeshPacket *p)
{
  Serial.println("enquing packet for sending on mesh");
  return txQueue.enqueue(p, 0); // nowait
}

ErrorCode MeshRadio::sendTo(NodeNum dest, const uint8_t *buf, size_t len)
{
  Serial.printf("mesh sendTo %d bytes to %d\n", len, dest);
  // FIXME - for now we do all packets as broadcast
  dest = NODENUM_BROADCAST;

  assert(len <= 255); // Make sure we don't overflow the tiny max packet size 

  // Note: we don't use sendToWait here because we don't want to wait and for the time being don't require
  // reliable delivery
  // return manager.sendtoWait((uint8_t *) buf, len, dest);
  return manager.sendto((uint8_t *)buf, len, dest) ? ERRNO_OK : ERRNO_UNKNOWN;
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
    Serial.printf("Received packet from mesh src=%d,dest=%d,id=%d,len=%d\n", srcaddr, destaddr, id, rxlen);

    MeshPacket *mp = pool.allocZeroed();
    assert(mp); // FIXME

    SubPacket *p = &mp->payload;

    mp->from = srcaddr;
    mp->to = destaddr;
    pb_istream_t stream = pb_istream_from_buffer(radiobuf, rxlen);
    if (!pb_decode(&stream, SubPacket_fields, p))
    {
      Serial.printf("Error: can't decode SubPacket %s\n", PB_GET_ERROR(&stream));
      pool.release(mp);
    }
    else
    {
      // parsing was successful, queue for our recipient
      mp->has_payload = true;
      int res = rxDest.enqueue(mp, 0); // NOWAIT - fixme, if queue is full, delete older messages
      assert(res == pdTRUE);
    }
  }

  // Poll to see if we need to send any packets
  MeshPacket *txp = txQueue.dequeuePtr(0); // nowait
  if (txp)
  {
    Serial.println("sending queued packet on mesh");
    assert(txp->has_payload);

    pb_ostream_t stream = pb_ostream_from_buffer(radiobuf, sizeof(radiobuf));
    if (!pb_encode(&stream, SubPacket_fields, &txp->payload))
    {
      Serial.printf("Error: can't encode SubPacket %s\n", PB_GET_ERROR(&stream));
    }
    else
    {
      int res = sendTo(txp->to, radiobuf, stream.bytes_written);
      assert(res == ERRNO_OK);
    }

    pool.release(txp);
  }
}
