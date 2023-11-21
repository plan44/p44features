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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "wifitrack.hpp"

#if ENABLE_FEATURE_WIFITRACK

#include "application.hpp"
#include "viewstack.hpp"

#define WIFITRACK_STATE_FILE_NAME "wifitrack_state.json"

using namespace p44;



// MARK: ===== WTMac

WTMac::WTMac() :
  seenLast(Never),
  seenFirst(Never),
  seenCount(0),
  ouiName(NULL),
  lastRssi(-9999),
  bestRssi(-9999),
  worstRssi(9999),
  hidden(false)
{
}


// MARK: ===== WTSSid

WTSSid::WTSSid() :
  seenLast(Never),
  seenCount(0),
  hidden(false),
  beaconSeenLast(Never),
  beaconRssi(-9999)
{
}


// MARK: ===== WTPerson

WTPerson::WTPerson() :
  seenLast(Never),
  seenFirst(Never),
  seenCount(0),
  lastRssi(-9999),
  bestRssi(-9999),
  worstRssi(9999),
  shownLast(Never),
  color(white),
  imageIndex(0),
  hidden(false)
{
}



// MARK: ===== WifiTrack

WifiTrack::WifiTrack(const string aMonitorIf, int aRadiotapDBOffset, bool doStart) :
  inherited("wifitrack"),
  directDisplay(true),
  apiNotify(false),
  monitorIf(aMonitorIf),
  dumpPid(-1),
  rememberWithoutSsid(false),
  ouiNames(true),
  minShowInterval(3*Minute),
  minRssi(-80),
  mRadiotapDBOffset(0x16), // correct value for mt76 on Openwrt 19.07, must be 0x1E on Openwrt 22.03
  reportSightings(false),
  aggregatePersons(true),
  scanBeacons(true),
  minProcessRssi(-99),
  minShowRssi(-65),
  tooCommonMacCount(20),
  minCommonSsidCount(3),
  numPersonImages(24),
  maxDisplayDelay(21*Second),
  saveTempInterval(10*Minute),
  saveDataInterval(7*Day),
  lastTempAutoSave(Never),
  lastDataAutoSave(Never),
  loadingContent(false)
{
  if (aRadiotapDBOffset>0) {
    mRadiotapDBOffset = aRadiotapDBOffset;
  }
  // check for commandline-triggered standalone operation
  if (doStart) {
    initOperation();
  }
}


WifiTrack::~WifiTrack()
{
}

// MARK: ==== API

ErrorPtr WifiTrack::initialize(JsonObjectPtr aInitData)
{
  reset();
  JsonObjectPtr o;
  if (aInitData->get("directDisplay", o)) {
    directDisplay = o->boolValue();
  }
  if (aInitData->get("apiNotify", o)) {
    apiNotify = o->boolValue();
  }
  if (aInitData->get("radiotapDBoffs", o)) {
    mRadiotapDBOffset = o->int32Value();
  }
  initOperation();
  return Error::ok();
}


ErrorPtr WifiTrack::processRequest(ApiRequestPtr aRequest)
{
  ErrorPtr err;
  JsonObjectPtr data = aRequest->getRequest();
  JsonObjectPtr o = data->get("cmd");
  if (o) {
    string cmd = o->stringValue();
    if (cmd=="dump") {
      bool ssids = true;
      bool macs = true;
      bool persons = true;
      bool personssids = false;
      bool ouinames = true;
      if (data->get("ssids", o)) ssids = o->boolValue();
      if (data->get("macs", o)) macs = o->boolValue();
      if (data->get("persons", o)) persons = o->boolValue();
      if (data->get("personssids", o)) personssids = o->boolValue();
      if (data->get("ouinames", o)) ouinames = o->boolValue();
      JsonObjectPtr ans = dataDump(ssids, macs, persons, ouinames, personssids);
      aRequest->sendResponse(ans, ErrorPtr());
      return ErrorPtr();
    }
    else if (cmd=="save") {
      string path = Application::sharedApplication()->dataPath(WIFITRACK_STATE_FILE_NAME);
      if (data->get("path", o)) path = o->stringValue();
      err = save(path);
      return err ? err : Error::ok();
    }
    else if (cmd=="load") {
      string path = Application::sharedApplication()->dataPath(WIFITRACK_STATE_FILE_NAME);
      if (data->get("path", o)) path = o->stringValue();
      err = load(path);
      return err ? err : Error::ok();
    }
    else if (cmd=="test") {
      string intro = "hi";
      string name = "anonymus";
      string brand = "any";
      string target = "wifi";
      if (data->get("intro", o)) intro = o->stringValue();
      if (data->get("name", o)) name = o->stringValue();
      if (data->get("brand", o)) brand = o->stringValue();
      if (data->get("target", o)) target = o->stringValue();
      int imgIdx = 0;
      if (data->get("imgidx", o)) imgIdx = o->int32Value() % numPersonImages;
      PixelColor col = white;
      if (data->get("color", o)) col = webColorToPixel(o->stringValue());
      displayEncounter(intro, imgIdx, col, name, brand, target);
      return Error::ok();
    }
    else if (cmd=="hide") {
      bool hide = true;
      if (data->get("hide", o)) hide = o->boolValue();
      if (data->get("ssid", o)) {
        string s = o->stringValue();
        WTSSidMap::iterator pos = ssids.find(s);
        if (pos!=ssids.end()) {
          pos->second->hidden = hide;
        }
      }
      else if (data->get("mac", o)) {
        uint64_t mac = stringToMacAddress(o->stringValue().c_str());
        WTMacMap::iterator pos = macs.find(mac);
        if (pos!=macs.end()) {
          if (data->get("withperson", o)) {
            if (pos->second->person && o->boolValue()) pos->second->person->hidden = hide; // hide associated person
          }
          pos->second->hidden = hide;
        }
      }
      return Error::ok();
    }
    else if (cmd=="rename") {
      if (data->get("mac", o)) {
        uint64_t mac = stringToMacAddress(o->stringValue().c_str());
        WTMacMap::iterator pos = macs.find(mac);
        if (pos!=macs.end() && pos->second->person) {
          if (data->get("name", o)) {
            pos->second->person->name = o->stringValue();
          }
          if (data->get("color", o)) {
            pos->second->person->color = webColorToPixel(o->stringValue());
          }
          if (data->get("imgidx", o)) {
            pos->second->person->imageIndex = o->int32Value() % numPersonImages;
          }
        }
      }
      return Error::ok();
    }
    else if (cmd=="restart") {
      restartScanner();
      return Error::ok();
    }
    else {
      return inherited::processRequest(aRequest);
    }
  }
  else {
    // decode properties
    if (data->get("minShowInterval", o, true)) {
      minShowInterval = o->doubleValue()*Second;
    }
    if (data->get("rememberWithoutSsid", o, true)) {
      rememberWithoutSsid = o->boolValue();
    }
    if (data->get("ouiNames", o, true)) {
      ouiNames = o->boolValue();
    }
    if (data->get("reportSightings", o)) {
      reportSightings = o->boolValue();
    }
    if (data->get("aggregatePersons", o)) {
      aggregatePersons = o->boolValue();
    }
    if (data->get("minProcessRssi", o, true)) {
      minProcessRssi = o->int32Value();
    }
    if (data->get("minRssi", o, true)) {
      int i = o->int32Value();
      if (i!=minRssi) {
        minRssi = i;
        restartScanner();
      }
    }
    if (data->get("scanBeacons", o, true)) {
      bool b = o->boolValue();
      if (b!=scanBeacons) {
        scanBeacons = b;
        restartScanner();
      }
    }
    if (data->get("minShowRssi", o, true)) {
      minShowRssi = o->int32Value();
    }
    if (data->get("tooCommonMacCount", o, true)) {
      tooCommonMacCount = o->int32Value();
    }
    if (data->get("minCommonSsidCount", o, true)) {
      minCommonSsidCount = o->int32Value();
    }
    if (data->get("numPersonImages", o, true)) {
      numPersonImages = o->int32Value();
    }
    if (data->get("maxDisplayDelay", o, true)) {
      maxDisplayDelay = o->doubleValue()*Second;
    }
    if (data->get("saveTempInterval", o, true)) {
      saveTempInterval = o->doubleValue()*Second;
    }
    if (data->get("saveDataInterval", o, true)) {
      saveDataInterval = o->doubleValue()*Second;
    }
    return err ? err : Error::ok();
  }
}


