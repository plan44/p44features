//
//  Copyright (c) 2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of pixelboardd.
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

#include "dispmatrix.hpp"

#if ENABLE_FEATURE_DISPMATRIX

#include "application.hpp"

#include "viewfactory.hpp"

#define LED_MODULE_COLS 74
#define LED_MODULE_ROWS 7
#define LED_MODULE_BORDER_LEFT 1
#define LED_MODULE_BORDER_RIGHT 1


using namespace p44;


// MARK: ===== DispPanel

DispPanel::DispPanel(const string aChainName, int aOffsetX, int aRows, int aCols, int aBorderLeft, int aBorderRight, int aOrientation) :
  offsetX(aOffsetX),
  rows(aRows),
  cols(aCols),
  borderLeft(aBorderLeft),
  borderRight(aBorderRight),
  orientation(aOrientation),
  lastUpdate(Never)
{
  // create chain driver
  chain = LEDChainCommPtr(new LEDChainComm(LEDChainComm::ledtype_ws281x, aChainName, rows*cols, cols, false, true));
  chain->begin();
  // the scroller
  dispView = ViewScrollerPtr(new ViewScroller);
  dispView->setLabel("DISPSCROLLER");
  dispView->setFrame({ 0, 0, cols-borderLeft-borderRight, rows });
  dispView->setFullFrameContent();
  dispView->setOrientation(orientation);
  dispView->setBackgroundColor(black); // not transparent!
  // the contents will be created via setText or loadScene
  // position main view
  dispView->setOffsetX(offsetX);
  LOG(LOG_NOTICE, "- created panel with %d cols total (%d visible), %d rows, at offsetX %d, orientation %d, border left %d, right %d", cols, cols-borderLeft-borderRight, rows, offsetX, orientation, borderLeft, borderRight);
  // show operation status: dim green in first LED (if invisible), dim blue in last LED (if invisible)
  if (borderLeft>0) {
    chain->setColorXY(0, 0, 0, 100, 0);
  }
  if (borderRight>0) {
    chain->setColorXY(cols-1, rows-1, 0, 0, 100);
  }
  chain->show();
}


DispPanel::~DispPanel()
{
  chain->clear();
  chain->show();
  chain->end();
}



#define MAX_STEP_INTERVAL (1000*MilliSecond) // run a step at least in this interval
#define MAX_UPDATE_INTERVAL (500*MilliSecond) // send an update at least this often, even if no changes happen (LED refresh)
#define MIN_UPDATE_INTERVAL (15*MilliSecond) // do not send updates faster than this
#define MAX_PRIORITY_INTERVAL (50*MilliSecond) // allow synchronizing prioritized timing for this timespan after the last LED refresh


MLMicroSeconds DispPanel::step()
{
  MLMicroSeconds nextCall = Infinite;
  if (dispView) {
    do {
      nextCall = dispView->step(lastUpdate+MAX_PRIORITY_INTERVAL);
    } while (nextCall==0);
    MLMicroSeconds n = updateDisplay();
    if (nextCall<0 || (n>0 && n<nextCall)) {
      nextCall = n;
    }
  }
  return nextCall;
}


MLMicroSeconds DispPanel::updateDisplay()
{
  MLMicroSeconds now = MainLoop::now();
  if (dispView) {
    bool dirty = dispView->isDirty();
    if (dirty || now>lastUpdate+MAX_UPDATE_INTERVAL) {
      // needs update
      if (now<lastUpdate+MIN_UPDATE_INTERVAL) {
        // cannot update noew, but we should update soon
        return lastUpdate+MIN_UPDATE_INTERVAL;
      }
      else
      {
        // update now
        lastUpdate = now;
        if (dirty) {
          // update LED chain content buffer
          for (int x=borderRight; x<cols-borderLeft; x++) {
            for (int y=0; y<rows; y++) {
              PixelColor p = dispView->colorAt({ x-borderRight, y });
              PixelColor dp = dimmedPixel(p, p.a);
              chain->setColorXY(x, y, dp.r, dp.g, dp.b);
            }
          }
          dispView->updated();
        }
        // update hardware (refresh actual LEDs, cleans away possible glitches
        chain->show();
      }
    }
  }
  // latest possible update
  return now+MAX_UPDATE_INTERVAL;
}


void DispPanel::setOffsetX(double aOffsetX)
{
  if (dispView) dispView->setOffsetX(aOffsetX+offsetX);
}

void DispPanel::setBackgroundColor(const PixelColor aColor)
{
  if (contents) contents->setBackgroundColor(aColor);
}

