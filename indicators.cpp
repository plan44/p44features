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

#include "indicators.hpp"

#if ENABLE_FEATURE_INDICATORS

#include "application.hpp"

using namespace p44;

#define FEATURE_NAME "indicators"

#include "viewfactory.hpp"
#include "lightspotview.hpp"

Indicators::Indicators(LEDChainArrangementPtr aLedChainArrangement) :
  inherited(FEATURE_NAME),
  mLedChainArrangement(aLedChainArrangement)
{
}


void Indicators::reset()
{
  stop();
  if (mLedChainArrangement) mLedChainArrangement->end();
  if (mIndicatorsView) mIndicatorsView.reset();
  inherited::reset();
}


Indicators::~Indicators()
{
  reset();
}


// MARK: ==== indicators API

#define INDICATORS_VIEW_LABEL "INDICATORS"

ErrorPtr Indicators::initialize(JsonObjectPtr aInitData)
{
  reset();
  // { "cmd":"init", "indicators": {} }
  // { "cmd":"init", "indicators": { "rootview": <p44lrgraphics-view-config> } }
  // { "cmd":"init", "indicators": { "ledchains": [ "ledchainspec1", "ledchainspec2", ... ], "rootview": <p44lrgraphics-view-config> } }
  ErrorPtr err;
  JsonObjectPtr o;
  if (mLedChainArrangement) {
    if (aInitData->get("ledchains", o)) {
      // ledchain re-arrangement from config
      // - forget default arrangement
      mLedChainArrangement->removeAllChains();
      // - add chains from array of strings
      for (int i=0; i<o->arrayLength(); ++i) {
        JsonObjectPtr ao = o->arrayGet(i);
        mLedChainArrangement->addLEDChain(ao->stringValue());
      }
      mLedChainArrangement->startChains(); // start chains that are not yet operating
    }
    // get the ledChainArrangement's current rootview (possibly shared with other feature)
    P44ViewPtr rootView = mLedChainArrangement->getRootView();
    if (aInitData->get("rootview", o)) {
      // create or re-configure the root view (such as adding a dedicated indicators view)
      err = createViewFromResourceOrObj(o, FEATURE_NAME "/", rootView, nullptr);
    }
    if (Error::isOK(err)) {
      string indicatorsLabel = INDICATORS_VIEW_LABEL;
      if (aInitData->get("indicatorslabel", o)) {
        indicatorsLabel = o->stringValue();
      }
      if (rootView) {
        // indicators view might already exist in current root view hiearchy
        mIndicatorsView = dynamic_pointer_cast<ViewStack>(rootView->getView(indicatorsLabel));
      }
      ViewStackPtr rootStack = dynamic_pointer_cast<ViewStack>(rootView);
      if (!mIndicatorsView) {
        if (rootStack) {
          // just use root view stack
          mIndicatorsView = rootStack;
        }
        else {
          // no suitable root view at all, create one
          PixelRect r = mLedChainArrangement->totalCover();
          rootStack = ViewStackPtr(new ViewStack);
          rootStack->setFrame(r);
          rootStack->setFullFrameContent();
          rootStack->setBackgroundColor(black);
          rootStack->setPositioningMode(P44View::noAdjust);
          rootStack->setDefaultLabel(indicatorsLabel); // set label
          // inidcator view stack is the itself the root view
          mIndicatorsView = rootStack;
          // activate as root view
          rootView = rootStack;
          mLedChainArrangement->setRootView(rootView);
        }
      }
      // now start
      initOperation();
    }
    else {
      err = TextError::err("error creating or configuring root view");
    }
  }
  else {
    err = TextError::err("no led chains configured");
  }
  return err;
}