JsonObjectPtr WifiTrack::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    answer->add("minShowInterval", JsonObject::newDouble((double)minShowInterval/Second));
    answer->add("rememberWithoutSsid", JsonObject::newBool(rememberWithoutSsid));
    answer->add("ouiNames", JsonObject::newBool(ouiNames));
    answer->add("reportSightings", JsonObject::newBool(reportSightings));
    answer->add("aggregatePersons", JsonObject::newBool(aggregatePersons));
    answer->add("minRssi", JsonObject::newInt32(minRssi));
    answer->add("scanBeacons", JsonObject::newBool(scanBeacons));
    answer->add("minProcessRssi", JsonObject::newInt32(minProcessRssi));
    answer->add("minShowRssi", JsonObject::newInt32(minShowRssi));
    answer->add("tooCommonMacCount", JsonObject::newInt32(tooCommonMacCount));
    answer->add("minCommonSsidCount", JsonObject::newInt32(minCommonSsidCount));
    answer->add("numPersonImages", JsonObject::newInt32(numPersonImages));
    answer->add("maxDisplayDelay", JsonObject::newDouble((double)maxDisplayDelay/Second));
    answer->add("saveTempInterval", JsonObject::newDouble((double)saveTempInterval/Second));
    answer->add("saveDataInterval", JsonObject::newDouble((double)saveDataInterval/Second));
    // also add some statistics
    answer->add("numpersons", JsonObject::newInt64(persons.size()));
    answer->add("nummacs", JsonObject::newInt64(macs.size()));
    answer->add("numssids", JsonObject::newInt64(ssids.size()));
  }
  return answer;
}



ErrorPtr WifiTrack::load(const string aPath)
{
  ErrorPtr err;
  JsonObjectPtr data = JsonObject::objFromFile(Application::sharedApplication()->tempPath(aPath).c_str(), &err);
  if (err) return err; // no data to import
  return dataImport(data);
}


ErrorPtr WifiTrack::save(const string aPath)
{
  JsonObjectPtr data = dataDump();
  return data->saveToFile(Application::sharedApplication()->tempPath(aPath).c_str());
}


