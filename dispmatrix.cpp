//
//  Copyright (c) 2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
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

#include "dispmatrix.hpp"

#if ENABLE_FEATURE_DISPMATRIX

#include "application.hpp"

#include "viewfactory.hpp"
#include "featureapi.hpp"

#define LED_MODULE_COLS 74
#define LED_MODULE_ROWS 7
#define LED_MODULE_BORDER_LEFT 1
#define LED_MODULE_BORDER_RIGHT 1


using namespace p44;


// MARK: ===== DispMatrix

#define FEATURE_NAME "dispmatrix"


DispMatrix::DispMatrix(LEDChainArrangementPtr aLedChainArrangement, const string aViewCfgStr) :
  inherited(FEATURE_NAME),
  ledChainArrangement(aLedChainArrangement),
  installationOffsetX(0),
  installationOffsetY(0)
{
  // create the root view
  if (ledChainArrangement) {
    // check for commandline-triggered standalone operation, adding views from config
    if (aViewCfgStr!="0" && aViewCfgStr!="none") {
      // json root view config or name of resource json file
      ErrorPtr err;
      JsonObjectPtr cfg = Application::jsonObjOrResource(aViewCfgStr, &err, FEATURE_NAME "/");
      if (Error::isOK(err)) {
        initialize(cfg);
      }
      else {
        OLOG(LOG_ERR, "configuration failed: %s", err->text());
      }
    }
  }
}


void DispMatrix::reset()
{
  if (ledChainArrangement) ledChainArrangement->end();
  if (rootView) rootView->clear();
  if (dispScroller) dispScroller.reset();
  inherited::reset();
}


DispMatrix::~DispMatrix()
{
  reset();
}


// MARK: ==== dispmatrix API

#define DISPSCROLLER_LABEL "DISPSCROLLER"

ErrorPtr DispMatrix::initialize(JsonObjectPtr aInitData)
{
  reset();
  // { "cmd":"init", "dispmatrix": { "installationX":<x-offset>, "installationY":<y-offset> } }
  // { "cmd":"init", "dispmatrix": { "installationX":<x-offset>, "installationY":<y-offset>, "rootview": <p44lrgraphics-view-config> } }
  // { "cmd":"init", "dispmatrix": { "ledchains": [ "ledchainspec1", "ledchainspec2", ... ], "installationX":<x-offset>, "installationY":<y-offset>, "rootview": <p44lrgraphics-view-config> } }
  JsonObjectPtr o;
  ErrorPtr err;
  if (ledChainArrangement) {
    if (aInitData->get("ledchains", o)) {
      // ledchain re-arrangement from config
      // - forget default arrangement
      ledChainArrangement->removeAllChains();
      // - add chains from array of strings
      for (int i=0; i<o->arrayLength(); ++i) {
        JsonObjectPtr ao = o->arrayGet(i);
        ledChainArrangement->addLEDChain(ao->stringValue());
      }
      ledChainArrangement->startChains(); // start chains that are not yet operating
    }
    // get the ledChainArrangement's current rootview
    rootView = ledChainArrangement->getRootView();
    if (aInitData->get("installationX", o)) {
      installationOffsetX = o->int32Value();
    }
    if (aInitData->get("installationY", o)) {
      installationOffsetY = o->int32Value();
    }
    if (aInitData->get("rootview", o)) {
      // create/replace or reconfigure rootview (depending on having 'type' in the config)
      err = createViewFromResourceOrObj(o, FEATURE_NAME "/", rootView, P44ViewPtr());
    }
    if (rootView) {
      // there is a rootview, it should contain a scroller
      dispScroller = boost::dynamic_pointer_cast<ViewScroller>(rootView->getView(DISPSCROLLER_LABEL));
    }
    else {
      // no existing or explicitly initialized rootview: install default scroller as root
      PixelRect r = ledChainArrangement->totalCover();
      dispScroller = ViewScrollerPtr(new ViewScroller);
      dispScroller->setFrame(r);
      dispScroller->setFullFrameContent();
      dispScroller->setBackgroundColor(black); // stack with black background is more efficient (and there's nothing below, anyway)
      dispScroller->setDefaultLabel(DISPSCROLLER_LABEL);
      // the scroller is the root view
      rootView = dispScroller;
    }
    if (dispScroller) {
      dispScroller->setOffsetX(installationOffsetX);
      dispScroller->setOffsetY(installationOffsetY);
    }
    if (Error::isOK(err)) {
      // install root view
      ledChainArrangement->setRootView(rootView);
      // start running
      initOperation();
    }
  }
  else {
    err = TextError::err("no led chains configured");
  }
  return err;
}



