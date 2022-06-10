#ifndef _SPEAKER_H_
  #define _SPEAKER_H_

  #include "configuration.h"

  #ifdef __cplusplus
    extern "C"
    {
  #endif /* __cplusplus */
  #include "esp32-hal-dac.h"
  #ifdef __cplusplus
    }
  #endif /* __cplusplus */

  class TONE {
    public:
      TONE(void);

      void begin();
      void end();
      void mute();
      void tone(uint16_t frequency);
      void setVolume(uint8_t volume);

    private:
      uint8_t _volume;
      bool _begun;
      bool speaker_on;
  };
#endif
