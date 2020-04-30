#include "RadioLibInterface.h"
#include <configuration.h>

// FIXME, we default to 4MHz SPI, SPI mode 0, check if the datasheet says it can really do that
static SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

RadioLibInterface::RadioLibInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                     SPIClass &spi, PhysicalLayer *_iface)
    : module(cs, irq, rst, busy, spi, spiSettings), iface(*_iface)
{
    assert(!instance); // We assume only one for now
    instance = this;
}

void INTERRUPT_ATTR RadioLibInterface::isrRxLevel0()
{
    instance->pending = ISR_RX;
    instance->disableInterrupt();
}

void INTERRUPT_ATTR RadioLibInterface::isrTxLevel0()
{
    instance->pending = ISR_TX;
    instance->disableInterrupt();
}

/** Our ISR code currently needs this to find our active instance
 */
RadioLibInterface *RadioLibInterface::instance;

/**
 * Convert our modemConfig enum into wf, sf, etc...
 */
void RadioLibInterface::applyModemConfig()
{
    switch (modemConfig) {
    case RH_RF95::Bw125Cr45Sf128: ///< Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on. Default medium range
        bw = 125;
        cr = 5;
        sf = 7;
        break;
    case RH_RF95::Bw500Cr45Sf128: ///< Bw = 500 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on. Fast+short range
        bw = 500;
        cr = 5;
        sf = 7;
        break;
    case RH_RF95::Bw31_25Cr48Sf512: ///< Bw = 31.25 kHz, Cr = 4/8, Sf = 512chips/symbol, CRC on. Slow+long range
        bw = 31.25;
        cr = 8;
        sf = 9;
        break;
    case RH_RF95::Bw125Cr48Sf4096:
        bw = 125;
        cr = 8;
        sf = 12;
        break;
    default:
        assert(0); // Unknown enum
    }
}

/// Send a packet (possibly by enquing in a private fifo).  This routine will
/// later free() the packet to pool.  This routine is not allowed to stall because it is called from
/// bluetooth comms code.  If the txmit queue is empty it might return an error
ErrorCode RadioLibInterface::send(MeshPacket *p)
{
    // We wait _if_ we are partially though receiving a packet (rather than just merely waiting for one).
    // To do otherwise would be doubly bad because not only would we drop the packet that was on the way in,
    // we almost certainly guarantee no one outside will like the packet we are sending.
    if (canSendImmediately()) {
        // if the radio is idle, we can send right away
        DEBUG_MSG("immediate send on mesh fr=0x%x,to=0x%x,id=%d\n (txGood=%d,rxGood=%d,rxBad=%d)\n", p->from, p->to, p->id, -1,
                  -1, -1);

        startSend(p);
        return ERRNO_OK;
    } else {
        DEBUG_MSG("enqueuing packet for send from=0x%x, to=0x%x\n", p->from, p->to);
        ErrorCode res = txQueue.enqueue(p, 0) ? ERRNO_OK : ERRNO_UNKNOWN;

        if (res != ERRNO_OK) // we weren't able to queue it, so we must drop it to prevent leaks
            packetPool.release(p);

        return res;
    }
}

void RadioLibInterface::loop()
{
    PendingISR wasPending = pending; // atomic read
    if (wasPending) {
        pending = ISR_NONE; // If the flag was set, it is _guaranteed_ the ISR won't be running, because it masked itself

        if (wasPending == ISR_TX)
            handleTransmitInterrupt();
        else if (wasPending == ISR_RX)
            handleReceiveInterrupt();
        else
            assert(0);

        // First send any outgoing packets we have ready
        MeshPacket *txp = txQueue.dequeuePtr(0);
        if (txp)
            startSend(txp);
        else {
            // Nothing to send, let's switch back to receive mode
            // FIXME - RH_RF95::setModeRx();
        }
    }
}

void RadioLibInterface::handleTransmitInterrupt()
{
    assert(sendingPacket); // Were we sending?

    // FIXME - check result code from ISR

    // We are done sending that packet, release it
    packetPool.release(sendingPacket);
    sendingPacket = NULL;
    // DEBUG_MSG("Done with send\n");
}

void RadioLibInterface::handleReceiveInterrupt()
{
    // FIXME
}

#if 0
// After doing standard behavior, check to see if a new packet arrived or one was sent and start a new send or receive as
// necessary
void CustomRF95::handleInterrupt()
{
    RH_RF95::handleInterrupt();
    enableInterrupt(); // Let ISR run again

    if (_mode == RHModeIdle) // We are now done sending or receiving
    {

        // If we just finished receiving a packet, forward it into a queue
        if (_rxBufValid) {
            // We received a packet

            // Skip the 4 headers that are at the beginning of the rxBuf
            size_t payloadLen = _bufLen - RH_RF95_HEADER_LEN;
            uint8_t *payload = _buf + RH_RF95_HEADER_LEN;

            // FIXME - throws exception if called in ISR context: frequencyError() - probably the floating point math
            int32_t freqerr = -1, snr = lastSNR();
            // DEBUG_MSG("Received packet from mesh src=0x%x,dest=0x%x,id=%d,len=%d rxGood=%d,rxBad=%d,freqErr=%d,snr=%d\n",
            //          srcaddr, destaddr, id, rxlen, rf95.rxGood(), rf95.rxBad(), freqerr, snr);

            MeshPacket *mp = packetPool.allocZeroed();

            SubPacket *p = &mp->payload;

            mp->from = _rxHeaderFrom;
            mp->to = _rxHeaderTo;
            mp->id = _rxHeaderId;

            //_rxHeaderId = _buf[2];
            //_rxHeaderFlags = _buf[3];

            // If we already have an entry in the DB for this nodenum, goahead and hide the snr/freqerr info there.
            // Note: we can't create it at this point, because it might be a bogus User node allocation.  But odds are we will
            // already have a record we can hide this debugging info in.
            NodeInfo *info = nodeDB.getNode(mp->from);
            if (info) {
                info->snr = snr;
                info->frequency_error = freqerr;
            }

            if (!pb_decode_from_bytes(payload, payloadLen, SubPacket_fields, p)) {
                packetPool.release(mp);
            } else {
                // parsing was successful, queue for our recipient
                mp->has_payload = true;

                deliverToReceiver(mp);
            }

            clearRxBuf(); // This message accepted and cleared
        }

        handleIdleISR();
    }
}
#endif

