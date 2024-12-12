//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44features_splitflaps_hpp__
#define __p44features_splitflaps_hpp__

#include "feature.hpp"
#include "serialqueue.hpp"
#include "digitalio.hpp"

#if ENABLE_FEATURE_SPLITFLAPS

namespace p44 {

  typedef boost::function<void (const string &aResponse, ErrorPtr aError)> SBBResultCB;

  typedef enum {
    moduletype_alphanum,
    moduletype_hour,
    moduletype_minute,
    moduletype_40,
    moduletype_62
  } SbbModuleType;


  class SplitflapModule
  {
  public:
    SplitflapModule() { mAddr = 0; mType = moduletype_62; mLastSentValue = ' '; }
    string mName;
    uint16_t mAddr; ///< for RS485 bus modules: the module's address, for Omega Controller: 100*line + char
    SbbModuleType mType;
    uint8_t mLastSentValue;
  };


  class Splitflaps : public Feature
  {
    typedef Feature inherited;

    SerialOperationQueue mSbbSerial; ///< the serial communication with the SBB splitflap modules

    bool mSimulation; ///< if set, simulate only, no RS485 output

    enum {
      interface_rs485bus,
      interface_omegacontroller
    } mInterfaceType;

    DigitalIoPtr mTxEnable;
    DigitalIoPtr mRxEnable;
    enum {
      txEnable_none,
      txEnable_io,
      txEnable_dtr,
      txEnable_rts
    } mTxEnableMode;
    MLMicroSeconds mTxOffDelay;
    MLTicket mTxOffTicket; ///< Tx control

    typedef std::vector<SplitflapModule> SplitFlapModuleVector;
    SplitFlapModuleVector mSplitflapModules;

    int mOCtrlAddress; ///< the Omega Controller Address
    uint16_t mOCtrlLines; ///< number of pseudo-terminal "lines"
    uint16_t mOCtrlColumns; ///< number of pseudo-terminal "columns"
    string mOCtrlData; ///< pseudo terminal data buffer
    bool mOCtrlDirty; ///< set when buffer needs to be re-sent
    MLTicket mOCtrlUpdater;

  public:

    /// create split flaps interface (SBB RS485 bus modules)
    /// @param aConnectionSpec serial device path (/dev/...) or host name/address[:port] (1.2.3.4 or xxx.yy)
    /// @param aDefaultPort default port number for TCP connection (irrelevant for direct serial device connection)
    /// @param aTxEnablePinSpec the digital output line to be used for enabling RS485 transmitter
    /// @param aRxEnablePinSpec the digital output line to be used for enabling RS485 receiver
    /// @param aOffDelay how long to keep TX enabled after enableSending(false)
    Splitflaps(
      const char *aConnectionSpec, uint16_t aDefaultPort,
      const char *aTxEnablePinSpec, const char *aRxEnablePinSpec, MLMicroSeconds aOffDelay
    );
    virtual ~Splitflaps();

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

    /// send raw command
    void sendRawCommand(const string aCommand, size_t aExpectedBytes, SBBResultCB aResultCB, MLMicroSeconds aInitiationDelay);

    /// set the value to display in a module
    /// @param aSplitFlapModule the module
    /// @param aValue the value to show.
    void setModuleValue(SplitflapModule& aSplitFlapModule, uint8_t aValue);

    /// get the value from a module (or its cache)
    /// @param aSplitFlapModule the module
    uint8_t getModuleValue(SplitflapModule& aSplitFlapModule);

  private:

    void initOperation();

    void initBusOperation();
    void initCtrlOperation();

    void rawCommandAnswer(ApiRequestPtr aRequest, const string &aResponse, ErrorPtr aError);

    void enableSending(bool aEnable);
    void enableSendingImmediate(bool aEnable);
    ssize_t acceptExtraBytes(size_t aNumBytes, const uint8_t *aBytes);

    /// RS485 Bus
    size_t sbbBusTransmitter(size_t aNumBytes, const uint8_t *aBytes);
    void sendRawBusCommand(const string aCommand, size_t aExpectedBytes, SBBResultCB aResultCB, MLMicroSeconds aInitiationDelay);
    void sbbBusCommandComplete(SBBResultCB aResultCB, SerialOperationPtr aSerialOperation, ErrorPtr aError);
    void setModuleValueBus(uint8_t aModuleAddr, SbbModuleType aType, uint8_t aValue);

    /// Omega Controller
    void sendRawCtrlCommand(const string aCommand, SBBResultCB aResultCB);
    void sbbCtrlCommandComplete(SBBResultCB aResultCB, ErrorPtr aError);
    void setModuleValueCtrl(uint8_t aLine, uint8_t aColumn, SbbModuleType aType, uint8_t aValue);
    void sendCtrlCommand(const char *aCmd, SBBResultCB aResultCB);
    void setCtrlDirty();
    void updateCtrlDisplay();


  };

} // namespace p44


#endif // ENABLE_FEATURE_SPLITFLAPS

#endif /* __p44features_splitflaps_hpp__ */
