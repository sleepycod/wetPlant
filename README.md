Wet Plant - keeps plants wet

Arduino thing which keeps plants wet
ESP8266-12F and Nano
5v submersible pumps, relays
Capacitive moisture sensors
Arduino cloud IoT dashboard

Each plant has a moisture sensor, relay and pump.
ESP runs main code, controls pumps, etc.
Nano reads analog values from moisture sensor, sends CSV to ESP over serial
Dashboard in Arduino cloud allows pump_runtime and low_moisture config for each plant

Dashboard:
![image](https://github.com/sleepycod/wetPlant/assets/17673141/eaf86dbf-05cd-413c-a2ea-1ff18d0a22d6)

Photos:
https://photos.app.goo.gl/rLp9eq9hjK5K3qvN9
