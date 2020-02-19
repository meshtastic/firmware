#include <SPI.h>
#include "RH_RF95.h"
#include <RHMesh.h>
#include <assert.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "MeshRadio.h"
#include "configuration.h"
#include "NodeDB.h"

#define DEFAULT_CHANNEL_NUM 3 // we randomly pick one


/// 16 bytes of random PSK for our _public_ default channel that all devices power up on
static const uint8_t defaultpsk[] = { 0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0xbf };

/**
 * ## LoRaWAN for North America

LoRaWAN defines 64, 125 kHz channels from 902.3 to 914.9 MHz increments.

The maximum output power for North America is +30 dBM.

The band is from 902 to 928 MHz. It mentions channel number and its respective channel frequency. All the 13 channels are separated by 2.16 MHz with respect to the adjacent channels.  
Channel zero starts at 903.08 MHz center frequency.
*/

MeshRadio::MeshRadio(MemoryPool<MeshPacket> &_pool, PointerQueue<MeshPacket> &_rxDest)
    : rf95(NSS_GPIO, DIO0_GPIO),
      manager(rf95),
      pool(_pool),
      rxDest(_rxDest),
      txQueue(MAX_TX_QUEUE)
{
  myNodeInfo.num_channels = NUM_CHANNELS;

  //radioConfig.modem_config = RadioConfig_ModemConfig_Bw125Cr45Sf128;  // medium range and fast
  //channelSettings.modem_config = ChannelSettings_ModemConfig_Bw500Cr45Sf128;  // short range and fast, but wide bandwidth so incompatible radios can talk together
  channelSettings.modem_config = ChannelSettings_ModemConfig_Bw125Cr48Sf4096; // slow and long range

  channelSettings.tx_power = 23;
  channelSettings.channel_num = DEFAULT_CHANNEL_NUM;
  memcpy(&channelSettings.psk, &defaultpsk, sizeof(channelSettings.psk));
  strcpy(channelSettings.name, "Default");
  // Can't print strings this early - serial not setup yet
  // DEBUG_MSG("Set meshradio defaults name=%s\n", channelSettings.name);
}

bool MeshRadio::init()
{
  DEBUG_MSG("Starting meshradio init...\n");

#ifdef RESET_GPIO
  pinMode(RESET_GPIO, OUTPUT); // Deassert reset
  digitalWrite(RESET_GPIO, HIGH);

  // pulse reset
  digitalWrite(RESET_GPIO, LOW);
  delay(10);
  digitalWrite(RESET_GPIO, HIGH);
  delay(10);
#endif

  manager.setThisAddress(nodeDB.getNodeNum()); // Note: we must do this here, because the nodenum isn't inited at constructor time.

  if (!manager.init())
  {
    DEBUG_MSG("LoRa radio init failed\n");
    DEBUG_MSG("Uncomment '#define SERIAL_DEBUG' in RH_RF95.cpp for detailed debug info\n");
    return false;
  }

  // not needed - defaults on
  // rf95.setPayloadCRC(true);

  reloadConfig();

  return true;
}

void MeshRadio::reloadConfig()
{
  rf95.setModeIdle();

  // Set up default configuration
  // No Sync Words in LORA mode.
  rf95.setModemConfig((RH_RF95::ModemConfigChoice)channelSettings.modem_config); // Radio default
                                                                             //    setModemConfig(Bw125Cr48Sf4096); // slow and reliable?
  // rf95.setPreambleLength(8);           // Default is 8

  assert(channelSettings.channel_num < NUM_CHANNELS); // If the phone tries to tell us to use an illegal channel then panic 

  // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
  float center_freq = CH0 + CH_SPACING * channelSettings.channel_num;
  if (!rf95.setFrequency(center_freq))
  {
    DEBUG_MSG("setFrequency failed\n");
    assert(0); // fixme panic
  }

  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then
  // you can set transmitter powers from 5 to 23 dBm:
  // FIXME - can we do this?  It seems to be in the Heltec board.
  rf95.setTxPower(channelSettings.tx_power, false);

  DEBUG_MSG("Set radio: name=%s. config=%u, ch=%d, txpower=%d\n", channelSettings.name, channelSettings.modem_config, channelSettings.channel_num, channelSettings.tx_power);
}


