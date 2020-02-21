//
//  Copyright (c) 2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  This file is part of p44featured
//
//  p44featured is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44featured is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44featured. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __p44features_mixloop_hpp__
#define __p44features_mixloop_hpp__

#include "p44features_common.hpp"

#if ENABLE_FEATURE_MIXLOOP

#include "ledchaincomm.hpp"
#include "spi.hpp"

#include <math.h>

namespace p44 {

  class MixLoop : public Feature
  {
    typedef Feature inherited;

    string ledChain1Name;
    LEDChainCommPtr ledChain1;
    string ledChain2Name;
    LEDChainCommPtr ledChain2;

    SPIDevicePtr accelerometer;

    MLTicket measureTicket;
    int16_t accel[3]; // X, Y, Z
    int16_t lastaccel[3]; // X, Y, Z
    double accelIntegral;
    MLMicroSeconds accelStart;
    bool hitDetectorActive;
    bool hitShowing;
    MLTicket showTicket;
    MLTicket dispTicket;
    ScriptContextPtr scriptContext;

    // parameters
    // - measurement
    uint16_t accelThreshold; ///< X,Y,Z changes below this will be ignored
    MLMicroSeconds interval; ///< sampling and integration interval
    double accelChangeCutoff; ///< offset to subtract from acceleration change value (lower end cutoff of small changes)
    double accelMaxChange; ///< max acceleration change value (top cutoff of too intense vibrations)
    double accelIntegrationGain; ///< factor for integrating acceleration change values
    double integralFadeOffset; ///< amount to subtract from integral per interval
    double integralFadeScaling; ///< amount to multiply the integral per interval
    double maxIntegral; ///< will not integrate above this
    // - hit detection: time window after seeing begin of acceleration, when a strong acceleration indicates a hit (ball trough funnel)
    double hitStartMinIntegral; ///< how high the integral must be to start the hit detector
    MLMicroSeconds hitWindowStart; ///< start of hit window (in time after seeing first acceleration)
    MLMicroSeconds hitWindowDuration; ///< size of hit window (duration)
    double hitMinAccelChange; ///< how much change in acceleration a hit needs to be
    // - display
    uint16_t numLeds; ///< number of LEDs
    double integralDispOffset; ///< offset for the integral for display (after scaling)
    double integralDispScaling; ///< scaling of the integral for display (for output scale 0..1 = none..all LEDs)
    MLMicroSeconds hitFlashTime; ///< how long the hit flash lasts
    MLMicroSeconds hitDispTime; ///< how long the hit display lasts

  public:

    MixLoop(const string aLedChain1Name, const string aLedChain2Name);

    void shoot(double aAngle, double aIntensity, MLMicroSeconds aPulseLength);

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
    void accelInit();
    void accelMeasure();
    void showAccel(double aFraction);
    void showHit();
    void showHitEnd();
    void dispNormal();

  };

} // namespace p44


#endif // ENABLE_FEATURE_MIXLOOP

#endif /* __p44features_mixloop_hpp__ */
