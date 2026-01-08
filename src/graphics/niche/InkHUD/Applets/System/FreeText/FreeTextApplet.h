#ifdef MESHTASTIC_INCLUDE_INKHUD

/*
 
System Applet to send a message using a virtual keyboard
 
*/

#pragma once

#include "configuration.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include <string>
namespace NicheGraphics::InkHUD
{


class FreeTextApplet : public SystemApplet
{
    public:
    FreeTextApplet();
	
	void onRender() override;
	void onForeground() override;
	void onBackground() override;
	void onButtonShortPress() override;
	void onButtonLongPress() override;
	void onExitShort() override;
	void onExitLong() override;
	void onNavUp() override;
	void onNavDown() override;
	void onNavLeft() override;
	void onNavRight() override;
	
	static const uint8_t KEYBOARD_COLS = 11;
	static const uint8_t KEYBOARD_ROWS = 4;
	private:
	enum KEY_ACTIONS {
		SWITCH_LAYER,
		BACKSPACE,
		SHIFT,
		ENTER,
	};
	struct Key {
		char c;
		KEY_ACTIONS action;

	};
    
	protected:
		struct Key keyboard[KEYBOARD_COLS][KEYBOARD_ROWS];
		const char keyboardLayout[KEYBOARD_ROWS][KEYBOARD_COLS] = {{'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\b'},
                                                                      {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '\n'},
                                                                      {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', ' '},
                                                                      {'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', '\x1b'}};

	
	

	//void drawKeypad(uint16_t pointX, uint16_t pointY, Color color);
	//void drawInput(uint16_t pointX, uint16_t pointY);)
};

} // namespace NicheGrpahics::InkHUD

#endif