void DispPanel::setTextColor(const PixelColor aColor)
{
  TextViewPtr message = boost::dynamic_pointer_cast<TextView>(contents->getView("TEXT"));
  if (message) message->setForegroundColor(aColor);
}

void DispPanel::setTextSpacing(int aSpacing)
{
  TextViewPtr message = boost::dynamic_pointer_cast<TextView>(contents->getView("TEXT"));
  if (message) message->setTextSpacing(aSpacing);
}



#define DEFAULT_CONTENTS_CFG "{ 'type':'text', 'label':'TEXT', 'x':0, 'y':0, 'sizetocontent':true, 'wrapmode':3 }"

void DispPanel::setText(const string aText)
{
  if (dispView) {
    if (!contents) {
      // no content yet, create text as default
      p44::createViewFromConfig(JsonObject::objFromText(DEFAULT_CONTENTS_CFG), contents, dispView);
      dispView->setScrolledView(contents);
    }
    if (contents) {
      // due to offset wraparound according to scrolled view's content size (~=text length)
      // current offset might be smaller than panel's offsetX right now. This must be
      // adjusted BEFORE content size changes
      double ox = dispView->getOffsetX();
      double cx = contents->getContentSize().x;
      while (cx>0 && ox<offsetX) ox += cx;
      dispView->setOffsetX(ox);
    }
    // now we can set new text (and content size)
    TextViewPtr message = boost::dynamic_pointer_cast<TextView>(contents->getView("TEXT"));
    if (message) message->setText(aText);
  }
}


ErrorPtr DispPanel::installScene(JsonObjectPtr aSceneConfig)
{
  ErrorPtr err;
  if (dispView) {
    if (contents) {
      // due to offset wraparound according to scrolled view's content size (~=text length)
      // current offset might be smaller than panel's offsetX right now. This must be
      // adjusted BEFORE content size changes
      double ox = dispView->getOffsetX();
      double cx = contents->getContentSize().x;
      while (cx>0 && ox<offsetX) ox += cx;
      dispView->setOffsetX(ox);
      contents.reset();
      dispView->setScrolledView(contents);
    }
    // get new contents view hierarchy
    err = p44::createViewFromConfig(aSceneConfig, contents, dispView);
    if (!Error::isOK(err))
      return err;
    dispView->setScrolledView(contents);
  }
  return err;
}


void DispPanel::resetScroll()
{
  if (dispView) {
    dispView->setOffsetX(0);
    dispView->setOffsetY(0);
    if (contents) {
      PixelRect f = contents->getFrame();
      f.x = 0;
      f.y = 0;
      contents->setFrame(f);
    }
  }
}


ErrorPtr DispPanel::reconfigure(const string aViewLabel, JsonObjectPtr aConfig)
{
  ErrorPtr err;
  if (dispView) {
    P44ViewPtr view = dispView->getView(aViewLabel);
    if (view) {
      view->configureView(aConfig);
    }
  }
  return err;
}


// MARK: ===== DispMatrix


DispMatrix::DispMatrix(const string aChainName1, const string aChainName2, const string aChainName3) :
  inherited("text"),
  usedPanels(0)
{
  // save chain names
  chainNames[0] = aChainName1;
  chainNames[1] = aChainName2;
  chainNames[2] = aChainName3;
  // check for commandline-triggered standalone operation
  string cfg;
  if (CmdLineApp::sharedCmdLineApp()->getStringOption("dispmatrix", cfg)) {
    int numCols = LED_MODULE_COLS;
    int numRows = LED_MODULE_ROWS;
    sscanf(cfg.c_str(), "%d,%d", &numCols, &numRows);
    // instantiate a single panel
    panels[usedPanels] = DispPanelPtr(new DispPanel(chainNames[usedPanels], 0, numRows, numCols, LED_MODULE_BORDER_LEFT, LED_MODULE_BORDER_RIGHT, P44View::right));
    usedPanels++;
    initOperation();
    // have standard message scrolling
    panels[0]->setText("Hello World +++ ");
    panels[0]->dispView->startScroll(0.25, 0, 20*MilliSecond, true);
  }
}


void DispMatrix::reset()
{
  stepTicket.cancel();
  for (int i=0; i<usedPanels; ++i) {
    panels[i].reset();
  }
  usedPanels = 0;
  inherited::reset();
}


DispMatrix::~DispMatrix()
{
  reset();
}


// MARK: ==== dispmatrix API