void MeshRadio::sleep() {
  // we no longer care about interrupts from this device 
  rf95.prepareDeepSleep();

  // FIXME - leave the device state in rx mode instead
  rf95.sleep();
}

ErrorCode MeshRadio::send(MeshPacket *p)
{
  DEBUG_MSG("enquing packet for send from=0x%x, to=0x%x\n", p->from, p->to);
  return txQueue.enqueue(p, 0); // nowait
}

ErrorCode MeshRadio::sendTo(NodeNum dest, const uint8_t *buf, size_t len)
{
  // We must do this before each send, because we might have just changed our nodenum
  manager.setThisAddress(nodeDB.getNodeNum()); // Note: we must do this here, because the nodenum isn't inited at constructor time.

  assert(len <= 251); // Make sure we don't overflow the tiny max packet size

  uint32_t start = millis();
  // Note: we don't use sendToWait here because we don't want to wait and for the time being don't require
  // reliable delivery
  // return manager.sendtoWait((uint8_t *) buf, len, dest);
  ErrorCode res = manager.sendto((uint8_t *)buf, len, dest) ? ERRNO_OK : ERRNO_UNKNOWN;

  // FIXME, we have to wait for sending to complete before freeing the buffer, otherwise it might get wiped
  // instead just have the radiohead layer understand queues.
  if (res == ERRNO_OK)
    manager.waitPacketSent();

  DEBUG_MSG("mesh sendTo %d bytes to 0x%x (%lu msecs)\n", len, dest, millis() - start);

  return res;
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
  #define MAX_RHPACKETLEN 251
  static uint8_t radiobuf[MAX_RHPACKETLEN];
  uint8_t rxlen;
  uint8_t srcaddr, destaddr, id, flags;

  // Poll to see if we've received a packet
  //   if (manager.recvfromAckTimeout(radiobuf, &rxlen, 0, &srcaddr, &destaddr, &id, &flags))
  // prefill rxlen with the max length we can accept - very important
  rxlen = (uint8_t) MAX_RHPACKETLEN;
  if (manager.recvfrom(radiobuf, &rxlen, &srcaddr, &destaddr, &id, &flags))
  {
    // We received a packet
    int32_t freqerr = rf95.frequencyError(), snr = rf95.lastSNR();
    DEBUG_MSG("Received packet from mesh src=0x%x,dest=0x%x,id=%d,len=%d rxGood=%d,rxBad=%d,freqErr=%d,snr=%d\n",
              srcaddr, destaddr, id, rxlen, rf95.rxGood(), rf95.rxBad(), freqerr, snr);

    MeshPacket *mp = pool.allocZeroed();

    SubPacket *p = &mp->payload;

    mp->from = srcaddr;
    mp->to = destaddr;

    // If we already have an entry in the DB for this nodenum, goahead and hide the snr/freqerr info there.
    // Note: we can't create it at this point, because it might be a bogus User node allocation.  But odds are we will
    // already have a record we can hide this debugging info in.
    NodeInfo *info = nodeDB.getNode(mp->from);
    if (info)
    {
      info->snr = snr;
      info->frequency_error = freqerr;
    }

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
    DEBUG_MSG("sending queued packet on mesh (txGood=%d,rxGood=%d,rxBad=%d)\n", rf95.txGood(), rf95.rxGood(), rf95.rxBad());
    assert(txp->has_payload);

    size_t numbytes = pb_encode_to_bytes(radiobuf, sizeof(radiobuf), SubPacket_fields, &txp->payload);

    int res = sendTo(txp->to, radiobuf, numbytes);
    assert(res == ERRNO_OK);

    bool loopbackTest = false; // if true we will pretend to receive any packets we just sent
    if (loopbackTest)
      handleReceive(txp);
    else
      pool.release(txp);

    DEBUG_MSG("Done with send\n");
  }
}
