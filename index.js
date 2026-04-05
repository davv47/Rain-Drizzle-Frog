'use strict';

// ── AppMessage keys (must match main.c) ───────────────────────────────────────
var KEY_WMO_CODE    = 0;
var KEY_TEMPERATURE = 1;
var KEY_PRECIP_MM10 = 2;
var KEY_FETCH_TIME  = 3;

// ── State ────────────────────────────────────────────────────────────────────
var lastFetchTime = 0;
var FETCH_INTERVAL_MS = 60 * 60 * 1000; // 1 hour

// ── Geolocation + Open-Meteo fetch ───────────────────────────────────────────
function fetchWeather() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      var lat = pos.coords.latitude.toFixed(4);
      var lon = pos.coords.longitude.toFixed(4);

      var url = 'https://api.open-meteo.com/v1/forecast' +
        '?latitude=' + lat +
        '&longitude=' + lon +
        '&current_weather=true' +
        '&daily=precipitation_sum' +
        '&timezone=auto' +
        '&forecast_days=1';

      var xhr = new XMLHttpRequest();
      xhr.open('GET', url, true);
      xhr.onload = function() {
        if (xhr.status === 200) {
          try {
            var data = JSON.parse(xhr.responseText);
            var wmo  = data.current_weather.weathercode;
            var temp = Math.round(data.current_weather.temperature);

            // daily precipitation_sum is in mm; multiply by 10 for int transfer
            var precip_raw = data.daily && data.daily.precipitation_sum
              ? data.daily.precipitation_sum[0]
              : 0;
            if (precip_raw === null || precip_raw === undefined) {
              precip_raw = 0;
            }
            var precip_mm10 = Math.round(precip_raw * 10);

            sendToWatch(wmo, temp, precip_mm10);
            lastFetchTime = Date.now();
          } catch (e) {
            console.log('RDF: parse error: ' + e);
          }
        } else {
          console.log('RDF: HTTP error ' + xhr.status);
        }
      };
      xhr.onerror = function() {
        console.log('RDF: network error');
      };
      xhr.send();
    },
    function(err) {
      console.log('RDF: geolocation error: ' + err.message);
    },
    { timeout: 15000, maximumAge: 300000 }
  );
}

function sendToWatch(wmo, temp, precip_mm10) {
  var msg = {};
  msg[KEY_WMO_CODE]    = wmo;
  msg[KEY_TEMPERATURE] = temp;
  msg[KEY_PRECIP_MM10] = precip_mm10;
  msg[KEY_FETCH_TIME]  = Math.round(Date.now() / 1000);

  Pebble.sendAppMessage(msg,
    function() { console.log('RDF: sent ok'); },
    function(e) { console.log('RDF: send fail: ' + JSON.stringify(e)); }
  );
}

// ── Pebble events ─────────────────────────────────────────────────────────────
Pebble.addEventListener('ready', function() {
  console.log('RDF: JS ready');
  fetchWeather();
});

Pebble.addEventListener('appmessage', function(e) {
  // Watch can request a refresh by sending any message
  var now = Date.now();
  if (now - lastFetchTime > FETCH_INTERVAL_MS) {
    fetchWeather();
  }
});