JsonObjectPtr WifiTrack::dataDump(bool aSsids, bool aMacs, bool aPersons, bool aOUINames, bool aPersonSsids)
{
  MLMicroSeconds unixTimeOffset = -MainLoop::now()+MainLoop::unixtime();
  JsonObjectPtr ans = JsonObject::newObj();
  // summary info
  ans->add("numpersons", JsonObject::newInt64(persons.size()));
  ans->add("nummacs", JsonObject::newInt64(macs.size()));
  ans->add("numssids", JsonObject::newInt64(ssids.size()));
  // persons
  if (aPersons) {
    JsonObjectPtr pans = JsonObject::newArray();
    for (WTPersonSet::iterator ppos = persons.begin(); ppos!=persons.end(); ++ppos) {
      JsonObjectPtr p = JsonObject::newObj();
      p->add("lastrssi", JsonObject::newInt32((*ppos)->lastRssi));
      p->add("bestrssi", JsonObject::newInt32((*ppos)->bestRssi));
      p->add("worstrssi", JsonObject::newInt32((*ppos)->worstRssi));
      if ((*ppos)->hidden) p->add("hidden", JsonObject::newBool(true));
      p->add("count", JsonObject::newInt64((*ppos)->seenCount));
      p->add("last", JsonObject::newInt64((*ppos)->seenLast+unixTimeOffset));
      p->add("first", JsonObject::newInt64((*ppos)->seenFirst+unixTimeOffset));
      p->add("color", JsonObject::newString(pixelToWebColor((*ppos)->color, true)));
      p->add("imgidx", JsonObject::newInt64((*ppos)->imageIndex));
      p->add("name", JsonObject::newString((*ppos)->name));
      JsonObjectPtr marr = JsonObject::newArray();
      WTSSidSet pssids;
      pssids.clear();
      for (WTMacSet::iterator mpos = (*ppos)->macs.begin(); mpos!=(*ppos)->macs.end(); ++mpos) {
        marr->arrayAppend(JsonObject::newString(macAddressToString((*mpos)->mac, ':').c_str()));
        if (aPersonSsids) {
          for (WTSSidSet::iterator spos = (*mpos)->ssids.begin(); spos!=(*mpos)->ssids.end(); ++spos) {
            pssids.insert(*spos);
          }
        }
      }
      p->add("macs", marr);
      if (aPersonSsids) {
        JsonObjectPtr sarr = JsonObject::newArray();
        for (WTSSidSet::iterator spos = pssids.begin(); spos!=pssids.end(); ++spos) {
          sarr->arrayAppend(JsonObject::newString((*spos)->ssid));
        }
        p->add("ssids", sarr);
      }
      pans->arrayAppend(p);
    }
    ans->add("persons", pans);
  }
  if (aMacs) {
    // macs
    JsonObjectPtr mans = JsonObject::newObj();
    for (WTMacMap::iterator mpos = macs.begin(); mpos!=macs.end(); ++mpos) {
      JsonObjectPtr m = JsonObject::newObj();
      if (aOUINames && mpos->second->ouiName) m->add("ouiname", JsonObject::newString(mpos->second->ouiName));
      m->add("lastrssi", JsonObject::newInt32(mpos->second->lastRssi));
      m->add("bestrssi", JsonObject::newInt32(mpos->second->bestRssi));
      m->add("worstrssi", JsonObject::newInt32(mpos->second->worstRssi));
      if (mpos->second->hidden) m->add("hidden", JsonObject::newBool(true));
      m->add("count", JsonObject::newInt64(mpos->second->seenCount));
      m->add("last", JsonObject::newInt64(mpos->second->seenLast+unixTimeOffset));
      m->add("first", JsonObject::newInt64(mpos->second->seenFirst+unixTimeOffset));
      JsonObjectPtr sarr = JsonObject::newArray();
      for (WTSSidSet::iterator spos = mpos->second->ssids.begin(); spos!=mpos->second->ssids.end(); ++spos) {
        sarr->arrayAppend(JsonObject::newString((*spos)->ssid));
      }
      m->add("ssids", sarr);
      mans->add(macAddressToString(mpos->first, ':').c_str(), m);
    }
    ans->add("macs", mans);
  }
  if (aSsids) {
    // ssid details
    JsonObjectPtr sans = JsonObject::newObj();
    for (WTSSidMap::iterator spos = ssids.begin(); spos!=ssids.end(); ++spos) {
      JsonObjectPtr s = JsonObject::newObj();
      s->add("count", JsonObject::newInt64(spos->second->seenCount));
      s->add("last", JsonObject::newInt64(spos->second->seenLast+unixTimeOffset));
      s->add("maccount", JsonObject::newInt64(spos->second->macs.size()));
      if (spos->second->hidden) s->add("hidden", JsonObject::newBool(true));
      if (spos->second->beaconSeenLast!=Never) {
        s->add("lastbeacon", JsonObject::newInt64(spos->second->beaconSeenLast+unixTimeOffset));
        s->add("beaconrssi", JsonObject::newInt32(spos->second->beaconRssi));
      }
      sans->add(spos->first.c_str(), s);
    }
    ans->add("ssids", sans);
  }
  return ans;
}


ErrorPtr WifiTrack::dataImport(JsonObjectPtr aData)
{
  MLMicroSeconds unixTimeOffset = -MainLoop::now()+MainLoop::unixtime();
  if (!aData || !aData->isType(json_type_object)) return TextError::err("invalid state data - must be JSON object");
  // insert ssids
  JsonObjectPtr sobjs = aData->get("ssids");
  if (!sobjs) return TextError::err("missing 'ssids'");
  sobjs->resetKeyIteration();
  JsonObjectPtr sobj;
  string ssidstr;
  while (sobjs->nextKeyValue(ssidstr, sobj)) {
    if (ssidstr.empty() && !rememberWithoutSsid) continue; // skip empty SSID
    WTSSidPtr s;
    WTSSidMap::iterator spos = ssids.find(ssidstr);
    if (spos!=ssids.end()) {
      s = spos->second;
    }
    else {
      s = WTSSidPtr(new WTSSid);
      s->ssid = ssidstr;
      ssids[ssidstr] = s;
    }
    JsonObjectPtr o;
    o = sobj->get("hidden");
    if (o) s->hidden = o->boolValue();
    o = sobj->get("count");
    if (o) s->seenCount += o->int64Value();
    o = sobj->get("last");
    MLMicroSeconds l = Never;
    if (o) l = o->int64Value()-unixTimeOffset;
    if (l>s->seenLast) s->seenLast = l;
  }
  // insert macs and links to ssids
  JsonObjectPtr mobjs = aData->get("macs");
  if (!mobjs) return TextError::err("missing 'macs'");
  mobjs->resetKeyIteration();
  JsonObjectPtr mobj;
  string macstr;
  while (mobjs->nextKeyValue(macstr, mobj)) {
    bool insertMac = false;
    uint64_t mac = stringToMacAddress(macstr.c_str());
    WTMacPtr m;
    WTMacMap::iterator mpos = macs.find(mac);
    if (mpos!=macs.end()) {
      m = mpos->second;
    }
    else {
      m = WTMacPtr(new WTMac);
      m->mac = mac;
      m->ouiName = ouiName(mac);
      insertMac = true;
    }
    // links
    JsonObjectPtr sarr = mobj->get("ssids");
    for (int i=0; i<sarr->arrayLength(); ++i) {
      string ssidstr = sarr->arrayGet(i)->stringValue();
      if (!rememberWithoutSsid && ssidstr.empty()) {
        // empty SSID and we don't want empty ones!
        if (sarr->arrayLength()==1) {
          // also prevent inserting mac if the empty SSID is the only one
          insertMac = false;
        }
        continue; // check next
      }
      WTSSidPtr s;
      WTSSidMap::iterator spos = ssids.find(ssidstr);
      if (spos!=ssids.end()) {
        s = spos->second;
      }
      else {
        s = WTSSidPtr(new WTSSid);
        s->ssid = ssidstr;
        ssids[ssidstr] = s;
      }
      m->ssids.insert(s);
      s->macs.insert(m);
    }
    if (insertMac) {
      macs[mac] = m;
    }
    // other props
    JsonObjectPtr o;
    o = mobj->get("hidden");
    if (o) m->hidden = o->boolValue();
    o = mobj->get("count");
    if (o) m->seenCount += o->int64Value();
    o = mobj->get("bestrssi");
    int r = -9999;
    if (o) r = o->int32Value();
    if (r>m->bestRssi) m->bestRssi = r;
    o = mobj->get("worstrssi");
    r = 9999;
    if (o) r = o->int32Value();
    if (r<m->worstRssi) m->worstRssi = r;
    o = mobj->get("last");
    MLMicroSeconds l = Never;
    if (o) l = o->int64Value()-unixTimeOffset;
    if (l>m->seenLast) {
      m->seenLast = l;
      o = mobj->get("lastrssi");
      if (o) m->lastRssi = o->int32Value();
    }
    o = mobj->get("first");
    l = Never;
    if (o) l = o->int64Value()-unixTimeOffset;
    if (l!=Never && m->seenFirst!=Never && l<m->seenFirst) m->seenFirst = l;
  }
  JsonObjectPtr pobjs = aData->get("persons");
  if (pobjs) {
    for (int pidx=0; pidx<pobjs->arrayLength(); pidx++) {
      JsonObjectPtr pobj = pobjs->arrayGet(pidx);
      WTPersonPtr p = WTPersonPtr(new WTPerson);
      // links to macs
      JsonObjectPtr marr = pobj->get("macs");
      for (int i=0; i<marr->arrayLength(); ++i) {
        string macstr = marr->arrayGet(i)->stringValue();
        uint64_t mac = stringToMacAddress(macstr.c_str());
        WTMacMap::iterator mpos = macs.find(mac);
        if (mpos!=macs.end()) {
          p->macs.insert(mpos->second);
          mpos->second->person = p;
        }
      }
      if (p->macs.size()==0) continue; // not linked to any mac -> invalid, skip
      persons.insert(p);
      // other props
      JsonObjectPtr o;
      o = pobj->get("name");
      if (o) p->name = o->stringValue();
      o = pobj->get("color");
      if (o) p->color = webColorToPixel(o->stringValue());
      o = pobj->get("imgidx");
      if (o) p->imageIndex = o->int32Value();
      o = pobj->get("hidden");
      if (o) p->hidden = o->boolValue();
      o = pobj->get("count");
      if (o) p->seenCount += o->int64Value();
      o = pobj->get("bestrssi");
      int r = -9999;
      if (o) r = o->int32Value();
      if (r>p->bestRssi) p->bestRssi = r;
      o = pobj->get("worstrssi");
      r = 9999;
      if (o) r = o->int32Value();
      if (r<p->worstRssi) p->worstRssi = r;
      o = pobj->get("last");
      MLMicroSeconds l = Never;
      if (o) l = o->int64Value()-unixTimeOffset;
      if (l>p->seenLast) {
        p->seenLast = l;
        o = pobj->get("lastrssi");
        if (o) p->lastRssi = o->int32Value();
      }
      o = pobj->get("first");
      l = Never;
      if (o) l = o->int64Value()-unixTimeOffset;
      if (l!=Never && p->seenFirst!=Never && l<p->seenFirst) p->seenFirst = l;
    }
  }
  return ErrorPtr();
}