#define MIN_SCROLL_STEP_INTERVAL (20*MilliSecond)

ErrorPtr DispMatrix::processRequest(ApiRequestPtr aRequest)
{
  ErrorPtr err;
  JsonObjectPtr data = aRequest->getRequest();
  JsonObjectPtr o = data->get("cmd");
  if (o) {
    // decode commands
    string cmd = o->stringValue();
    if (cmd=="stopscroll") {
      if (dispScroller) dispScroller->stopScroll();
      return Error::ok();
    }
    else if (cmd=="startscroll") {
      double stepx = 1;
      double stepy = 0;
      long steps = -1; // forever
      bool roundoffsets = true;
      MLMicroSeconds interval = 22*MilliSecond;
      MLMicroSeconds start = Never; // right away
      if (data->get("stepx", o, true)) {
        stepx = o->doubleValue();
      }
      if (data->get("stepy", o, true)) {
        stepy = o->doubleValue();
      }
      if (data->get("steps", o, true)) {
        steps = o->int64Value();
      }
      if (data->get("interval", o, true)) {
        interval = o->doubleValue()*Second;
      }
      if (data->get("roundoffsets", o, true)) {
        roundoffsets = o->boolValue();
      }
      if (data->get("start", o, false)) {
        MLMicroSeconds st;
        if (!o) {
          // null -> next 10-second boundary in unix time
          st = (uint64_t)((MainLoop::unixtime()+10*Second)/10/Second)*10*Second;
        }
        else {
          st = o->doubleValue()*Second;
        }
        start = MainLoop::unixTimeToMainLoopTime(st);
      }
      if (interval<MIN_SCROLL_STEP_INTERVAL) interval = MIN_SCROLL_STEP_INTERVAL;
      if (dispScroller) dispScroller->startScroll(stepx, stepy, interval, roundoffsets, steps, start);
      return Error::ok();
    }
    else if (cmd=="scrollstatus") {
      bool last = true;
      bool purge = false;
      if (data->get("last", o)) last = o->boolValue();
      if (data->get("purge", o)) purge = o->boolValue();
      JsonObjectPtr answer = JsonObject::newObj();
      answer->add("remainingtime", JsonObject::newDouble((double)getRemainingScrollTime(last, purge)/Second));
      // - return
      aRequest->sendResponse(answer, ErrorPtr());
      return ErrorPtr();
    }
    else if (cmd=="fade") {
      #if ENABLE_ANIMATION
      int to = 255;
      MLMicroSeconds t = 300*MilliSecond;
      if (data->get("to", o, true)) {
        to = o->int32Value();
      }
      if (data->get("t", o, true)) {
        t = o->doubleValue()*Second;
      }
      if (dispScroller) dispScroller->animatorFor("alpha")->animate(to, t);
      #endif
      return Error::ok();
    }
    else if (cmd=="configure") {
      if (data->get("view", o)) {
        string viewLabel = o->stringValue();
        JsonObjectPtr viewConfig = data->get("config");
        if (viewConfig) {
          P44ViewPtr view = rootView->getView(viewLabel);
          if (view) err = view->configureFromResourceOrObj(viewConfig, FEATURE_NAME "/");
          return err ? err : Error::ok();
        }
      }
      return TextError::err("missing 'view' and/or 'config'");
    }
    return inherited::processRequest(aRequest);
  }
  else {
    // decode properties
    if (data->get("scene", o, true)) {
      o = Application::jsonObjOrResource(o, &err, FEATURE_NAME "/");
      if (!Error::isOK(err)) return err;
      if (dispScroller) {
        P44ViewPtr sceneView = dispScroller->getScrolledView();
        if (sceneView) {
          // due to offset wraparound according to scrolled view's content size (~=text length)
          // current offset might be smaller than panel's offsetX right now. This must be
          // adjusted BEFORE content size changes
          double ox = dispScroller->getOffsetX();
          double cx = dispScroller->getContentSize().x;
          while (cx>0 && ox<installationOffsetX) ox += cx;
          dispScroller->setOffsetX(ox);
          sceneView.reset();
          dispScroller->setScrolledView(sceneView);
        }
        // get new contents view hierarchy
        err = p44::createViewFromConfig(o, sceneView, dispScroller);
        if (Error::notOK(err))
          return err; // abort early, other properties most likely need the scene in place
        dispScroller->setScrolledView(sceneView);
      }
    }
    if (data->get("text", o, true)) {
      string msg = o->stringValue();
      TextViewPtr textview = boost::dynamic_pointer_cast<TextView>(rootView->getView("TEXT"));
      if (textview) textview->setText(msg);
    }
    if (data->get("color", o, true)) {
      // of the text
      PixelColor p = webColorToPixel(o->stringValue());
      TextViewPtr textview = boost::dynamic_pointer_cast<TextView>(rootView->getView("TEXT"));
      if (textview) textview->setForegroundColor(p);
    }
    if (data->get("spacing", o, true)) {
      // of the text
      int spacing = o->int32Value();
      TextViewPtr textview = boost::dynamic_pointer_cast<TextView>(rootView->getView("TEXT"));
      if (textview) textview->setTextSpacing(spacing);
    }
    if (data->get("bgcolor", o, true)) {
      // of the entire content view
      PixelColor p = webColorToPixel(o->stringValue());
      P44ViewPtr contentView = dispScroller->getScrolledView();
      if (contentView) contentView->setBackgroundColor(p);
    }
    if (data->get("offsetx", o, true)) {
      // of the scroller, additionally offset by installation offset
      double offs = o->doubleValue();
      if (dispScroller) dispScroller->setOffsetX(offs+installationOffsetX);
    }
    if (data->get("offsety", o, true)) {
      // of the scroller, additionally offset by installation offset
      double offs = o->doubleValue();
      if (dispScroller) dispScroller->setOffsetY(offs+installationOffsetY);
    }
    return err ? err : Error::ok();
  }
}


