p44features
===========

[![Flattr this git repo](http://api.flattr.com/button/flattr-badge-large.png)](https://flattr.com/submit/auto?user_id=luz&url=https://github.com/plan44/p44features&title= p44features&language=&tags=github&category=software) 

*p44features* is a set of free (opensource, GPLv3) C++ classes and functions building a library of "features" for various applications in exhibitions, experiments, art and fun projects. Each "feature" usually drives a particular piece of hardware, such as LEDs, solenoids, sensors etc. The features have a common base class and are accessible via a common JSON API.

*p44features* needs some classes and functions from the [*p44utils*](https://github.com/plan44/p44utils) and [*p44utils*](https://github.com/plan44/p44lrgraphics) libraries.

Projects using p44features (or cointaining roots that led to it) include 
the [ETH digital platform](https://plan44.ch/custom/custom.php#leth), the "chatty wifi" installation I brought to the 35c3, or the "hermeldon 2018" remote crocket playing installation (both in the [*hermel* branch of *lethd*](https://github.com/plan44/lethd/tree/hermeld)


Usage
-----
*p44features* sources meant to be included as .cpp and .hpp files into a project (usually as a git submodule) and compiled together with the project's other sources.
A configuration header file *p44features_config.hpp* needs to be present in the project, and allows customizing some aspects of *p44features*.
To get started, just copy the *p44features_config_TEMPLATE.hpp* to a location in your include path and name it *p44features_config.hpp*.

License
-------

p44features are licensed under the GPLv3 License (see COPYING).

If that's a problem for your particular application, I am open to provide a commercial license, please contact me at [luz@plan44.ch](mailto:luz@plan44.ch).

Features
--------

List, will expand with each new project:

- dispmatrix: time synchronized LED matrix displays for large scrolling text display
- light: simple PWM light dimmer
- neuron: sensor triggered "conductance" light effect
- mixloop: accelerometer triggered ball movement detector and light effect
- wifitrack: visualizing WiFi SSIDs revealed to the public in probe requests
- hermel: dual solenoid crocket playing driver
- rfid: multiple cheap RFID readers as user-detecting "buttons"

(c) 2013-2020 by Lukas Zeller / [plan44.ch](https://www.plan44.ch/opensource.php)
