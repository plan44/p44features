//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  This file is part of p44features.
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

#ifndef __p44features_wifitrack_hpp__
#define __p44features_wifitrack_hpp__

#include "feature.hpp"

#if ENABLE_FEATURE_WIFITRACK

#include "p44view.hpp"
#include "dispmatrix.hpp"

#include <math.h>
#include <set>

namespace p44 {

  class WTMac;
  typedef boost::intrusive_ptr<WTMac> WTMacPtr;
  class WTSSid;
  typedef boost::intrusive_ptr<WTSSid> WTSSidPtr;
  class WTPerson;
  typedef boost::intrusive_ptr<WTPerson> WTPersonPtr;

  typedef std::map<uint64_t, WTMacPtr> WTMacMap;
  typedef std::map<string, WTSSidPtr> WTSSidMap;

  typedef std::set<WTMacPtr> WTMacSet;
  typedef std::set<WTSSidPtr> WTSSidSet;
  typedef std::set<WTPersonPtr> WTPersonSet;


  typedef std::map<uint32_t, const char*> OUIMap;



  class WTMac : public P44Obj
  {
  public:

    WTMac();

    MLMicroSeconds seenLast;
    MLMicroSeconds seenFirst;
    long seenCount;
    int lastRssi;
    int bestRssi;
    int worstRssi;
    uint64_t mac;
    const char *ouiName;
    bool hidden;

    WTSSidSet ssids;
    WTPersonPtr person;
  };


  class WTSSid : public P44Obj
  {
  public:

    WTSSid();

    MLMicroSeconds seenLast;
    long seenCount;
    string ssid;
    bool hidden;

    int beaconRssi;
    MLMicroSeconds beaconSeenLast;

    WTMacSet macs;

  };



  class WTPerson : public P44Obj
  {
  public:

    WTPerson();

    MLMicroSeconds seenLast;
    MLMicroSeconds seenFirst;
    long seenCount;
    int lastRssi;
    int bestRssi;
    int worstRssi;

    PixelColor color;
    int imageIndex;
    string name;
    bool hidden;

    MLMicroSeconds shownLast;

    WTMacSet macs;

  };




  class WifiTrack : public Feature
  {
    typedef Feature inherited;

    string mMonitorIf;
    int mDumpPid;
    FdCommPtr mDumpStream;

    MLTicket mRestartTicket;

    WTMacMap mMacs;
    WTSSidMap mSsids;
    WTPersonSet mPersons;

    OUIMap mOuis;

    // settings
    bool mOuiNames;
    bool mReportSightings;
    bool mAggregatePersons;
    bool mRememberWithoutSsid;
    MLMicroSeconds mMinShowInterval;
    int mMinRssi; ///< minimal rssi, will be passed to tcpdump as packet filter if not 0
    bool mScanBeacons; ///< if set, beacons will be processed and remembered in addition to probe requests
    int mMinProcessRssi; ///< minimal rssi to process packet
    int mRadiotapDBOffset; ///< offset of dBm info in radiotap date (dependent on wifi driver)
    int mMinShowRssi; ///< minimal rssi for triggering to show a person
    int mTooCommonMacCount;
    int mMinCommonSsidCount;
    int mNumPersonImages;
    MLMicroSeconds mMaxDisplayDelay;

    MLMicroSeconds mSaveTempInterval;
    MLMicroSeconds mSaveDataInterval;

    MLMicroSeconds mLastTempAutoSave;
    MLMicroSeconds mLastDataAutoSave;

    bool mDirectDisplay; ///< if set, local dispmatrix is used for display
    bool mApiNotify; ///< if set, send persons back to feature API client
    DispMatrixPtr mDisp;
    bool mLoadingContent;

  public:

    WifiTrack(const string aMonitorIf, int aRadiotapDBOffset, bool doStart);
    virtual ~WifiTrack();

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

    /// command line tool mode
    /// @return error if tool fails, ok otherwise
    virtual ErrorPtr runTool() override;

  private:

    void initOperation();
    void startScanner();
    void restartScanner();

    void loadOUIs();
    const char* ouiName(uint64_t aMac);

    ErrorPtr save(const string aPath);
    ErrorPtr load(const string aPath);

    JsonObjectPtr dataDump(bool aSsids = true, bool aMacs = true, bool aPersons = true, bool aOUINames = false, bool aPersonSsids = false);
    ErrorPtr dataImport(JsonObjectPtr aData);

    void dumpEnded(ErrorPtr aError);
    void gotDumpLine(ErrorPtr aError);

    void processSighting(WTMacPtr aMac, WTSSidPtr aSSid, bool aNewSSidForMac);

    void displayEncounter(string aIntro, int aImageIndex, PixelColor aColor, string aName, string aBrand, string aTarget);

    bool needContentHandler();
    void contentLoaded();

  };

} // namespace p44


#endif // ENABLE_FEATURE_WIFITRACK

#endif /* __p44features_wifitrack_hpp__ */
