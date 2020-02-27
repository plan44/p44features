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


Feature API
-----------

### Dispmatrix

#### Initialisation

{ "cmd":"init", "dispmatrix": { "installationX": *overall-x*, "installationY": *overall-y*, "rootview": *p44lrgraphics-view-config* }

- *x,y*: position of this p44featured unit in a longer scroller consisting of multiple p44featured hardware units. This allows sending the same global scrolling offsets to all units, and each unit interprets it according to its position in the overall installation.
- if no *p44lrgraphics-view-config* is specified, the root view will be set to a scroller labelled "DISPSCROLLER" filling the entire LEDarrangement
- *p44lrgraphics-view-config* can be the view config JSON object itself or a resource file name. A relative filename will be searched in the /dispmatrix subdirectory of the app's resource directory.

#### Start scrolling

{ "feature":"dispmatrix", "cmd":"startscroll", "stepx":*x-step-size*, "stepy": *y-step-size*, "steps": *num-steps*, "interval": *step-interval-seconds*, "roundoffsets": *bool*, "start": *absolute_unix-time-in-seconds* }

- step sizes can be fractional
- *num-steps* can be negative for unlimited scrolling
- *roundoffsets* causes the current scroll offsets to be rounded to integer number before starting scroll
- *absolute_unix-time-in-seconds* allows strictly synchronized scrolling start over multiple (NTP synchronized!) hardware units. if `null` is passed, scrolling starts at the next 10-second boundary in absolute unix time.

#### Stop scrolling

{ "feature":"dispmatrix", "cmd":"stopscroll" }

#### Fade Alpha

{ "feature":"dispmatrix", "cmd":"fade", "to": *target-alpha*, "t": *fade-time-in-seconds* }

#### (Re)Configure a view

{ "feature":"dispmatrix", "cmd":"configure", "view": *view-label*, "config": *p44lrgraphics-view-config* }

#### Set a "scene"

{ "feature":"dispmatrix", "scene":*p44lrgraphics-view-config* }

- Setting a scene means replacing the "DISPSCROLLER"'s scrolled view by a new view. This special command makes sure changing the scrolled view's works with wraparound scrolling over multiple modules.

#### Set scroll offsets

{ "feature":"dispmatrix", "offsetx":*offset-x* }

{ "feature":"dispmatrix", "offsetx":*offset-y* }

- This sets the content's scroll position, relative to the configured *installationX/Y* offsets

 




(c) 2013-2020 by Lukas Zeller / [plan44.ch](https://www.plan44.ch/opensource.php)
