p44features
===========

*[[if you want to support p44features development, please consider to sponsor plan44]](https://github.com/sponsors/plan44)* 

*p44features* is a set of free (opensource, GPLv3) C++ classes and functions building a library of "features" for various applications in exhibitions, experiments, art and fun projects. Each "feature" usually drives a particular piece of hardware, such as LEDs, RFIDs, Swiss Railway Splitflap modules, sensors etc. The features have a common base class and are accessible via a common JSON API. When used in applications with [p44script](https://plan44.ch/p44-techdocs/en/#topics) enabled, these features can also be controller from on-devices scripts. Likewise, the feature JSON api can be extended to implement additional *features* in p44script using the `featurecall()` event source.

*p44features* needs some classes and functions from the [*p44utils*](https://github.com/plan44/p44utils) and [*p44lrgraphics*](https://github.com/plan44/p44lrgraphics) libraries.

Projects using p44features (or cointaining roots that led to it) include 
the [ETH digital platform](https://plan44.ch/custom#leth), the "chatty wifi" installation I brought to the 35c3, the "hermeldon 2018" remote crocket playing installation (both in the [*hermel* branch of *lethd*](https://github.com/plan44/lethd/tree/hermeld)), the exhibition [Auf der Suche nach der Wahrheit](https://suchewahrheit.ch/web/de-ch/) with hundreds of RFID readers. The p44features are also integrated in the [vdcd](https://plan44.ch/opensource/vdcd) project and thus available in the [p44-xx-open (home)automation platform](https://github.com/plan44/p44-xx-open), which allows for interesting augmentation of home automation and lighting systems.


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

List of currently available features, will expand with the demands of new projects:

- **indicators**: areas in a LED strip or matrix used as indicators with different styles
- **rfids**: multiple cheap RFID readers as user-detecting "buttons"
- **dispmatrix**: time synchronized LED matrix displays for large scrolling text display
- **inputs**: use any GPIO, or pins of some i2c/spi based I/O extensions, or console keys for simulation, as generic inputs
- **light**: simple PWM light dimmer
- **splitflaps**: Splitflap displays (as produced by Omega for Swiss Railways (SBB) and also other railways, such as Deutsche Bahn). Both older setups with a central RS422 connected controller board ("Omega controller") driving a dozen or more non-smart splitflap modules, as well as the newer RS485 bus connected modules with a µP controller on each module are supported. 
- **neuron**: sensor triggered "conductance" light effect
- **mixloop**: accelerometer triggered ball movement detector and light effect
- **wifitrack**: visualizing WiFi SSIDs revealed to the public in probe requests
- **hermel**: dual solenoid crocket playing driver


Feature API
-----------
*Note: this is Work in Progress, not all features are documented here*

### General

Feature API calls consist of a JSON object. One call can contain one command and/or optionally property values to set.

- A command consists of a *cmd* field containing the command name plus possibly some parameters. Commands directed to a specific feature always need the *feature* parameter.
- Setting a property consists of a propertyname:value JSON field
- The *init* command is special in that it always has a single parameter named after the feature to initialize, it's value being the configuration for the feature (just *true* in simple features w/o config, but often a JSON object containing config data in a feature specific format.

See below for examples

### global commands

```json
{ "cmd":"status" }
```

- get status of all features

```json
{ "event":{ ... } }
```

- inject an event (that might be processed by custom p44script on device).

### common to all features

```json
{ "cmd":"status", "feature": "<featurename>" }
```

- get status of all features

```json
{ "logleveloffset":<offset>, "feature": "<featurename>" }
```

- set log level offset property for a feature (making its log more/less verbose)


### Dispmatrix

#### Initialisation

```json
{ "cmd":"init", "dispmatrix": { "installationX": <overall-x>, "installationY": <overall-y>, "rootview": <p44lrgraphics-view-config> }
```

- *x,y*: position of this p44feature unit in a longer scroller consisting of multiple p44feature based hardware units. This allows sending the same global scrolling offsets to all units, and each unit interprets it according to its position in the overall installation.
- if no *p44lrgraphics-view-config* is specified, the root view will be set to a scroller labelled "DISPSCROLLER" filling the entire LEDarrangement. If a custom view config is passed, it should contain a scroller named "DISPSCROLLER".
- *p44lrgraphics-view-config* can be the view config JSON object itself or a resource file name. A relative filename will be searched in the /dispmatrix subdirectory of the app's resource directory.

#### Start scrolling

```json
{ "feature":"dispmatrix", "cmd":"startscroll", "stepx":<x-step-size>, "stepy": <y-step-size>, "steps": <num-steps>, "interval": <step-interval-seconds>, "roundoffsets": <bool>, "start": <absolute_unix-time-in-seconds> }
```

- step sizes can be fractional
- *num-steps* can be negative for unlimited scrolling
- *roundoffsets* causes the current scroll offsets to be rounded to integer number before starting scroll
- *absolute_unix-time-in-seconds* allows strictly synchronized scrolling start over multiple (NTP synchronized!) hardware units. if `null` is passed, scrolling starts at the next 10-second boundary in absolute unix time.

#### Stop scrolling

```json
{ "feature":"dispmatrix", "cmd":"stopscroll" }
```

#### Fade Alpha

```json
{ "feature":"dispmatrix", "cmd":"fade", "to": <target-alpha>, "t": <fade-time-in-seconds> }
```

#### (Re)Configure a view

```json
{ "feature":"dispmatrix", "cmd":"configure", "view": <view-label>, "config": <p44lrgraphics-view-config> }
```

#### Set a "scene"

```json
{ "feature":"dispmatrix", "scene":<p44lrgraphics-view-config> }
```

- Setting a scene means replacing the "DISPSCROLLER"'s scrolled view by a new view. This special command makes sure changing the scrolled view's works with wraparound scrolling over multiple modules.

#### Set scroll offsets

```json
{ "feature":"dispmatrix", "offsetx":<offset-x> }
{ "feature":"dispmatrix", "offsety":<offset-y> }
```

- This sets the content's scroll position, relative to the configured *installationX/Y* offsets

### Indicators

#### Initialisation

```json
{ "cmd":"init", "indicators": { "rootview": <p44lrgraphics-view-config> } }
```

- if no *p44lrgraphics-view-config* is specified, the root view will be set to a stack labelled "INDICATORS" filling the entire LEDarrangement. If a custom view config is passed, it should contain a stack named "INDICATORS".

#### Show an indicator

```json
{ "feature":"indicators", "cmd":"indicate", "x":<x-start>, "dx":<x-size>, "y":<y-start>, "dy":<y-size>, "effect":"<effect-name-or-viewconfig>", "t":<effect-duration-in-seconds>, "color":"<web-color>" }
```

- *effect-name-or-viewconfig* can be one of the built-in effects (at this time: "*plain*", "*swipe*", "*pulse*", "*spot*") or configuration of a view (including animations) to be shown at the specified coordinates. The view can be specified as inline JSON or via specifying the resource name of a resource file. A relative filename will be searched in the /indicators subdirectory of the app's resource directory.


### RFIDs

#### Initialisation

```json
{ "cmd":"init", "rfids": { "readers": [<index>, <index>,...], "pollinterval":<pollinterval_seconds>, "sameidtimeout":<re-reporting-timeout> "pauseafterdetect":<poll-pause-after-card-detected> } }
```

- *index* are the physical bus addresses (0..23 in p44rfidctrl and p44rfidhat hardware) to select the reader. Depending on which readers on which cables are in use, this can be a sequence of numbers with or without gaps.
- *pollinterval_seconds* is the  polling interval for the connected readers, i.e. how often every reader will be checked for the presence of a new RFID tag (default: 0.1 seconds)
- *re-reporting-timeout* is the time during which a reader will not report the _same nUID again (default: 3 seconds)
- *poll-pause-after-card-detected* is the time polling RFIDs will be paused after detecting a card - mainly to free performance for LED effects, as SPI on RPi seems to block a lot (default: 1 second)


#### API events

```json
{ "feature":"rfids", "nUID":"<rfid_nUID>", "reader":<rfid-reader-index> }
```

- *rfid_nUID* is the nUID of the RFID tag seen
- *rfid-reader-index* is the physical bus address of the reader which has seen the RFID tag


### Splitflaps

#### Initialisation

For individual modules connected to RS485 bus:

```json
{
  "cmd":"init",
  "splitflaps": {
    "modules":[
      {"name":"<module_name>","addr":<module_address>,"type":"<alphanum|hour|minute|40|62>"},
      ...
    ]
  }
}
```

- The "modules" array defines the splitflaps to be used.
- *name* is a handle for the module to access it with the *position* command later (see below).
- *module_address* is the module address of the splitflap module (for SBB (Swiss Railways) modules, usually written onto a yellow sticker on the module itself, but as the address can be reprogrammed, the sticker might not be correct in all cases).
- the "type" field specifies the module kind. *hour* and *minute* are basically the same as *40* and *62*, however the flap indices are made to match the actual numerical display (the flaps are not in strict ascending order in those modules). *40* and *62* are for generic modules with 40 and 62 flaps, resp. 


For older modules connected via a "omega controller" board:

```json
{
  "cmd":"init",
  "splitflaps": {
    "controller":51,
    "lines":2,
    "columns":6,
    "modules":[
      {"name":"<module_name>","addr":<line*100+column>,"type":"<alphanum|hour|minute|40|62>"},
      ...
    ]
  }
}
```

- *controller* is the controller address of the "omega controller" board connected
- *lines*, *columns* define the layout of the modules - the omega controller acts like a terminal with lines and columns, each character position represents a module. The module *addr* must be specified as (zero based) linenumber*100+columnnumber, so *addr*=102 would be the third module on the second line. Standard SBB "Gleisanzeiger" two-sided boxes usually represent one side on line 0, the other side on line 1.

#### Set splitflap position

```json
{ "feature":"splitflaps", "cmd":"position", "name":"<module_name>", "value":<flap_number|alphanum-char> }
```

Sets the module that was named *module_name* at initialisation (see above) to position *flap_number*, starting at 0 for the first flap (in *42* und *60* type modules, and with hour 0/minute 00 for *hour* and *minute* type modules. For *alphanum* type modules, *alphanum-char* is the character to display (only `A`-`Z`,`0`-`9`,`-`,`.`,`/` and space).

#### Get splitflap position

```json
{ "feature":"splitflaps", "cmd":"position", "name":"<module_name>" }
```

Without the *value* parameter, the *position* command returns the module's current value.
However, at this time, the feature does not actually read back current positions - the value returned is just the last value set for this module via the *position* command.

### Generic Inputs

#### Initialisation

```json
{ "cmd":"init", "inputs": { "<input_name>":{ "pin":"<pin_spec>", "initially":<initial_bool>, "debouncing":<debounce-in-seconds>, "pollinterval":<pollinterval-in-seconds> } , ... } }
```

- *input_name* is a name which is included in the API events to identify the input.
- *pin_spec* is a p44utils DigitalIo pin specification
- optional *initial_bool* is the initial value the input is assumed to have (to force/prevent event at init)
- optional *debounce-in-seconds* sets debouncing time (default = 80mS)
- optional *pollinterval-in-seconds* sets polling interval (default = 250mS). This is only for inputs which do not have system level edge detection.

#### API events

```json
{ "feature":"inputs", "*input_name*":*input_value* }
```

- *input_name* is a name of the input.
- *input_value* is the current value of the input

Supporting p44features
----------------------

1. use it!
2. support development via [github sponsors](https://github.com/sponsors/plan44) or [flattr](https://flattr.com/@luz)
3. Discuss it in the [plan44 community forum](https://forum.plan44.ch/t/opensource-c-vdcd).
3. contribute patches, report issues and suggest new functionality [on github](https://github.com/plan44/p44features) or in the [forum](https://forum.plan44.ch/t/opensource-c-vdcd).
4. Create cool new *features*!
5. Buy plan44.ch [products](https://plan44.ch/automation/products.php) - sales revenue is paying the time for contributing to opensource projects :-)

(c) 2013-2022 by Lukas Zeller / [plan44.ch](https://www.plan44.ch/opensource.php)