// MARK: ==== OUI lookup


const char* WifiTrack::ouiName(uint64_t aMac)
{
  // default to a /24 search
  const char *n = NULL;
  OUIMap::iterator opos = ouis.find((uint32_t)(aMac>>24));
  if (opos!=ouis.end()) {
    n = opos->second;
    if ((intptr_t)n<256) {
      // not a pointer, but subtable identifier byte
      // Siiiiii, S: 0=/28, 1=/36, i=subtable identifier
      uint32_t msrch = (uint32_t)((intptr_t)n<<24);
      msrch |= (aMac>>(msrch&0x80000000 ? (48-36) : (48-28))) & 0xFFFFFF;
      n = NULL;
      OUIMap::iterator opos = ouis.find((uint32_t)(msrch));
      if (opos!=ouis.end()) {
        n = opos->second;
      }
    }
  }
  return n;
}


#ifdef __APPLE__

// create a space saving version of the wireshark OUI table, by grouping /24 and /36 into sublists,
// differentiating them by the first byte of the lookup key (0 for regular oui24)
static void createOUItable()
{
  typedef std::map<uint32_t, uint8_t> SubgroupMap;
  string line;
  FILE *f = fopen(Application::sharedApplication()->resourcePath("oui_source.txt").c_str(), "r");
  SubgroupMap subGroups;
  if (!f) return;
  while (string_fgetline(f, line)) {
    if (line.size()<1 || line[0]=='#') continue; // skip comments and empty lines
    string s;
    const char *cursor = line.c_str();
    if (!nextPart(cursor, s, '\t', true)) continue;
    int msz = 24;
    size_t n = s.find("/");
    if (n!=string::npos) {
      sscanf(s.c_str()+n+1, "%d", &msz);
      s.erase(n);
    }
    string mb = hexToBinaryString(s.c_str(), false, 6);
    if (mb.size()<3 || mb.size()>6) continue;
    uint64_t mac = 0;
    for (int i=0; i<mb.size(); i++) {
      mac |= (uint64_t)((uint8_t)mb[i])<<(8*(5-i));
    }
    uint32_t oui24 = (uint32_t)(mac>>24);
    uint32_t msrch;
    // encode /24 and /36
    if (msz!=24) {
      SubgroupMap::iterator gpos = subGroups.find(oui24);
      uint8_t gbyte;
      if (gpos!=subGroups.end()) {
        gbyte = gpos->second;
      }
      else {
        gbyte = msz==36 ? 0x80 : 0;
        gbyte |= (subGroups.size()+1); // new group id
        subGroups[oui24] = gbyte;
        printf("%X\t*%02u\n", oui24, gbyte); // extra oui24 group lookup line
      }
      msrch = (gbyte<<24) | ((mac>>(gbyte&0x80 ? (48-36) : (48-28))) & 0xFFFFFF);
    }
    else {
      msrch = oui24;
    }
    // name
    if (!nextPart(cursor, s, '\t', true)) continue;
    if (s!="IeeeRegi") {
      bool capallowed = true;
      for (int i=0; i<s.size(); i++) {
        char c = s[i];
        if (!capallowed && isupper(c)) {
          s.erase(i);
          break;
        }
        // more caps only after non-alphanum
        capallowed = !isalpha(c) || isupper(c);
      }
      printf("%X\t%s\n", msrch, s.c_str()); // extra oui24 group lookup line
    }
  }
}

#endif



