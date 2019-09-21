# GC-20
### Portable Geiger counter, dosimeter and monitoring station with features such as:
- Touchscreen controlled, intuitive GUI
- Displays counts per minute, current dose, and accumulated dose on homescreen
- Sensitive and reliable SBM-20 Geiger-Muller tube
- Variable integration time for averaging dose rate
- Timed count mode for measuring low doses
- Choose between Sieverts and Rems as the units for the displayed dose rate
- User adjustable alert threshold
- Adjustable calibration to relate CPM to dose rate for various isotopes
- Audible clicker and LED indicator toggled on and off from homescreen
- Offline data logging
- Post bulk logged data to cloud service (ThingSpeak) to graph, analyze and/or save to computer
- Monitoring Station mode: device stays connected to WiFi and regularly posts ambient radiation level to ThingSpeak channel

### Overview

# Homepage:
![test](https://raw.githubusercontent.com/pra22/GC-20/master/Images/homepage.jpg)

The homepage displays the current effective dose rate, counts per minute, and the cumulative dose since the device was turned on. The integration time can be changed by tapping the "INT 60 s" button and the user can choose between 60, 180 and 5 seconds of integration. A shorter time allows faster response to changing radiation levels at the expense of accuracy. Using 180 seconds gives the least amount random fluctuation.

![test](https://raw.githubusercontent.com/pra22/GC-20/master/Images/settings.jpg)

The settings menu has four options: Dose Units, Alert Threshold, Calibration, and Logging and WiFi. 

![test](https://raw.githubusercontent.com/pra22/GC-20/master/Images/units.jpg)

The Dose Units option allows the user to choose between either Sieverts or Rems for the effective dose rate and the accumulated dose.

![test](https://raw.githubusercontent.com/pra22/GC-20/master/Images/threshold.jpg)

The user can adjust the threshold at which the home page displays a "High Radiation Level" alert in red, in increments of 1 uSv/hr.

![test](https://raw.githubusercontent.com/pra22/GC-20/master/Images/calibration.jpg)

The calibration factor that relates CPM to uSv/hr can be changed using this option. The geiger tube has different sensitivities to different isotopes, so to keep the dose readings reasonably accurate, a default conversion factor of 175 is used. This will accurately report the gamma dose from Cesium-137. 
![test](https://raw.githubusercontent.com/pra22/GC-20/master/Images/logging_off.jpg)
Finally, tapping the Logging and WiFi button offers a further sub-menu of options related to setting up wifi for the first time, logging data, uploading logged data, and setting the device mode.
![test](https://raw.githubusercontent.com/pra22/GC-20/master/Images/wifi_setup.jpg)

