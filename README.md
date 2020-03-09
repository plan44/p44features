p44features
===========

[![Flattr this git repo](http://api.flattr.com/button/flattr-badge-large.png)](https://flattr.com/submit/auto?user_id=luz&url=https://github.com/plan44/p44features&title= p44features&language=&tags=github&category=software) 

*p44features* is a set of free (opensource, GPLv3) C++ classes and functions building a library of "features" for various applications in exhibitions, experiments, art and fun projects. Each "feature" usually drives a particular piece of hardware, such as LEDs, solenoids, sensors etc. The features have a common base class and are accessible via a common JSON API.

*p44features* needs some classes and functions from the [*p44utils*](https://github.com/plan44/p44utils) and [*p44utils*](https://github.com/plan44/p44lrgraphics) libraries.

Projects using p44features (or cointaining roots that led to it) include 
the [ETH digital platform](https://plan44.ch/custom/custom.php#leth), the "chatty wifi" installation I brought to the 35c3, or the "hermeldon 2018" remote crocket playing installation (both in the [*hermel* branch of *lethd*](https://github.com/plan44/lethd/tree/hermeld))


Usage
-----
*p44features* sources are meant to be included as .cpp and .hpp files into a project (usually as a git submodule) and compiled together with the project's other sources.

A configuration header file *p44features\_config.hpp* needs to be present in the project, and allows customizing some aspects of *p44features*.

To get started, just copy the *p44features\_config\_TEMPLATE.hpp* to a location in your include path and name it *p44features\_config.hpp*.

License
-------

p44features are licensed under the GPLv3 License (see COPYING).

If that's a problem for your particular application, I am open to provide a commercial license, please contact me at [luz@plan44.ch](mailto:luz@plan44.ch).

Features
--------

List, will expand with each new project:

- indicators: areas in a LED strip or matrix used as indicators with different styles
- rfid: multiple cheap RFID readers as user-detecting "buttons"
- dispmatrix: time synchronized LED matrix displays for large scrolling text display
- inputs: use any GPIO, or pins of some i2c/spi based I/O extensions, or console keys for simulation, as generic inputs
- light: simple PWM light dimmer
- neuron: sensor triggered "conductance" light effect
- mixloop: accelerometer triggered ball movement detector and light effect
- wifitrack: visualizing WiFi SSIDs revealed to the public in probe requests
- hermel: dual solenoid crocket playing driver


Feature API
-----------

### Dispmatrix

#### Initialisation

{ "cmd":"init", "dispmatrix": { "installationX": *overall-x*, "installationY": *overall-y*, "rootview": *p44lrgraphics-view-config* }

- *x,y*: position of this p44featured unit in a longer scroller consisting of multiple p44featured hardware units. This allows sending the same global scrolling offsets to all units, and each unit interprets it according to its position in the overall installation.
- if no *p44lrgraphics-view-config* is specified, the root view will be set to a scroller labelled "DISPSCROLLER" filling the entire LEDarrangement. If a custom view config is passed, it should contain a scroller named "DISPSCROLLER".
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

### Indicators

#### Initialisation

{ "cmd":"init", "indicators": { "rootview": *p44lrgraphics-view-config* }

- if no *p44lrgraphics-view-config* is specified, the root view will be set to a stack labelled "INDICATORS" filling the entire LEDarrangement. If a custom view config is passed, it should contain a stack named "INDICATORS".

#### Show an indicator

{ "feature":"indicators", "cmd":"indicate", "x":*x-start*, "dx":*x-size*, "y":*y-start*, "dy":*y-size*, "effect":"*effect-name-or-viewconfig*", "t":*effect-duration-in-seconds*, "color":"*web-color*" }

- *effect-name-or-viewconfig* can be one of the built-in effects (at this time: "*plain*", "*swipe*", "*pulse*", "*spot*") or configuration of a view (including animations) to be shown at the specified coordinates. The view can be specified as inline JSON or via specifying the resource name of a resource file. A relative filename will be searched in the /indicators subdirectory of the app's resource directory.


### RFIDs

#### Initialisation

{ "cmd":"init", "rfids": { "readers": [*index*, *index*,...], "pollinterval":*pollinterval_seconds*, "sameidtimeout":*re\_reporting\_timeout* "pauseafterdetect":*poll\_pause\_after\_card\_detected*}

- *index* are the physical bus addresses (0..23 in p44rfidctrl and p44rfidhat hardware) to select the reader. Depending on which readers on which cables are in use, this can be a sequence of numbers with or without gaps.
- *pollinterval_seconds* is the  polling interval for the connected readers, i.e. how often every reader will be checked for the presence of a new RFID tag (default: 0.1 seconds)
- *pauseafterdetect* is the  polling interval for the connected readers, i.e. how often every reader will be checked for the presence of a new RFID tag (default: 0.1 seconds)
- *re\_reporting\_timeout* is the time during which a reader will not report the same nUID again (default: 3 seconds)
- *poll\_pause\_after\_card\_detected* is the time polling RFIDs will be paused after detecting a card - mainly to free performance for LED effects, as SPI on RPi seems to block a lot (default: 1 second)


#### API events

{ "feature":"rfids", "nUID":"*rfid_nUID*", "reader":*rfid\_reader\_index* }

- *rfid_nUID* is the nUID of the RFID tag seen
- *rfid\_reader\_index* is the physical bus address of the reader which has seen the RFID tag


### Generic Inputs

#### Initialisation

{ "cmd":"init", "inputs": { "*input_name*":{ "pin":"*pin_spec*", "initially":*initial_bool*, "debouncing":*debounce\_in\_seconds*, "pollinterval":*pollinterval\_in\_seconds* } , ... } }

- *input_name* is a name which is included in the API events to identify the input.
- *pin_spec* is a p44utils DigitalIo pin specification
- optional *initial_bool* is the initial value the input is assumed to have (to force/prevent event at init)
- optional *debounce\_in\_seconds* sets debouncing time (default = 80mS)
- optional *pollinterval\_in\_seconds* sets polling interval (default = 250mS). This is only for inputs which do not have system level edge detection.

#### API events

{ "feature":"inputs", "*input_name*":*input_value* }

- *input_name* is a name of the input.
- *input_value* is the current value of the input



(c) 2013-2020 by Lukas Zeller / [plan44.ch](https://www.plan44.ch/opensource.php)
