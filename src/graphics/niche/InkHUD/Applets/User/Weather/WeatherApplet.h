#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

WeatherApplet
-------------
Mostra le condizioni meteo attuali e la previsione a 3 giorni,
scaricate via Wi-Fi da Open-Meteo (nessuna API key richiesta).

I dati vengono aggiornati una volta all'ora da un OSThread interno
(WeatherFetcher), che al termine del download chiede all'applet di
ridisegnare lo schermo.

In variants/esp32s3/heltec_vision_master_e290/nicheGraphics.h:

    - includere questo header
    - registrare l'applet:
        inkhud->addApplet("Meteo", new InkHUD::WeatherApplet, true, true);

*/

#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"
#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

// Previsione di un singolo giorno
struct WeatherDayForecast {
    std::string dayLabel;  // "Oggi", "Dom", "Lun" ...
    int16_t tempMinC = 0;
    int16_t tempMaxC = 0;
    int weatherCode = 0; // WMO weather code (Open-Meteo)
};

class WeatherApplet : public Applet
{
  public:
    WeatherApplet();

    void onRender(bool full) override;
    void onActivate() override;
    void onDeactivate() override;

    // Chiamato dal WeatherFetcher quando arrivano nuovi dati
    void setCurrentConditions(float tempC, float feelsLikeC, int humidity, int weatherCode);
    void setForecastDay(uint8_t index, const WeatherDayForecast &day);
    void markFetchFailed();

  private:
    void drawWeatherIcon(int16_t cx, int16_t cy, uint16_t size, int weatherCode);
    std::string describeWeatherCode(int code);

    bool hasData = false;
    bool lastFetchFailed = false;

    float currentTempC = 0;
    float currentFeelsLikeC = 0;
    int currentHumidity = 0;
    int currentWeatherCode = 0;

    static constexpr uint8_t FORECAST_DAYS = 3;
    WeatherDayForecast forecast[FORECAST_DAYS];

    class WeatherFetcher *fetcher = nullptr;
};

// Thread in background: interroga l'API meteo una volta all'ora
class WeatherFetcher : public concurrency::OSThread
{
  public:
    explicit WeatherFetcher(WeatherApplet *applet);

  protected:
    int32_t runOnce() override;

  private:
    void fetchWeather();

    WeatherApplet *applet;

    // Intervallo tra due aggiornamenti: un'ora
    static constexpr uint32_t UPDATE_INTERVAL_MS = 60UL * 60UL * 1000UL;
    // Se il tentativo fallisce (no wifi, errore rete), riprova prima
    static constexpr uint32_t RETRY_INTERVAL_MS = 2UL * 60UL * 1000UL;
};

} // namespace NicheGraphics::InkHUD

#endif