/** start an immediate transmit */
void RadioLibInterface::startSend(MeshPacket *txp)
{
    size_t numbytes = beginSending(txp);

    int res = iface.startTransmit(radiobuf, numbytes);
    assert(res);

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrTxLevel0);
}

/**
 *
 *
 *
// include the library


// save transmission state between loops
int transmissionState = ERR_NONE;

void setup() {
  Serial.begin(9600);

  // initialize SX1262 with default settings
  Serial.print(F("[SX1262] Initializing ... "));
  // carrier frequency:           434.0 MHz
  // bandwidth:                   125.0 kHz
  // spreading factor:            9
  // coding rate:                 7
  // sync word:                   0x12 (private network)
  // output power:                14 dBm
  // current limit:               60 mA
  // preamble length:             8 symbols
  // TCXO voltage:                1.6 V (set to 0 to not use TCXO)
  // regulator:                   DC-DC (set to true to use LDO)
  // CRC:                         enabled
  int state = lora.begin();
  if (state == ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true);
  }

  // set the function that will be called
  // when packet transmission is finished
  lora.setDio1Action(setFlag);

  // start transmitting the first packet
  Serial.print(F("[SX1262] Sending first packet ... "));

  // you can transmit C-string or Arduino string up to
  // 256 characters long
  transmissionState = lora.startTransmit("Hello World!");

  // you can also transmit byte array up to 256 bytes long

    byte byteArr[] = {0x01, 0x23, 0x45, 0x67,
                      0x89, 0xAB, 0xCD, 0xEF};
    state = lora.startTransmit(byteArr, 8);

}

// flag to indicate that a packet was sent
volatile bool transmittedFlag = false;

// disable interrupt when it's not needed
volatile bool enableInterrupt = true;

// this function is called when a complete packet
// is transmitted by the module
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
void setFlag(void)
{
    // check if the interrupt is enabled
    if (!enableInterrupt) {
        return;
    }

    // we sent a packet, set the flag
    transmittedFlag = true;
}

void loop()
{
    // check if the previous transmission finished
    if (transmittedFlag) {
        // disable the interrupt service routine while
        // processing the data
        enableInterrupt = false;

        // reset flag
        transmittedFlag = false;

        if (transmissionState == ERR_NONE) {
            // packet was successfully sent
            Serial.println(F("transmission finished!"));

            // NOTE: when using interrupt-driven transmit method,
            //       it is not possible to automatically measure
            //       transmission data rate using getDataRate()

        } else {
            Serial.print(F("failed, code "));
            Serial.println(transmissionState);
        }

        // wait a second before transmitting again
        delay(1000);

        // send another one
        Serial.print(F("[SX1262] Sending another packet ... "));

        // you can transmit C-string or Arduino string up to
        // 256 characters long
        transmissionState = lora.startTransmit("Hello World!");

        // you can also transmit byte array up to 256 bytes long

          byte byteArr[] = {0x01, 0x23, 0x45, 0x67,
                            0x89, 0xAB, 0xCD, 0xEF};
          int state = lora.startTransmit(byteArr, 8);


// we're ready to send more packets,
// enable interrupt service routine
enableInterrupt = true;
}
}

// this function is called when a complete packet
// is received by the module
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
void setFlag(void)
{
    // check if the interrupt is enabled
    if (!enableInterrupt) {
        return;
    }

    // we got a packet, set the flag
    receivedFlag = true;
}

void loop()
{
    // check if the flag is set
    if (receivedFlag) {
        // disable the interrupt service routine while
        // processing the data
        enableInterrupt = false;

        // reset flag
        receivedFlag = false;

        // you can read received data as an Arduino String
        String str;
        int state = lora.readData(str);

        // you can also read received data as byte array

          byte byteArr[8];
          int state = lora.readData(byteArr, 8);


if (state == ERR_NONE) {
    // packet was successfully received
    Serial.println(F("[SX1262] Received packet!"));

    // print data of the packet
    Serial.print(F("[SX1262] Data:\t\t"));
    Serial.println(str);

    // print RSSI (Received Signal Strength Indicator)
    Serial.print(F("[SX1262] RSSI:\t\t"));
    Serial.print(lora.getRSSI());
    Serial.println(F(" dBm"));

    // print SNR (Signal-to-Noise Ratio)
    Serial.print(F("[SX1262] SNR:\t\t"));
    Serial.print(lora.getSNR());
    Serial.println(F(" dB"));

} else if (state == ERR_CRC_MISMATCH) {
    // packet was received, but is malformed
    Serial.println(F("CRC error!"));

} else {
    // some other error occurred
    Serial.print(F("failed, code "));
    Serial.println(state);
}

// put module back to listen mode
lora.startReceive();

// we're ready to receive more packets,
// enable interrupt service routine
enableInterrupt = true;
}
}
*/