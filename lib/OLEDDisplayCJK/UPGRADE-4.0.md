# Upgrade from 3.x to 4.0

There are changes that breaks compatibility with older versions.

1. You'll have to change data type for all your binary resources such as images and fonts from
    
    ```c
    const char MySymbol[] PROGMEM = {
    ```
    
    to
    
    ```c
    const uint8_t MySymbol[] PROGMEM = {
    ```

1. Arguments of `setContrast` from `char` to `uint8_t`
    
    ```c++
    void OLEDDisplay::setContrast(char contrast, char precharge, char comdetect);
    ```
    
    to
    
    ```c++
    void OLEDDisplay::setContrast(uint8_t contrast, uint8_t precharge, uint8_t comdetect);
    ```
