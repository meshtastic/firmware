/*
 * @Author: ljk lcflyr@qq.com
 * @Date: 2025-05-26 16:16:45
 * @LastEditors: ljk lcflyr@qq.com
 * @LastEditTime: 2025-05-26 16:33:36
 * @FilePath: \meshtastic_firmware\src\nimble\NimbleBluetooth.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#pragma once
#include "BluetoothCommon.h"

class NimbleBluetooth : BluetoothApi
{
  public:
    void setup();
    void shutdown();
    void deinit();
    void clearBonds();
    bool isActive();
    bool isConnected();
    int getRssi();
    void sendLog(const uint8_t *logMessage, size_t length);
    void Send_GPWPL(uint32_t node, char* name,int32_t latitude_i,int32_t longitude_i);

  private:
    void setupService();
    void startAdvertising();
};

void setBluetoothEnable(bool enable);
void clearNVS();