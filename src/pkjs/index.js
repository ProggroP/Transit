// PebbleKit JS: Clay-Einstellungen plus die Tages-Tiefst- und -Hoechstwerte
// von Open-Meteo. Die Uhr fragt per REQUEST_WEATHER an, wenn die dritte Zeile
// auf Temperatur steht; die Antwort landet als TEMP_MIN/TEMP_MAX/WX_UNIT auf
// der Uhr. Kein Abruf beim Start, damit die Konfigurationsseite nicht mit
// einem laufenden Netzabruf kollidiert.

var Clay = require('@rebble/clay');
var clayConfig = require('./config');

var customClay = require('./custom-clay');

var clay = new Clay(clayConfig, customClay);

var WEATHER_URL = 'https://api.open-meteo.com/v1/forecast';
var REQUEST_TIMEOUT_MS = 15000;

function getStoredSettings() {
    try {
        return JSON.parse(localStorage.getItem('clay-settings') || '{}');
    } catch (e) {
        return {};
    }
}

// Keine Schnittstelle zur Einheiten-Einstellung der Pebble-App, also die
// Region des Telefons auswerten -- wer damit falsch liegt, stellt es um.
var FAHRENHEIT_REGIONS = /-(US|BS|BZ|KY|FM|MH|PW|LR)\b/i;

function useFahrenheit() {
    var unit = getStoredSettings().TEMP_UNIT;
    if (unit === 'celsius')    return false;
    if (unit === 'fahrenheit') return true;
    var locale = (navigator && navigator.language) || '';
    return FAHRENHEIT_REGIONS.test(locale);
}

function fetchWeather(lat, lon) {
    var fahrenheit = useFahrenheit();
    var url = WEATHER_URL
            + '?latitude='  + lat.toFixed(4)
            + '&longitude=' + lon.toFixed(4)
            + '&daily=temperature_2m_max,temperature_2m_min'
            + '&timezone=auto&forecast_days=1'
            + '&temperature_unit=' + (fahrenheit ? 'fahrenheit' : 'celsius');

    var request = new XMLHttpRequest();
    request.open('GET', url, true);
    request.timeout = REQUEST_TIMEOUT_MS;
    request.onload = function() {
        if (request.status !== 200) {
            console.log('weather request failed: HTTP ' + request.status);
            return;
        }
        try {
            var daily = (JSON.parse(request.responseText) || {}).daily;
            if (!daily || !daily.temperature_2m_min || !daily.temperature_2m_max) return;
            Pebble.sendAppMessage({
                'TEMP_MIN': Math.round(daily.temperature_2m_min[0]),
                'TEMP_MAX': Math.round(daily.temperature_2m_max[0]),
                'WX_UNIT':  fahrenheit ? 1 : 0
            }, null, function() { console.log('weather: send failed'); });
        } catch (e) {
            console.log('weather not readable: ' + e);
        }
    };
    request.onerror   = function() { console.log('weather: network error'); };
    request.ontimeout = function() { console.log('weather: timeout'); };
    request.send();
}

function refreshWeather() {
    navigator.geolocation.getCurrentPosition(
        function(pos) { fetchWeather(pos.coords.latitude, pos.coords.longitude); },
        function(err) { console.log('location unavailable: ' + err.message); },
        { enableHighAccuracy: false, maximumAge: 1800000, timeout: 15000 }
    );
}

Pebble.addEventListener('appmessage', function(e) {
    if (e.payload && e.payload.REQUEST_WEATHER !== undefined) refreshWeather();
});
