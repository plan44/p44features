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

#ifndef __p44features_rfids_hpp__
#define __p44features_rfids_hpp__

#include "feature.hpp"

#if ENABLE_FEATURE_RFIDS

#include "rfid.hpp"

namespace p44 {

  #define POLLING_IRQ 1

  class RFIDReader
  {
  public:
    RFIDReader() : lastDetect(Never) {};
    RFID522Ptr reader;
    MLMicroSeconds lastDetect;
    string lastNUID;
  };


  class RFIDs : public Feature
  {
    typedef Feature inherited;

    typedef std::map<int,RFIDReader> RFIDReaderMap;

    SPIDevicePtr spiDevice; ///< the generic SPI device where readers are connected
    RFID522::SelectCB readerSelectFunc; ///< the function to select a specific reader by index
    DigitalIoPtr resetOutput; ///< common reset output
    DigitalIoPtr irqInput; ///< common IRQ input
    RFIDReaderMap rfidReaders; ///< the active RFID readers

    MLMicroSeconds rfidPollInterval; ///< poll interval
    MLMicroSeconds sameIdTimeout; ///< how long the same ID will not get re-reported
    MLMicroSeconds pollPauseAfterDetect; ///< how long polling pauses after a card detection (to free performance for effects...)

    MLTicket startupTimer; ///< timer for startup timing
    bool pauseIrqHandling; ///< flag to suspend irq checking for a moment

    #if POLLING_IRQ
    MLTicket irqTimer; ///< timer for polling IRQ
    #endif

  public:

    /// create set of RFID522 readers
    /// @param aSPIGenericDev a generic SPI device for the bus the readers are connected to
    /// @param aReaderSelectFunc will be called to select readers by index
    /// @param aResetOutput output that resets the RFID readers
    /// @param aIRQInput input connected to the IRQ lines of the RFID readers
    RFIDs(SPIDevicePtr aSPIGenericDev, RFID522::SelectCB aReaderSelectFunc, DigitalIoPtr aResetOutput, DigitalIoPtr aIRQInput);
    virtual ~RFIDs();

    /// reset the feature to uninitialized/re-initializable state
    virtual void reset() override;

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

    void resetReaders(SimpleCB aDoneCB);
    void releaseReset(SimpleCB aDoneCB);
    void resetDone(SimpleCB aDoneCB);

    void initOperation();
    void initReaders();

    void irqHandler(bool aState);
    void haltIrqHandling();
    void pollIrq(MLTimer &aTimer);
    void detectedCard(RFID522Ptr aReader, ErrorPtr aErr);
    void gotCardNUID(RFID522Ptr aReader, ErrorPtr aErr, const string aNUID);

    void rfidRead(MLTimer& aTimer);

    void rfidDetected(int aReaderIndex, const string aRFIDnUID);

  };

} // namespace p44


#endif // ENABLE_FEATURE_RFIDS

#endif /* __p44features_rfids_hpp__ */