JsonObjectPtr DispMatrix::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    answer->add("unixtime", JsonObject::newDouble((double)MainLoop::unixtime()/Second));
    if (dispScroller) {
      answer->add("brightness", JsonObject::newInt32(dispScroller->getAlpha()));
      answer->add("scrolloffsetx", JsonObject::newDouble(dispScroller->getOffsetX()));
      answer->add("scrolloffsety", JsonObject::newDouble(dispScroller->getOffsetY()));
      answer->add("scrollstepx", JsonObject::newDouble(dispScroller->getStepX()));
      answer->add("scrollstepy", JsonObject::newDouble(dispScroller->getStepY()));
      answer->add("scrollsteptime", JsonObject::newDouble((double)dispScroller->getScrollStepInterval()/Second));
      P44ViewPtr contents = dispScroller->getScrolledView();
      if (contents) {
        // scrolled view = contents
        answer->add("bgcolor", JsonObject::newString(pixelToWebColor(contents->getBackgroundColor())));
      }
      TextViewPtr text = boost::dynamic_pointer_cast<TextView>(contents);
      if (text) {
        // text view
        answer->add("color", JsonObject::newString(pixelToWebColor(text->getForegroundColor())));
        answer->add("text", JsonObject::newString(text->getText()));
        answer->add("spacing", JsonObject::newInt32(text->getTextSpacing()));
      }
      #if ENABLE_VIEWSTATUS
      answer->add("scrollview", dispScroller->viewStatus());
      #endif
    }
    #if ENABLE_VIEWSTATUS
    answer->add("rootview", rootView->viewStatus());
    #endif
  }
  return answer;
}


// MARK: ==== dispmatrix operation

void DispMatrix::initOperation()
{
  if (ledChainArrangement) {
    ledChainArrangement->begin(true);
  }
  else {
    OLOG(LOG_WARNING, "NOP: no ledchain connected");
  }
  setInitialized();
}


void DispMatrix::setNeedContentHandler(NeedContentCB aNeedContentCB)
{
  if (dispScroller) dispScroller->setNeedContentHandler(aNeedContentCB);
}


MLMicroSeconds DispMatrix::getRemainingScrollTime(bool aLast, bool aPurge)
{
  MLMicroSeconds rem = Infinite;
  if (dispScroller) {
    rem = dispScroller->remainingScrollTime();
    if (aPurge) dispScroller->purgeScrolledOut();
  }
  return rem;
}



void DispMatrix::resetScroll()
{
  if (dispScroller) {
    dispScroller->setOffsetX(0);
    dispScroller->setOffsetY(0);
    P44ViewPtr contents = dispScroller->getScrolledView();
    if (contents) {
      PixelRect f = contents->getFrame();
      f.x = 0;
      f.y = 0;
      contents->setFrame(f);
    }
  }
}



#endif // ENABLE_FEATURE_DISPMATRIX
