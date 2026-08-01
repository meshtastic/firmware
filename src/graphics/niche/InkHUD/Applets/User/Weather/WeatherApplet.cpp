#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./WeatherApplet.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "mesh/wifi/WiFiAPClient.h"

using namespace NicheGraphics;

// =====================================================================
// CONFIGURAZIONE: modifica queste 3 righe prima di compilare
// =====================================================================
// Coordinate del luogo di cui mostrare il meteo.
// Esempio: Roma = 41.9028, 12.4964 -- Milano = 45.4642, 9.1900
static constexpr float WEATHER_LATITUDE = 45.068;
static constexpr float WEATHER_LONGITUDE = 7.577;
// =====================================================================

static const char *WEATHER_API_HOST = "https://api.open-meteo.com/v1/forecast";

// -----------------------------------------------------------------------
// WeatherApplet
// -----------------------------------------------------------------------

InkHUD::WeatherApplet::WeatherApplet()
{
    // Aggiorna anche se non è la applet mostrata in primo piano
    // (in questo caso è comunque l'unica applet attiva)
}

void InkHUD::WeatherApplet::onActivate()
{
    if (!fetcher)
        fetcher = new WeatherFetcher(this);
}

void InkHUD::WeatherApplet::onDeactivate()
{
    // Il fetcher continua comunque a girare in background;
    // lo lasciamo vivo per semplicità (un solo applet attivo in questo firmware)
}

void InkHUD::WeatherApplet::setCurrentConditions(float tempC, float feelsLikeC, int humidity, int weatherCode)
{
    currentTempC = tempC;
    currentFeelsLikeC = feelsLikeC;
    currentHumidity = humidity;
    currentWeatherCode = weatherCode;
    hasData = true;
    lastFetchFailed = false;
    requestUpdate(); // Chiede il refresh del display
}

void InkHUD::WeatherApplet::setForecastDay(uint8_t index, const WeatherDayForecast &day)
{
    if (index < FORECAST_DAYS)
        forecast[index] = day;
}

void InkHUD::WeatherApplet::markFetchFailed()
{
    lastFetchFailed = true;
    requestUpdate();
}

std::string InkHUD::WeatherApplet::describeWeatherCode(int code)
{
    // Codici meteo WMO (usati da Open-Meteo)
    if (code == 0)
        return "Sereno";
    if (code >= 1 && code <= 3)
        return "Poco nuvoloso";
    if (code == 45 || code == 48)
        return "Nebbia";
    if (code >= 51 && code <= 67)
        return "Pioggia";
    if (code >= 71 && code <= 77)
        return "Neve";
    if (code >= 80 && code <= 82)
        return "Rovesci";
    if (code >= 95)
        return "Temporale";
    return "N/D";
}

void InkHUD::WeatherApplet::drawWeatherIcon(int16_t cx, int16_t cy, uint16_t size, int weatherCode)
{
    // Icone essenziali disegnate con forme geometriche (schermo monocromatico)
    uint16_t r = size / 2;

    if (weatherCode == 0) {
        // Sole: cerchio pieno + raggi
        fillCircle(cx, cy, r * 0.6, BLACK);
        for (int i = 0; i < 8; i++) {
            float angle = i * (PI / 4);
            int16_t x1 = cx + cos(angle) * r * 0.75;
            int16_t y1 = cy + sin(angle) * r * 0.75;
            int16_t x2 = cx + cos(angle) * r;
            int16_t y2 = cy + sin(angle) * r;
            drawLine(x1, y1, x2, y2, BLACK);
        }
    } else if (weatherCode <= 3) {
        // Poco nuvoloso: sole parziale + nuvola
        fillCircle(cx - r * 0.3, cy - r * 0.2, r * 0.45, BLACK);
        fillCircle(cx + r * 0.15, cy + r * 0.2, r * 0.5, BLACK);
        fillCircle(cx + r * 0.55, cy + r * 0.15, r * 0.35, BLACK);
    } else if (weatherCode == 45 || weatherCode == 48) {
        // Nebbia: linee orizzontali
        for (int i = -2; i <= 2; i++)
            drawLine(cx - r, cy + i * (r / 3), cx + r, cy + i * (r / 3), BLACK);
    } else if ((weatherCode >= 51 && weatherCode <= 67) || (weatherCode >= 80 && weatherCode <= 82)) {
        // Pioggia: nuvola + gocce
        fillCircle(cx - r * 0.25, cy - r * 0.3, r * 0.4, BLACK);
        fillCircle(cx + r * 0.2, cy - r * 0.15, r * 0.45, BLACK);
        for (int i = -1; i <= 1; i++)
            drawLine(cx + i * (r / 2), cy + r * 0.2, cx + i * (r / 2) - 3, cy + r * 0.8, BLACK);
    } else if (weatherCode >= 71 && weatherCode <= 77) {
        // Neve: nuvola + fiocchi (punti)
        fillCircle(cx - r * 0.2, cy - r * 0.3, r * 0.4, BLACK);
        fillCircle(cx + r * 0.2, cy - r * 0.15, r * 0.4, BLACK);
        for (int i = -1; i <= 1; i++)
            fillCircle(cx + i * (r / 2), cy + r * 0.6, 2, BLACK);
    } else if (weatherCode >= 95) {
        // Temporale: nuvola + fulmine
        fillCircle(cx - r * 0.2, cy - r * 0.3, r * 0.4, BLACK);
        fillCircle(cx + r * 0.2, cy - r * 0.15, r * 0.45, BLACK);
        drawLine(cx, cy + r * 0.1, cx - 4, cy + r * 0.5, BLACK);
        drawLine(cx - 4, cy + r * 0.5, cx + 3, cy + r * 0.5, BLACK);
        drawLine(cx + 3, cy + r * 0.5, cx - 2, cy + r * 0.9, BLACK);
    } else {
        drawCircle(cx, cy, r * 0.6, BLACK);
    }
}

