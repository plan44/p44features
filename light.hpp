//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Ueli Wahlen <ueli@hotmail.com>
//
//  This file is part of p44features.
//
//  p44features is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44features is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44features. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __p44features_light_hpp__
#define __p44features_light_hpp__

#include "feature.hpp"

#if ENABLE_FEATURE_LIGHT

#include "analogio.hpp"
#include "valueanimator.hpp"

#include <math.h>

namespace p44 {

  #if ENABLE_P44SCRIPT
  namespace P44Script {

    /// represents a single "feature"
    class LightObj : public FeatureObj
    {
      typedef FeatureObj inherited;
      friend class Light;
    public:
      LightObj(FeaturePtr aFeature);
      ValueAnimatorPtr animator();
    };

  } // namespace P44Script
  #endif // ENABLE_P44SCRIPT

  class Light : public Feature
  {
    typedef Feature inherited;
    friend class P44Script::LightObj;

    AnalogIoPtr pwmDimmer;
    ValueAnimatorPtr animator;


    double currentValue = 0;
    double to;
    double dv;
    MLTicket ticket;

  public:

    Light(AnalogIoPtr aPwmDimmer, bool doStart);

    void fade(double aFrom, double aTo, MLMicroSeconds aFadeTime, MLMicroSeconds aStartTime);
    double current();

    /// initialize the feature
    /// @param aInitData the init data object specifying feature init details
    /// @return error if any, NULL if ok
    virtual ErrorPtr initialize(JsonObjectPtr aInitData) override;

    /// handle request
    /// @param aRequest the API request to process
    /// @return NULL to send nothing at return (but possibly later via aRequest->sendResponse),
    ///   Error::ok() to just send a empty response, or error to report back
    virtual ErrorPtr processRequest(ApiRequestPtr aRequest) override;

    /// @return status information object for initialized feature, bool false for uninitialized
    virtual JsonObjectPtr status() override;

    #if ENABLE_P44SCRIPT
    /// @return a new script object representing this feature. Derived device classes might return different types of device object.
    virtual ScriptObjPtr newFeatureObj() override;
    #endif

  private:

    void initOperation();

    // PWM    = PWM value
    // maxPWM = max PWM value
    // B      = brightness
    // maxB   = max brightness value
    // S      = dim Curve Exponent (1=linear, 2=quadratic, ...)
    //
    //                   (B*S/maxB)
    //                 e            - 1
    // PWM =  maxPWM * ----------------
    //                      S
    //                    e   - 1
    //
    double brightnessToPWM(double aBrightness);
    void startFading(double aTo, MLMicroSeconds aFadeTime);

    ErrorPtr fade(ApiRequestPtr aRequest);

  };

} // namespace p44


#endif // ENABLE_FEATURE_LIGHT

#endif /* __p44features_light_hpp__ */