void WifiTrack::loadOUIs()
{
  if (!ouiNames || !ouis.empty()) return; // prevent re-loading
  OLOG(LOG_NOTICE, "Loading OUIs");
  typedef std::map<string, const char*> NameMap;
  NameMap nameMap;
  string line;
  FILE *f = fopen(Application::sharedApplication()->resourcePath("oui.txt").c_str(), "r");
  while (string_fgetline(f, line)) {
    if (line.size()<1 || line[0]=='#') continue; // skip comments and empty lines
    // mmmmm[/nn]   name
    string s;
    const char *cursor = line.c_str();
    if (!nextPart(cursor, s, '\t', true)) continue;
    uint32_t msrch;
    if (sscanf(s.c_str(), "%x", &msrch)!=1) continue;
    // Name or oui24 group header
    if (!nextPart(cursor, s, '\t', true)) continue;
    if (s.size()<1) continue;
    // - check for OUI24 group header
    if (s[0]=='*') {
      uint32_t gbyte;
      if (sscanf(s.c_str()+1, "%u", &gbyte)!=1) continue;
      ouis[msrch] = (const char *)gbyte; // not a pointer, but group header byte
    }
    else {
      // always use same string for multiple occurrences
      NameMap::iterator npos = nameMap.find(s);
      const char *nameP = NULL;
      if (npos==nameMap.end()) {
        nameP = new char[s.size()+1];
        strcpy((char *)nameP, s.c_str());
        nameMap[s] = nameP;
      }
      else {
        nameP = npos->second;
      }
      ouis[msrch] = nameP;
    }
  }
  OLOG(LOG_NOTICE, "Loaded %lu OUIs with %lu distinct names", ouis.size(), nameMap.size());
}



// MARK: ==== wifitrack operation

void WifiTrack::initOperation()
{
  restartTicket.cancel(); // cancel pending restarts
  OLOG(LOG_NOTICE, "initializing wifitrack");
  // display
  if (directDisplay) {
    disp = boost::dynamic_pointer_cast<DispMatrix>(FeatureApi::sharedApi()->getFeature("dispmatrix"));
    if (disp) {
      disp->setNeedContentHandler(boost::bind(&WifiTrack::needContentHandler, this));
    }
  }
  // network scanning
  #ifdef __APPLE__
  //createOUItable();
  #endif
  ErrorPtr err;
  loadOUIs();
  #ifdef __APPLE__
  uint64_t testMac = 0x40A36BC12345ll;
  //printf("%llX = %s", testMac, ouiName(testMac));
  #endif
  err = load(Application::sharedApplication()->tempPath(WIFITRACK_STATE_FILE_NAME));
  if (Error::isOK(err)) {
    OLOG(LOG_NOTICE, ">>> loaded data from temp file");
  }
  else {
    // try persistent path
    err = load(Application::sharedApplication()->dataPath(WIFITRACK_STATE_FILE_NAME));
    if (Error::isOK(err)) {
      OLOG(LOG_NOTICE, ">>> loaded data from persistent data file");
    }
  }
  if (Error::isOK(err)) {
    // assume data secured
    lastTempAutoSave = MainLoop::now();
    lastDataAutoSave = lastTempAutoSave;
  }
  else {
    OLOG(LOG_ERR, "could not load state: %s", Error::text(err));
  }
  startScanner();
}



void WifiTrack::startScanner()
{
  if (!monitorIf.empty()) {
    string cmd = string_format("tcpdump -e -i %s -s 256", monitorIf.c_str());
    if (scanBeacons) {
      string_format_append(cmd, " \\( type mgt subtype probe-req or subtype beacon \\)");
    }
    else {
      string_format_append(cmd, " \\( type mgt subtype probe-req \\)");
    }
    if (minRssi!=0 && mRadiotapDBOffset!=0) {
      uint16_t m = minRssi & 0xFF;
      // Note: offset into radiotap to get rssi is specific to set of radio tap fields supported by the wifi driver
      string_format_append(cmd, " and \\( radio[0x%x] \\> 0x%02X \\)", mRadiotapDBOffset , m);
    }
    #ifdef __APPLE__
    #warning "hardcoded access to mixloop/hermel/35c3 chatty wifi device"
    //cmd = "ssh -p 22 root@hermel-40a36bc18907.local. \"tcpdump -e -i moni0 -s 2000 type mgt subtype probe-req\"";
    cmd = "ssh -p 22 root@1a8479bcaf76.cust.devices.plan44.ch \"" + cmd + "\"";
    #endif
    int resultFd = -1;
    OLOG(LOG_NOTICE, "Starting tcpdump: %s", cmd.c_str());
    dumpPid = MainLoop::currentMainLoop().fork_and_system(boost::bind(&WifiTrack::dumpEnded, this, _1), cmd.c_str(), true, &resultFd);
    if (dumpPid>=0 && resultFd>=0) {
      dumpStream = FdCommPtr(new FdComm(MainLoop::currentMainLoop()));
      dumpStream->setFd(resultFd);
      dumpStream->setReceiveHandler(boost::bind(&WifiTrack::gotDumpLine, this, _1), '\n');
    }
  }
  // ready
  setInitialized();
}


void WifiTrack::dumpEnded(ErrorPtr aError)
{
  OLOG(LOG_NOTICE, "tcpdump terminated with status: %s", Error::text(aError));
  restartTicket.executeOnce(boost::bind(&WifiTrack::startScanner, this), 5*Second);
}


void WifiTrack::restartScanner()
{
  if (dumpPid>=0) {
    kill(dumpPid, SIGTERM);
    dumpPid = -1;
    // killing tcpdump should cause dumpEnded() and automatic restart
  }
  // anyway, if not restarted after 15 seconds, try anyway
  restartTicket.executeOnce(boost::bind(&WifiTrack::startScanner, this), 15*Second);
}




