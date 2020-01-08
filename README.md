# ESP32-Captive-Portal
Basic Sketch to set WiFi, IP and Hostname through a captive portal for ESP32 based on the WiFiManager library.

With this Sketch it is possible to change the WiFi settings, IP-Address and Hostname settings through a webpage.

On the first boot it opens a AP with the name ESP_Config.

When connected a Captive Portal opens to change the settings.

The settings are then stored on the EEPROM.