ErrorPtr DispMatrix::initialize(JsonObjectPtr aInitData)
{
  LOG(LOG_NOTICE, "initializing dispmatrix");
  reset();
  if (!aInitData->isType(json_type_array)) {
    return FeatureApiError::err("init data must be array of panel specs");
  }
  for (int i = 0; i<aInitData->arrayLength(); ++i) {
    JsonObjectPtr panelCfg = aInitData->arrayGet(i);
    int rows = LED_MODULE_ROWS;
    int visiblecols = LED_MODULE_COLS;
    int borderLeft = LED_MODULE_BORDER_LEFT;
    int borderRight = LED_MODULE_BORDER_RIGHT;
    int orientation = P44View::right;
    int offsetX = 0;
    // configure
    JsonObjectPtr o;
    // - usually
    if (panelCfg->get("cols", o, true)) {
      visiblecols = o->int32Value();
    }
    if (panelCfg->get("orientation", o, true)) {
      orientation = o->int32Value();
    }
    if (panelCfg->get("offset", o, true)) {
      offsetX = o->int32Value();
    }
    // - special cases
    if (panelCfg->get("rows", o, true)) {
      rows = o->int32Value();
    }
    if (panelCfg->get("borderleft", o, true)) {
      borderLeft = o->int32Value();
    }
    if (panelCfg->get("borderright", o, true)) {
      borderRight = o->int32Value();
    }
    // now create panel
    if (usedPanels>=numChains) {
      return FeatureApiError::err("cannot create more than %d display panels", numChains);
    }
    int cols = visiblecols+borderLeft+borderRight;
    panels[usedPanels] = DispPanelPtr(new DispPanel(chainNames[usedPanels], offsetX, rows, cols, borderLeft, borderRight, orientation));
    usedPanels++;
  }
  initOperation();
  return Error::ok();
}



#define MIN_SCROLL_STEP_INTERVAL (20*MilliSecond)

#define FOR_EACH_PANEL(m) for(int i=0; i<usedPanels; ++i) { panels[i]->m; }
#define FOR_EACH_PANEL_WITH_ERR(m,e) for(int i=0; i<usedPanels; ++i) { e = panels[i]->m; if (!Error::isOK(e)) break; }


ErrorPtr DispMatrix::processRequest(ApiRequestPtr aRequest)
{
  ErrorPtr err;
  JsonObjectPtr data = aRequest->getRequest();
  JsonObjectPtr o = data->get("cmd");
  if (o) {
    // decode commands
    string cmd = o->stringValue();
    if (cmd=="stopscroll") {
      FOR_EACH_PANEL(dispView->stopScroll());
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
        interval = o->doubleValue()*MilliSecond;
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
          st = o->int64Value()*MilliSecond;
        }
        start = MainLoop::unixTimeToMainLoopTime(st);
      }
      if (interval<MIN_SCROLL_STEP_INTERVAL) interval = MIN_SCROLL_STEP_INTERVAL;
      FOR_EACH_PANEL(dispView->startScroll(stepx, stepy, interval, roundoffsets, steps, start));
      return Error::ok();
    }
    else if (cmd=="scrollstatus") {
      bool last = true;
      bool purge = false;
      if (data->get("last", o)) last = o->boolValue();
      if (data->get("purge", o)) purge = o->boolValue();
      JsonObjectPtr answer = JsonObject::newObj();
      answer->add("remainingtime", JsonObject::newDouble(getRemainingScrollTime(last, purge)/MilliSecond));
      // - return
      aRequest->sendResponse(answer, ErrorPtr());
      return ErrorPtr();
    }
    else if (cmd=="fade") {
      int to = 255;
      MLMicroSeconds t = 300*MilliSecond;
      if (data->get("to", o, true)) {
        to = o->doubleValue()*255;
      }
      if (data->get("t", o, true)) {
        t = o->doubleValue()*MilliSecond;
      }
      FOR_EACH_PANEL(dispView->fadeTo(to, t));
      return Error::ok();
    }
    else if (cmd=="reconfigure") {
      if (data->get("view", o)) {
        string viewLabel = o->stringValue();
        JsonObjectPtr viewConfig = data->get("config");
        if (viewConfig) {
          FOR_EACH_PANEL_WITH_ERR(reconfigure(viewLabel, viewConfig), err);
          return err ? err : Error::ok();
        }
      }
      return TextError::err("missing 'view' and/or 'config'");
    }
    return inherited::processRequest(aRequest);
  }
  else {
    // decode properties
    if (data->get("text", o, true)) {
      string msg = o->stringValue();
      FOR_EACH_PANEL(setText(msg));
    }
    if (data->get("scene", o, true)) {
      if (o->isType(json_type_string)) {
        // scene file path, load it
        string scenePath = o->stringValue();
        o = JsonObject::objFromFile(Application::sharedApplication()->resourcePath(scenePath).c_str(), &err);
        if (!Error::isOK(err)) return err;
      }
      FOR_EACH_PANEL_WITH_ERR(installScene(o), err);
    }
    if (data->get("color", o, true)) {
      PixelColor p = webColorToPixel(o->stringValue());
      FOR_EACH_PANEL(setTextColor(p));
    }
    if (data->get("backgroundcolor", o, true)) {
      PixelColor p = webColorToPixel(o->stringValue());
      FOR_EACH_PANEL(setBackgroundColor(p));
    }
    if (data->get("spacing", o, true)) {
      int spacing = o->int32Value();
      FOR_EACH_PANEL(setTextSpacing(spacing));
    }
    if (data->get("offsetx", o, true)) {
      double offs = o->doubleValue();
      FOR_EACH_PANEL(setOffsetX(offs));
    }
    if (data->get("offsety", o, true)) {
      double offs = o->doubleValue();
      FOR_EACH_PANEL(dispView->setOffsetY(offs));
    }
    return err ? err : Error::ok();
  }
}


