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
    // 所有初始化逻辑已全部整合到下面的 configScanning() 函数中。
}

/**
 * @brief [最终完整版] 配置屏幕的所有核心参数。
 * 此版本在修正X轴镜像的基础上，完整定义了Y轴的显示窗口，解决了底部像素丢失的问题。
 */
void HINK_E0213A367::configScanning()
{
    // --- Y-Axis Commands (垂直方向配置) ---
    // 0x01: Driver output control
    sendCommand(0x01);
    sendData(0xF9); // MUX line setting: 249 -> 250 lines
    sendData(0x00);
    
    // 0x45: set RAM Y address start/end
    // [!] 关键修正：明确定义Y轴的完整窗口范围 (249 down to 0)。
    // 这解决了底部像素丢失的问题。
    sendCommand(0x45);
    sendData(0xF9); // Y Start Address (low byte) = 249
    sendData(0x00); // Y Start Address (high byte) = 0
    sendData(0x00); // Y End Address (low byte) = 0
    sendData(0x00); // Y End Address (high byte) = 0

    // 0x4F: Set RAM Y address counter
    // Y方向是递减的，所以光标起始点设在Y范围的开头 (249)
    sendCommand(0x4F);
    sendData(0xF9);
    sendData(0x00);
    
    // --- X-Axis Commands (水平方向配置，用于修正镜像) ---
    // 0x11: Data entry mode setting -> Y-decrement, X-decrement
    sendCommand(0x11);
    sendData(0x00); 

    // 0x44: set RAM X address start/end
    // 定义X轴窗口范围，顺序与递减方向匹配 (15 down to 0)
    sendCommand(0x44);
    sendData(0x0F); // X End Address   (15)
    sendData(0x00); // X Start Address (0)

    // 0x4E: Set RAM X address counter
    // X方向是递减的，所以光标起始点设在X范围的末尾 (15)
    sendCommand(0x4E);
    sendData(0x00);
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