void WifiTrack::gotDumpLine(ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    OLOG(LOG_ERR, "error reading from tcp output stream: %s", Error::text(aError));
    return;
  }
  string line;
  if (dumpStream->receiveDelimitedString(line)) {
    OLOG(LOG_DEBUG, "TCPDUMP: %s", line.c_str());
    // 17:40:22.356367 1.0 Mb/s 2412 MHz 11b -75dBm signal -75dBm signal antenna 0 -109dBm signal antenna 1 BSSID:5c:49:79:6d:28:1a (oui Unknown) DA:5c:49:79:6d:28:1a (oui Unknown) SA:c8:bc:c8:be:0d:0a (oui Unknown) Probe Request (iWay_Fiber_bu725) [1.0* 2.0* 5.5* 11.0* 6.0 9.0 12.0 18.0 Mbit]
    bool decoded = false;
    bool beacon = false;
    int rssi = 0;
    uint64_t mac = 0;
    string ssid;
    size_t s,e;
    // - rssi (signal)
    e = line.find(" signal ");
    if (e!=string::npos) {
      s = line.rfind(" ", e-1);
      if (s!=string::npos) {
        sscanf(line.c_str()+s+1, "%d", &rssi);
      }
      if (scanBeacons) {
        s = line.find("Beacon (", s);
        if (s!=string::npos) {
          // Is a beacon, get SSID
          s += 8;
          e = line.find(") ", s);
          ssid = line.substr(s, e-s);
          decoded = true;
          beacon = true;
        }
      }
      if (!decoded) {
        // must be Probe Request
        // - sender MAC (source address)
        s = line.find("SA:");
        if (s!=string::npos) {
          mac = stringToMacAddress(line.c_str()+s+3);
          // - name of SSID probed
          s = line.find("Probe Request (", s);
          if (s!=string::npos) {
            s += 15;
            e = line.find(") ", s);
            ssid = line.substr(s, e-s);
            // - check min rssi
            if (rssi<minProcessRssi) {
              FOCUSOLOG("Too weak: RSSI=%d<%d, MAC=%s, SSID='%s'", rssi, minProcessRssi, macAddressToString(mac,':').c_str(), ssid.c_str());
            }
            else {
              decoded = true;
            }
          }
        }
      }
    }
    if (decoded) {
      WTSSidPtr s;
      WTMacPtr m;
      bool newSSID = false;
      // record
      MLMicroSeconds now = MainLoop::now();
      // - SSID
      bool newSSidForMac = false;
      WTSSidMap::iterator ssidPos = ssids.find(ssid);
      if (ssidPos!=ssids.end()) {
        s = ssidPos->second;
      }
      else {
        // unknown, create
        newSSID = true;
        s = WTSSidPtr(new WTSSid);
        s->ssid = ssid;
        ssids[ssid] = s;
      }
      if (beacon) {
        // just record beacon sighting
        if (s->beaconSeenLast==Never) {
          OLOG(LOG_INFO, "New Beacon found: RSSI=%d, SSID='%s'", rssi, ssid.c_str());
        }
        s->beaconSeenLast = now;
        s->beaconRssi = rssi;
      }
      else {
        // process probe request
        FOCUSOLOG("RSSI=%d, MAC=%s, SSID='%s'", rssi, macAddressToString(mac,':').c_str(), ssid.c_str());
        s->seenLast = now;
        s->seenCount++;
        // - MAC
        WTMacMap::iterator macPos = macs.find(mac);
        if (macPos!=macs.end()) {
          m = macPos->second;
        }
        else {
          // unknown, create
          if (!s->ssid.empty() || rememberWithoutSsid) {
            m = WTMacPtr(new WTMac);
            m->mac = mac;
            m->ouiName = ouiName(mac);
            macs[mac] = m;
          }
        }
        if (m) {
          m->seenCount++;
          m->seenLast = now;
          if (m->seenFirst==Never) m->seenFirst = now;
          m->lastRssi = rssi;
          if (rssi>m->bestRssi) m->bestRssi = rssi;
          if (rssi<m->worstRssi) m->worstRssi = rssi;
          // - connection (if not empty ssid or empty ssids are allowed)
          if (!s->ssid.empty() || rememberWithoutSsid) {
            if (m->ssids.find(s)==m->ssids.end()) {
              newSSidForMac = true;
              m->ssids.insert(s);
            }
            s->macs.insert(m);
          }
          // process sighting
          if (aggregatePersons) {
            processSighting(m, s, newSSidForMac);
          }
        }
      }
      if (reportSightings && apiNotify) {
        JsonObjectPtr message = JsonObject::newObj();
        JsonObjectPtr sighting = JsonObject::newObj();
        sighting->add("type", JsonObject::newString(beacon ? "beacon" : "probe"));
        sighting->add("newSSID", JsonObject::newBool(newSSID));
        if (m) {
          sighting->add("MAC", JsonObject::newString(macAddressToString(m->mac,':')));
          sighting->add("MACsightings", JsonObject::newInt64(m->seenCount));
          sighting->add("OUIname", JsonObject::newString(m->ouiName));
          sighting->add("rssi", JsonObject::newInt32(m->lastRssi));
          sighting->add("worstRssi", JsonObject::newInt32(m->worstRssi));
          sighting->add("bestRssi", JsonObject::newInt32(m->bestRssi));
        }
        if (s) {
          sighting->add("SSID", JsonObject::newString(s->ssid));
          sighting->add("SSIDsightings", JsonObject::newInt64(s->seenCount));
          sighting->add("hidden", JsonObject::newBool(s->hidden));
          sighting->add("beaconRssi", JsonObject::newInt32(s->beaconRssi));
        }
        message->add("sighting", sighting);
        sendEventMessage(message);
      }
    }
  }
}


