/***********************************************************************************************************************
 * based on XMODEM implementation by Georges Menie (www.menie.org)
 ***********************************************************************************************************************
 * Copyright 2001-2019 Georges Menie (www.menie.org)
 * All rights reserved.
 *
 * Adapted for protobuf encapsulation. this is not really Xmodem any more.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of California, Berkeley nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **********************************************************************************************************************/

#pragma once

#include "FSCommon.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/xmodem.pb.h"

#define MAXRETRANS 25

class XModemAdapter
{
  public:
    // Called when we put a fragment in the outgoing memory
    Observable<uint32_t> packetReady;

    XModemAdapter();

    void handlePacket(meshtastic_XModem xmodemPacket);
    meshtastic_XModem getForPhone();
    void resetForPhone();

  private:
    bool isReceiving = false;
    bool isTransmitting = false;
    bool isEOT = false;

    int retrans = MAXRETRANS;

    uint16_t packetno = 0;

#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    File file = File(FSCom);
#else
    File file;
#endif

    char filename[sizeof(meshtastic_XModem_buffer_t::bytes)] = {0};

  protected:
    meshtastic_XModem xmodemStore = meshtastic_XModem_init_zero;
    unsigned short crc16_ccitt(const pb_byte_t *buffer, int length);
    int check(const pb_byte_t *buf, int sz, unsigned short tcrc);
    void sendControl(meshtastic_XModem_Control c);
};

extern XModemAdapter xModem;
