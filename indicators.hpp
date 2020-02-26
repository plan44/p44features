//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  This file is part of p44featured.
//
//  pixelboardd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  pixelboardd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with pixelboardd. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __p44features_ledbars_hpp__
#define __p44features_ledbars_hpp__

#include "feature.hpp"

#if ENABLE_FEATURE_INDICATORS

#include "ledchaincomm.hpp"

namespace p44 {

  /*
  class Light : public Feature
  {
    typedef Feature inherited;

    AnalogIoPtr pwmDimmer;
    double currentValue = 0;
    const MLMicroSeconds dt = 20 * MilliSecond;
    double to;
    double dv;
    MLTicket ticket;

  public:

    Light(AnalogIoPtr aPwmDimmer);

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
    void update(MLTimer &aTimer);

    ErrorPtr fade(ApiRequestPtr aRequest);

  };
  */

} // namespace p44


#endif // ENABLE_FEATURE_INDICATORS

#endif /* __p44features_ledbars_hpp__ */