void WifiTrack::processSighting(WTMacPtr aMac, WTSSidPtr aSSid, bool aNewSSidForMac)
{
  WTPersonPtr person = aMac->person; // default to already existing, if any
  // log
  if (FOCUSOLOGENABLED) {
    string s;
    const char* sep = "";
    for (WTSSidSet::iterator pos = aMac->ssids.begin(); pos!=aMac->ssids.end(); ++pos) {
      string sstr = (*pos)->ssid;
      if (sstr.empty()) sstr = "<undefined>";
      string_format_append(s, "%s%s (%ld)", sep, sstr.c_str(), (*pos)->seenCount);
      sep = ", ";
    }
    FOCUSOLOG(
      "Sighted%s: MAC=%s, %s (%ld), RSSI=%d,%d,%d : %s",
      person ? " and already has person" : "",
      macAddressToString(aMac->mac,':').c_str(),
      nonNullCStr(aMac->ouiName),
      aMac->seenCount,
      aMac->worstRssi, aMac->lastRssi, aMac->bestRssi,
      s.c_str()
    );
  }
  // process
  if (aNewSSidForMac && aSSid->macs.size()<tooCommonMacCount) {
    // a new SSID for this Mac, not too commonly used
    FOCUSOLOG("- not too common (only %lu MACs)", aSSid->macs.size());
    WTMacSet relatedMacs;
    WTMacPtr mostCommonMac;
    WTPersonPtr mostProbablePerson;
    if (aMac->ssids.size()>=minCommonSsidCount) {
      // has enough ssids overall -> try to find related MACs
      // - search all macs that know the new ssid
      int maxCommonSsids = 0;
      for (WTMacSet::iterator mpos = aSSid->macs.begin(); mpos!=aSSid->macs.end(); ++mpos) {
        // - see how many other ssids this mac shares with the other one
        if (*mpos==aMac) continue; // avoid comparing with myself!
        if ((*mpos)->ssids.size()<minCommonSsidCount) continue; // shortcut, candidate does not have enough ssids to possibly match at all -> next
        int commonSsids = 1; // we have at least aSSid in common by definition when we get here!
        for (WTSSidSet::iterator spos = (*mpos)->ssids.begin(); spos!=(*mpos)->ssids.end(); ++spos) {
          if (*spos==aSSid) continue; // shortcut, we know that we have aSSid in common
  //        if ((*spos)->macs.size()>=tooCommonMacCount) continue; // this is a too common ssid, don't count (maybe we still should %%%)
          if (aMac->ssids.find(*spos)!=aMac->ssids.end()) {
            commonSsids++;
          }
        }
        if (commonSsids<minCommonSsidCount) continue; // not a candidate
        OLOG(LOG_INFO, "- This MAC %s has %d SSIDs in common with %s -> link to same person",
          macAddressToString(aMac->mac,':').c_str(),
          commonSsids,
          macAddressToString((*mpos)->mac,':').c_str()
        );
        relatedMacs.insert(*mpos); // is a candidate
        if (commonSsids>maxCommonSsids) {
          mostCommonMac = *mpos; // this is the mac with most common ssids
          if (mostCommonMac->person) mostProbablePerson = mostCommonMac->person; // this is the person of the mac with the most common ssids -> most likely the correct one
        }
      }
    }
    // determine person
    if (!person) {
      if (mostProbablePerson) {
        person = mostProbablePerson;
      }
      else {
        // none of the related macs has a person, or we have no related macs at all -> we need to create a person
        person = WTPersonPtr(new WTPerson);
        persons.insert(person);
        person->imageIndex = rand() % numPersonImages;
        person->color = hsbToPixel(rand() % 360);
        // link to this mac (without logging, as this happens for every new Mac seen)
        aMac->person = person;
        person->macs.insert(aMac);
      }
    }
    if (person) {
      // assign to all macs found related
      if (person->macs.insert(aMac).second) {
        OLOG(LOG_NOTICE, "+++ MAC %s, %s via '%s' (just sighted) -> now linked to person '%s' (%d/%s), MACs=%lu",
          macAddressToString(aMac->mac,':').c_str(),
          nonNullCStr(aMac->ouiName),
          aSSid->ssid.c_str(),
          person->name.c_str(),
          person->imageIndex,
          pixelToWebColor(person->color, true).c_str(),
          person->macs.size()
        );
      }
      for (WTMacSet::iterator mpos = relatedMacs.begin(); mpos!=relatedMacs.end(); ++mpos) {
        WTPersonPtr oldPerson = (*mpos)->person;
        if (oldPerson && oldPerson!=person) {
          oldPerson->macs.erase(*mpos); // remove this mac from the person
          if (oldPerson->macs.size()==0) {
            persons.erase(oldPerson); // delete person with zero macs assigned
            if (oldPerson->seenFirst<person->seenFirst && !oldPerson->hidden && oldPerson->shownLast!=Never) {
              // now orphaned person was older -> clone its appearance to maintain continuity as much as possible
              person->color = oldPerson->color;
              person->imageIndex = oldPerson->imageIndex;
              person->name = oldPerson->name;
              person->seenCount += oldPerson->seenCount; // cumulate count
              person->seenFirst = oldPerson->seenFirst; // inherit age
              if (person->bestRssi<oldPerson->bestRssi) person->bestRssi = oldPerson->bestRssi;
              if (person->worstRssi>oldPerson->worstRssi) person->worstRssi = oldPerson->worstRssi;
              OLOG(LOG_NOTICE, "--- Using older appearance '%s' (%d/%s) for new combined person from now on",
                oldPerson->name.c_str(),
                oldPerson->imageIndex,
                pixelToWebColor(oldPerson->color, true).c_str()
              );
            }
            else {
              OLOG(LOG_NOTICE, "--- Person '%s' (%d/%s) not linked to a MAC any more -> deleted",
                oldPerson->name.c_str(),
                oldPerson->imageIndex,
                pixelToWebColor(oldPerson->color, true).c_str()
              );
            }
          }
        }
        // assign new person
        (*mpos)->person = person;
        if (person->macs.insert(*mpos).second) {
          OLOG(LOG_NOTICE, "+++ Found other MAC %s, %s related -> now linked to person '%s' (%d/%s), MACs=%lu",
            macAddressToString((*mpos)->mac,':').c_str(),
            nonNullCStr((*mpos)->ouiName),
            person->name.c_str(),
            person->imageIndex,
            pixelToWebColor(person->color, true).c_str(),
            person->macs.size()
          );
        }
      }
    }
  }
  // person determined, if any
  if (person) {
    // seen the person, update it
    person->seenCount++;
    person->seenLast = aMac->seenLast;
    person->lastRssi = aMac->lastRssi;
    if (person->bestRssi<person->lastRssi) person->bestRssi = person->lastRssi;
    if (person->worstRssi>person->lastRssi) person->worstRssi = person->lastRssi;
    if (person->seenFirst==Never) person->seenFirst = person->seenLast;
    OLOG(person->hidden || aMac->hidden ? LOG_DEBUG : LOG_INFO, "=== Recognized person%s, '%s', (%d/%s), linked MACs=%lu, via ssid='%s', MAC=%s, %s%s (%d, best: %d)",
      person->hidden ? " (hidden)" : "",
      person->name.c_str(),
      person->imageIndex,
      pixelToWebColor(person->color, true).c_str(),
      person->macs.size(),
      aSSid->ssid.c_str(),
      macAddressToString(aMac->mac,':').c_str(),
      nonNullCStr(aMac->ouiName),
      aMac->hidden ? " (hidden)" : "",
      aMac->lastRssi,
      aMac->bestRssi
    );
    // show person?
    if (!aMac->hidden && !person->hidden && person->lastRssi>=minShowRssi && person->seenLast>person->shownLast+minShowInterval) {
      // determine name
      string nameToShow = person->name;
      if (nameToShow.empty()) {
        // pick SSID with the least mac links as most relevant (because: unique) name
        long minMacs = 999999999;
        WTSSidPtr relevantSSid;
        for (WTMacSet::iterator mpos = person->macs.begin(); mpos!=person->macs.end(); ++mpos) {
          for (WTSSidSet::iterator spos = (*mpos)->ssids.begin(); spos!=(*mpos)->ssids.end(); ++spos) {
            if (!(*spos)->hidden && (*spos)->macs.size()<minMacs && !(*spos)->ssid.empty()) {
              minMacs = (*spos)->macs.size();
              relevantSSid = (*spos);
            }
          }
        }
        OLOG(LOG_DEBUG, "minMacs = %ld, relevantSSid='%s'", minMacs, relevantSSid ? relevantSSid->ssid.c_str() : "<none>");
        if (relevantSSid) {
          nameToShow = relevantSSid->ssid;
        }
      }
      if (!nameToShow.empty()) {
        // show message
        person->shownLast = person->seenLast;
        OLOG(LOG_NOTICE, "*** Showing person as '%s' (%d/%s) via %s, %s / '%s' (%d, best: %d)",
          nameToShow.c_str(),
          person->imageIndex,
          pixelToWebColor(person->color, true).c_str(),
          macAddressToString(aMac->mac,':').c_str(),
          nonNullCStr(aMac->ouiName),
          aSSid->ssid.c_str(),
          person->lastRssi,
          person->bestRssi
        );
        displayEncounter("hi", person->imageIndex, person->color, nameToShow!=aSSid->ssid ? nameToShow : "", nonNullCStr(aMac->ouiName), aSSid->ssid);
      }
    }
  }
  // check for regular saves
  MLMicroSeconds now = MainLoop::now();
  if (saveTempInterval!=Never && now>lastTempAutoSave+saveTempInterval) {
    lastTempAutoSave = now;
    OLOG(LOG_NOTICE,">>> auto-saving data to temp file")
    save(Application::sharedApplication()->tempPath(WIFITRACK_STATE_FILE_NAME));
  }
  if (saveDataInterval!=Never && now>lastDataAutoSave+saveDataInterval) {
    lastDataAutoSave = now;
    OLOG(LOG_NOTICE,">>> auto-saving data to (persistent) data file")
    save(Application::sharedApplication()->dataPath(WIFITRACK_STATE_FILE_NAME));
  }
}



