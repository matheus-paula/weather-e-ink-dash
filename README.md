# Weather E-Ink Dash 🌤️
<p align="center">
  <img height="300" src="https://raw.githubusercontent.com/matheus-paula/weather-e-ink-dash/refs/heads/main/images/example_1.jpg">
</p>

[![License MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

### Weather Dashboard for 2,9" WeAct Studio E-paper modules, with Home Assistant and OpenWeatherMap API integrations for ESP-32 microcontrollers

## Open Weather API  
To use the OpenWeather API on this project, go to [OpenWeather API](https://openweathermap.org/api) and get your free API key, which allows up to 1000 requests per day on the free plan.

## Home Assistant Integration  
To use the Home Assistant temperature sensors integration, you must have a maximum of 3 temperature sensors in your current setup, which will display the current temperature and humidity values. You also need to generate an API key on Home Assistant itself to authenticate with your server.

## Battery Level Information  
To use the battery level information on the project, an additional circuit needs to be added to your current setup. Following the image below, you need to attach a voltage divider circuit to the battery and connect its midpoint to an ADC pin on the ESP32. In the base code, the used pin is PIN 0, but this can change depending on your specific ESP32 board.

<p align="center">
  <img height="300" src="https://raw.githubusercontent.com/matheus-paula/weather-e-ink-dash/refs/heads/main/images/example_2.png">
</p>
