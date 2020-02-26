//
//  Copyright (c) 2016-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44features.
//
//  p44lrgraphics is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44lrgraphics is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44lrgraphics. If not, see <http://www.gnu.org/licenses/>.
//


#ifndef __p44features__config__
#define __p44features__config__

// NOTE: This is a default (template) config file only
//                 ******************
//       Usually, copy this file into a location where is is found BEFORE this
//       default file is found, and modify the copied version according to
//       your needs.
//       DO NOT MODIFY THE ORIGINAL IN the p44utils directory/git submodule!

// Features:
// - generic use
#define ENABLE_FEATURE_LIGHT 0
#define ENABLE_FEATURE_DISPMATRIX 0
#define ENABLE_FEATURE_RFIDS 0
#define ENABLE_FEATURE_INDICATORS 0
// - specific application
#define ENABLE_FEATURE_WIFITRACK 0
// - very specific hardware related stuff
#define ENABLE_FEATURE_NEURON 0
#define ENABLE_FEATURE_HERMEL 0
#define ENABLE_FEATURE_MIXLOOP 0

// dependencies
#define ENABLE_LEDARRANGEMENT (ENABLE_FEATURE_DISPMATRIX || ENABLE_FEATURE_INDICATORS)


#endif // __p44features__config__