ViewScrollerPtr DispMatrix::firstPanelScroller()
{
  if (usedPanels>0) {
    return panels[0]->dispView;
  }
  return ViewScrollerPtr();
}


JsonObjectPtr DispMatrix::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    if (usedPanels>0) {
      DispPanelPtr p = panels[0];
      P44ViewPtr contents = p->contents;
      if (contents) {
        answer->add("backgroundcolor", JsonObject::newString(pixelToWebColor(contents->getBackgroundColor())));
        answer->add("color", JsonObject::newString(pixelToWebColor(contents->getForegroundColor())));
        answer->add("alpha", JsonObject::newInt32(contents->getAlpha()));
        TextViewPtr message = boost::dynamic_pointer_cast<TextView>(contents);
        if (message) {
          answer->add("text", JsonObject::newString(message->getText()));
          answer->add("spacing", JsonObject::newInt32(message->getTextSpacing()));
        }
      }
      if (p->dispView) {
        answer->add("brightness", JsonObject::newDouble((double)p->dispView->getAlpha()/255));
        answer->add("scrolloffsetx", JsonObject::newDouble(p->dispView->getOffsetX()));
        answer->add("scrolloffsety", JsonObject::newDouble(p->dispView->getOffsetY()));
        answer->add("scrollstepx", JsonObject::newDouble(p->dispView->getStepX()));
        answer->add("scrollstepy", JsonObject::newDouble(p->dispView->getStepY()));
        answer->add("scrollsteptime", JsonObject::newDouble(p->dispView->getScrollStepInterval()/MilliSecond));
        answer->add("unixtime", JsonObject::newInt64(MainLoop::unixtime()/MilliSecond));
      }
    }
  }
  return answer;
}


// MARK: ==== dispmatrix operation

void DispMatrix::initOperation()
{
  stepTicket.executeOnce(boost::bind(&DispMatrix::step, this, _1));
  setInitialized();
}


void DispMatrix::step(MLTimer &aTimer)
{
  MLMicroSeconds nextCall = Infinite;
  for (int i=0; i<usedPanels; ++i) {
    MLMicroSeconds n = panels[i]->step();
    if (nextCall<0 || (n>0 && n<nextCall)) {
      nextCall = n;
    }
  }
  MLMicroSeconds now = MainLoop::now();
  if (nextCall<0 || nextCall-now>MAX_STEP_INTERVAL) {
    nextCall = now+MAX_STEP_INTERVAL;
  }
  MainLoop::currentMainLoop().retriggerTimer(aTimer, nextCall, 0, MainLoop::absolute);
}


void DispMatrix::setNeedContentHandler(NeedContentCB aNeedContentCB)
{
  FOR_EACH_PANEL(dispView->setNeedContentHandler(aNeedContentCB));
}


MLMicroSeconds DispMatrix::getRemainingScrollTime(bool aLast, bool aPurge)
{
  MLMicroSeconds rem = Infinite;
  for (int i=0; i<usedPanels; ++i) {
    MLMicroSeconds n = panels[i]->dispView->remainingScrollTime();
    if (n!=Infinite) {
      if (rem==Infinite || (aLast && n>rem) || (!aLast && n<rem)) rem = n;
    }
    if (aPurge) panels[i]->dispView->purgeScrolledOut();
  }
  return rem;
}


void DispMatrix::resetScroll()
{
  FOR_EACH_PANEL(resetScroll());
}

#endif // ENABLE_FEATURE_DISPMATRIX
