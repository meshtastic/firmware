#include "./HINK_E0213A367.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

/**
 * @brief 重写基类的configFullscreen，但内容为空。
 * 目的是阻止基类中任何可能冲突的初始化逻辑被执行。
 */
void HINK_E0213A367::configFullscreen()
{
    // 故意留空，以覆盖并禁用基类的实现。
}

/**
 * @brief [最终修正版 V] 基于您的代码，修正最后一处错误。
 */
void HINK_E0213A367::configScanning()
{
    // --- Y-Axis Commands (垂直方向配置) ---
    // 您的Y轴设置是正确的，我们予以保留。
    sendCommand(0x01);
    sendData(0xF9); 
    sendData(0x00);
    
    sendCommand(0x45);
    sendData(0xF9); 
    sendData(0x00); 
    //sendData(0x00); 
   // sendData(0x00); 

    sendCommand(0x4F);
    sendData(0xF9);
    //sendData(0x00);
    
    // --- X-Axis Commands (水平方向配置) ---
    // 您的方向和窗口设置是正确的，我们予以保留。
    sendCommand(0x11);
    sendData(0x00); 

    sendCommand(0x44);
    sendData(0x0F); 
    sendData(0x00); 

    // [!] 关键修正: 修正X轴起始光标位置。
    // 因为扫描方向是X递减，所以光标必须从最右边(地址15)开始。
    sendCommand(0x4E);
    sendData(0x0F); // <-- 从 0x00 修改为 0x0F
}

// 指定用于控制像素移动电压序列的信息
void HINK_E0213A367::configWaveform()
{
    // 此部分函数与官方C代码一致，无需修改
    sendCommand(0x3C);
    sendData(0x01);

    sendCommand(0x37);
    sendData(0x40);
    sendData(0x80);
    sendData(0x03);
    sendData(0x0E);
}

// 描述显示控制器在刷新期间执行的事件序列
void HINK_E0213A367::configUpdateSequence()
{
    // 此部分函数与官方C代码一致，无需修改
    switch (updateType) {
    case FAST: // 快速刷新 / 局部刷新
        sendCommand(0x21);
        sendData(0x00);
        sendCommand(0x3C);
        sendData(0x81);
        sendCommand(0x18);
        sendData(0x80);
        sendCommand(0x22);
        sendData(0xFF);
        break;

    case FULL: // 全屏刷新
    default:
        sendCommand(0x4F);
        sendData(0xF9);
        sendCommand(0x4E);
        sendData(0x0F); 
        sendCommand(0x21);
        sendData(0x40);
        sendCommand(0x18);
        sendData(0x80);
        sendCommand(0x22);
        sendData(0xF7);
        break;
    }
}

// 在刷新操作开始后，使用Meshtastic的常规线程代码周期性地轮询显示器以检查其是否完成
void HINK_E0213A367::detachFromUpdate()
{
    // 此部分逻辑与原版兼容，无需修改
    switch (updateType) {
    case FAST:
        return beginPolling(50, 500);
    case FULL:
    default:
        return beginPolling(100, 1000);
    }
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