void InkHUD::WeatherApplet::onRender(bool full)
{
    if (!hasData) {
        setFont(fontMedium);
        printAt(X(0.5), Y(0.5), lastFetchFailed ? "Errore connessione meteo" : "Attendo dati meteo...", CENTER,
                MIDDLE);
        return;
    }

    // ---- Riga superiore: condizioni attuali, testo grande ----
    setFont(fontLarge);
    char tempStr[16];
    snprintf(tempStr, sizeof(tempStr), "%.0f\xC2\xB0" "C", currentTempC);
    printAt(X(0.02), Y(0.02), tempStr, LEFT, TOP);

    drawWeatherIcon(X(0.82), Y(0.22), Y(0.34), currentWeatherCode);

    setFont(fontMedium);
    printAt(X(0.02), Y(0.30), describeWeatherCode(currentWeatherCode), LEFT, TOP);

    setFont(fontSmall);
    char subStr[48];
    snprintf(subStr, sizeof(subStr), "Percepita %.0f\xC2\xB0" "C - Umidita %d%%", currentFeelsLikeC, currentHumidity);
    printAt(X(0.02), Y(0.44), subStr, LEFT, TOP);

    // ---- Linea divisoria ----
    drawLine(X(0.0), Y(0.52), X(1.0), Y(0.52), BLACK);

    // ---- Riga inferiore: previsione 3 giorni, 3 colonne uguali ----
    uint16_t colWidth = X(1.0) / FORECAST_DAYS;
    for (uint8_t i = 0; i < FORECAST_DAYS; i++) {
        int16_t colCenter = colWidth * i + colWidth / 2;

        setFont(fontSmall);
        printAt(colCenter, Y(0.58), forecast[i].dayLabel, CENTER, TOP);

        drawWeatherIcon(colCenter, Y(0.78), Y(0.18), forecast[i].weatherCode);

        setFont(fontMedium);
        char range[16];
        snprintf(range, sizeof(range), "%d/%d\xC2\xB0", forecast[i].tempMaxC, forecast[i].tempMinC);
        printAt(colCenter, Y(0.92), range, CENTER, TOP);

        if (i > 0)
            drawLine(colWidth * i, Y(0.55), colWidth * i, Y(1.0), BLACK);
    }
}

// -----------------------------------------------------------------------
// WeatherFetcher: OSThread che interroga Open-Meteo una volta all'ora
// -----------------------------------------------------------------------

InkHUD::WeatherFetcher::WeatherFetcher(WeatherApplet *applet) : concurrency::OSThread("WeatherFetcher"), applet(applet)
{
    // Primo tentativo poco dopo l'avvio, per non ritardare il boot
    setIntervalFromNow(15 * 1000);
}

int32_t InkHUD::WeatherFetcher::runOnce()
{
    fetchWeather();
    return UPDATE_INTERVAL_MS;
}

void InkHUD::WeatherFetcher::fetchWeather()
{
    if (!isWifiAvailable() || WiFi.status() != WL_CONNECTED) {
        // Wi-Fi non ancora pronto (o non configurato): riprova tra poco
        applet->markFetchFailed();
        setIntervalFromNow(RETRY_INTERVAL_MS);
        return;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "%s?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
             "weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=4",
             WEATHER_API_HOST, WEATHER_LATITUDE, WEATHER_LONGITUDE);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        http.end();
        applet->markFetchFailed();
        setIntervalFromNow(RETRY_INTERVAL_MS);
        return;
    }

    // Documento JSON con dimensionamento automatico (API ArduinoJson v7)
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        applet->markFetchFailed();
        setIntervalFromNow(RETRY_INTERVAL_MS);
        return;
    }

    // ---- Condizioni attuali ----
    float tempC = doc["current"]["temperature_2m"] | 0.0f;
    float feelsLikeC = doc["current"]["apparent_temperature"] | tempC;
    int humidity = doc["current"]["relative_humidity_2m"] | 0;
    int weatherCode = doc["current"]["weather_code"] | 0;
    applet->setCurrentConditions(tempC, feelsLikeC, humidity, weatherCode);

    // ---- Previsione: saltiamo l'indice 0 (oggi, già coperto dal blocco
    //      "current") e usiamo i 3 giorni successivi ----
    JsonArray dates = doc["daily"]["time"];
    JsonArray codes = doc["daily"]["weather_code"];
    JsonArray tmax = doc["daily"]["temperature_2m_max"];
    JsonArray tmin = doc["daily"]["temperature_2m_min"];

    for (uint8_t i = 0; i < 3; i++) {
        uint8_t srcIndex = i + 1; // giorno successivo
        InkHUD::WeatherDayForecast day;

        if (srcIndex < dates.size()) {
            std::string date = dates[srcIndex].as<std::string>(); // "YYYY-MM-DD"
            if (date.size() == 10)
                day.dayLabel = date.substr(8, 2) + "/" + date.substr(5, 2); // "DD/MM"
        }
        if (srcIndex < codes.size())
            day.weatherCode = codes[srcIndex].as<int>();
        if (srcIndex < tmax.size())
            day.tempMaxC = (int16_t)round(tmax[srcIndex].as<float>());
        if (srcIndex < tmin.size())
            day.tempMinC = (int16_t)round(tmin[srcIndex].as<float>());

        applet->setForecastDay(i, day);
    }
}

#endif
