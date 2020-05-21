//
//  Copyright (c) 2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44features.
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

#ifndef __p44features_dispmatrix_hpp__
#define __p44features_dispmatrix_hpp__

#include "feature.hpp"

#if ENABLE_FEATURE_DISPMATRIX

#include "ledchaincomm.hpp"

#include "viewstack.hpp"
#include "viewscroller.hpp"
#include "textview.hpp"


namespace p44 {

  class DispMatrix : public Feature
  {
    typedef Feature inherited;

    LEDChainArrangementPtr ledChainArrangement;
    P44ViewPtr rootView; // the root view
    ViewScrollerPtr dispScroller; // the view that receives global scroll commands
    int installationOffsetX; ///< X offset within an installation of multiple displays
    int installationOffsetY; ///< Y offset within an installation of multiple displays

  public:

    DispMatrix(LEDChainArrangementPtr aLedChainArrangement);
    virtual ~DispMatrix();

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

    /// set a handler that is called when the display runs out of display content (scrolled out)
    void setNeedContentHandler(NeedContentCB aNeedContentCB);

    /// get the remaining time until the first (aLast==false) or the last (aLast==true) panel runs out of content
    /// @param aLast if set, get remaining scroll time of the last panel that will run out of content, if cleared, of the first
    /// @param aPurge if set, scrolled out views will be purged
    MLMicroSeconds getRemainingScrollTime(bool aLast, bool aPurge);

    /// @return the main display scroller view (the one named "DISPSCROLLER"
    ViewScrollerPtr getDispScroller() { return dispScroller; }

    /// resets scrolling: panels are reset to their initial scroll offset, scrolledView's frame is reset to 0,0
    void resetScroll();

  private:

    void initOperation();

  };
  typedef boost::intrusive_ptr<DispMatrix> DispMatrixPtr;



} // namespace p44

#endif // ENABLE_FEATURE_DISPMATRIX

#endif /* __p44features_dispmatrix_hpp__ */