void WifiTrack::displayEncounter(string aIntro, int aImageIndex, PixelColor aColor, string aName, string aBrand, string aTarget)
{
  if (directDisplay && disp) {
    MLMicroSeconds rst = disp->getRemainingScrollTime(true, true); // purge old views
    if (rst<maxDisplayDelay) {
      if (OLOGENABLED(LOG_INFO)) {
        ViewScrollerPtr sc = disp->getDispScroller();
        ViewStackPtr st;
        if (sc) st = boost::dynamic_pointer_cast<ViewStack>(sc->getScrolledView());
        if (st) {
          PixelRect r;
          st->getEnclosingContentRect(r);
          OLOG(LOG_INFO, "Remaining scroll time before this message will appear is %.2f Seconds, scrollX=%d, frame_x=%d/dx=%d, content_x=%d/dx=%d, enclosing_x=%d/dx=%d, stacksz=%zu", (double)rst/Second, (int)sc->getOffsetX(), st->getFrame().x, st->getFrame().dx, st->getContent().x, st->getContent().dx, r.x, r.dx, st->numViews());
        }
      }
      if (rst<-1*Second) {
        // scrolling is derailed, re-sync
        OLOG(LOG_WARNING, "Scrolling de-synchronized (actual content out of view) -> reset scrolling");
        disp->resetScroll();
      }
      #if ENABLE_LEGACY_FEATURE_SCRIPTS
      // use eventscript instead to handle wifiscroll events
      FeatureApi::SubstitutionMap subst;
      subst["HASINTRO"] = aIntro.size()>0 ? "1" : "0";
      subst["INTRO"] = aIntro;
      subst["IMGIDX"] = string_format("%d", aImageIndex);
      subst["COLOR"] = pixelToWebColor(aColor, false);
      subst["HASNAME"] = aName.size()>0 ? "1" : "0";
      subst["NAME"] = aName;
      subst["HASBRAND"] = aBrand.size()>0 ? "1" : "0";
      subst["BRAND"] = aBrand;
      subst["HASTARGET"] = aTarget.size()>0 ? "1" : "0";
      subst["TARGET"] = aTarget;
      loadingContent = false; // because calling script will terminate previous script without callback, make sure loading is not kept in progress (would never get out)
      FeatureApi::sharedApi()->runJsonFile("scripts/showssid.json", NoOP, &scriptContext, &subst);
      #endif // ENABLE_LEGACY_FEATURE_SCRIPTS
    }
    else {
      OLOG(LOG_WARNING, "Cannot push to scroll text (scroll delay would be > %.1f Seconds)", (double)maxDisplayDelay/Second);
    }
  }
  if (apiNotify) {
    JsonObjectPtr message = JsonObject::newObj();
    JsonObjectPtr personinfo = JsonObject::newObj();
    personinfo->add("HASINTRO", JsonObject::newString(aIntro.size()>0 ? "1" : "0"));
    personinfo->add("INTRO", JsonObject::newString(aIntro));
    personinfo->add("IMGIDX", JsonObject::newString(string_format("%d", aImageIndex)));
    personinfo->add("COLOR", JsonObject::newString(pixelToWebColor(aColor, false)));
    personinfo->add("HASNAME", JsonObject::newString(aName.size()>0 ? "1" : "0"));
    personinfo->add("NAME", JsonObject::newString(aName));
    personinfo->add("HASBRAND", JsonObject::newString(aBrand.size()>0 ? "1" : "0"));
    personinfo->add("BRAND", JsonObject::newString(aBrand));
    personinfo->add("HASTARGET", JsonObject::newString(aTarget.size()>0 ? "1" : "0"));
    personinfo->add("TARGET", JsonObject::newString(aTarget));
    message->add("personinfo", personinfo);
    sendEventMessage(message);
  }
}


bool WifiTrack::needContentHandler()
{
  if (!loadingContent) {
    loadingContent = true;
    FOCUSOLOG("Display needs content - calling wifipause script");
    #if ENABLE_LEGACY_FEATURE_SCRIPTS
    ErrorPtr err = FeatureApi::sharedApi()->runJsonFile("scripts/wifipause.json", boost::bind(&WifiTrack::contentLoaded, this), &scriptContext, NULL);
    if (!Error::isOK(err)) {
      loadingContent = false;
      OLOG(LOG_WARNING, "wifipause script could not be run: %s", Error::text(err));
    }
    #endif
    // report
    JsonObjectPtr message = JsonObject::newObj();
    message->add("event", JsonObject::newString("needcontent"));
    sendEventMessage(message);
  }
  return true; // anyway, keep scrolling
}


void WifiTrack::contentLoaded()
{
  loadingContent = false;
  FOCUSOLOG("Content loading complete");
}


ErrorPtr WifiTrack::runTool()
{
  // FIXME: tdb
  #warning tbd
  return TextError::err("Not yet implemented");
}

#endif // ENABLE_FEATURE_WIFITRACK