ErrorPtr Indicators::processRequest(ApiRequestPtr aRequest)
{
  ErrorPtr err;
  JsonObjectPtr data = aRequest->getRequest();
  JsonObjectPtr o = data->get("cmd");
  if (o) {
    string cmd = o->stringValue();
    // decode commands
    //  minimally: { cmd: "indicate" } /* full area */
    //  normally: { cmd: "indicate", x:0, dx:20, effect="swipe" }
    //  full: { cmd: "indicate", x:0, dx:20, y:0, dy:1, effect:"pulse", t:1 }
    if (cmd=="indicate" && mIndicatorsView) {
      PixelRect f = mIndicatorsView->getContent(); // default to full view
      // common parameters
      if (data->get("x", o)) f.x = o->int32Value();
      if (data->get("y", o)) f.y = o->int32Value();
      if (data->get("dx", o)) f.dx = o->int32Value();
      if (data->get("dy", o)) f.dy = o->int32Value();
      MLMicroSeconds t = 0.5*Second;
      if (data->get("t", o)) t = o->doubleValue()*Second;
      PixelColor col = { 255, 0, 0, 255 }; // default to red
      if (data->get("color", o)) col = webColorToPixel(o->stringValue());
      // effect
      JsonObjectPtr viewCfg;
      P44ViewPtr effectView;
      if (!data->get("effect", o)) {
        o = JsonObject::newString("plain"); // default effect
      }
      if (o->isType(json_type_string)) {
        string effectName = o->stringValue();
        // check predefined effects
        if (effectName=="plain") {
          effectView = P44ViewPtr(new P44View);
          effectView->setBackgroundColor(col);
          effectView->setFrame(f);
          //effectView->setFullFrameContent();
        }
        else if (effectName=="swipe") {
          effectView = P44ViewPtr(new P44View);
          effectView->setForegroundColor(col);
          effectView->setFrame(f);
          effectView->setFullFrameContent();
          effectView->animatorFor("content_x")->from(-f.dx)->animate(f.dx, t);
        }
        else if (effectName=="pulse") {
          effectView = P44ViewPtr(new P44View);
          effectView->setBackgroundColor(col);
          effectView->setFrame(f);
          //effectView->setFullFrameContent();
          effectView->animatorFor("alpha")->from(0)->repeat(true, 2)->animate(255, t/2);
        }
        else if (effectName=="spot") {
          bool radial = false;
          if (data->get("radial", o)) radial = o->boolValue();
          LightSpotViewPtr lsp = LightSpotViewPtr(new LightSpotView);
          effectView = lsp;
          effectView->setFrame(f);
          effectView->setFullFrameContent();
          lsp->setRelativeContentOrigin(0, 0);
          lsp->setRelativeExtent(1); // fill the area
          lsp->setColoringParameters(col, -1, gradient_curve_cos, 0, gradient_none, 0, gradient_none, radial);
          effectView->animatorFor("alpha")->from(0)->repeat(true, 2)->animate(255, t/2);
        }
      }
      // not a predefined effect: could be JSON literal config or filename
      if (!viewCfg && !effectView) {
        viewCfg = Application::jsonObjOrResource(o, &err, FEATURE_NAME "/");
        if (Error::notOK(err)) return err;
      }
      if (viewCfg) {
        // add-in frame
        viewCfg->add("x", JsonObject::newInt32(f.x));
        viewCfg->add("y", JsonObject::newInt32(f.y));
        viewCfg->add("dx", JsonObject::newInt32(f.dx));
        viewCfg->add("dy", JsonObject::newInt32(f.dy));
        err = p44::createViewFromConfig(viewCfg, effectView, mIndicatorsView);
        if (Error::notOK(err)) return err;
      }
      if (!effectView) return TextError::err("No valid indicator effect");
      // now run
      runEffect(effectView, t, data);
      return Error::ok();
    }
    else if (cmd=="stop") {
      stop();
      return Error::ok();
    }
    return inherited::processRequest(aRequest);
  }
  else {
    // decode properties
    //%%% none yet
    return err ? err : Error::ok();
  }
}


JsonObjectPtr Indicators::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    answer->add("activeIndicators", JsonObject::newInt64(mActiveIndicators.size()));
  }
  return answer;
}


// MARK: ==== indicators operation

void Indicators::stop()
{
  for (EffectsList::iterator pos = mActiveIndicators.begin(); pos!=mActiveIndicators.end(); ++pos) {
    IndicatorEffectPtr eff = *pos;
    eff->mTicket.cancel();
    mIndicatorsView->removeView(eff->mView);
  }
  mActiveIndicators.clear();
}


void Indicators::runEffect(P44ViewPtr aView, MLMicroSeconds aDuration, JsonObjectPtr aConfig)
{
  OLOG(LOG_INFO, "Starting effect");
  mIndicatorsView->pushView(aView);
  IndicatorEffectPtr effect = IndicatorEffectPtr(new IndicatorEffect);
  effect->mView = aView;
  // and make sure it gets cleaned up after given time
  effect->mTicket.executeOnce(boost::bind(&Indicators::effectDone, this, effect), aDuration);
  mActiveIndicators.push_back(effect);
  mIndicatorsView->requestUpdate();
}


void Indicators::effectDone(IndicatorEffectPtr aEffect)
{
  aEffect->mView->stopAnimations();
  mIndicatorsView->removeView(aEffect->mView);
  mActiveIndicators.remove(aEffect);
  mIndicatorsView->requestUpdate();
  OLOG(LOG_INFO, "Effect Done");
}


void Indicators::initOperation()
{
  if (mLedChainArrangement && mIndicatorsView) {
    mLedChainArrangement->begin(true);
  }
  else {
    OLOG(LOG_WARNING, "NOP: no ledchain connected");
  }
  setInitialized();
}



#endif // ENABLE_FEATURE_INDICATORS
