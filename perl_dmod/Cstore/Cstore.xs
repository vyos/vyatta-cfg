/*
 * Copyright (C) 2010 Vyatta, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

/* these macros are defined in perl headers but conflict with C++ headers */
#undef do_open
#undef do_close

#include <cstring>
#include <vector>
#include <string>

#include <cstore/cstore.hpp>

using namespace cstore;

typedef SV STRVEC;
typedef SV CPATH;
typedef SV STRSTRMAP;

MODULE = Cstore		PACKAGE = Cstore		


Cstore *
Cstore::new()
CODE:
  RETVAL = Cstore::createCstore(false);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathExists(CPATH *pref, bool active_cfg)
PREINIT:
  Cpath arg_cpath;
CODE:
  RETVAL = THIS->cfgPathExists(arg_cpath, active_cfg);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathDefault(CPATH *pref, bool active_cfg)
PREINIT:
  Cpath arg_cpath;
CODE:
  RETVAL = THIS->cfgPathDefault(arg_cpath, active_cfg);
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetChildNodes(CPATH *pref, bool active_cfg)
PREINIT:
  Cpath arg_cpath;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetChildNodes(arg_cpath, ret_strvec, active_cfg);
OUTPUT:
  RETVAL


SV *
Cstore::cfgPathGetValue(CPATH *pref, bool active_cfg)
PREINIT:
  Cpath arg_cpath;
CODE:
  string value;
  if (THIS->cfgPathGetValue(arg_cpath, value, active_cfg)) {
    RETVAL = newSVpv(value.c_str(), 0);
  } else {
    XSRETURN_UNDEF;
  }
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetValues(CPATH *pref, bool active_cfg)
PREINIT:
  Cpath arg_cpath;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetValues(arg_cpath, ret_strvec, active_cfg);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathEffective(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  RETVAL = THIS->cfgPathEffective(arg_cpath);
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetEffectiveChildNodes(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetEffectiveChildNodes(arg_cpath, ret_strvec);
OUTPUT:
  RETVAL


SV *
Cstore::cfgPathGetEffectiveValue(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  string value;
  if (THIS->cfgPathGetEffectiveValue(arg_cpath, value)) {
    RETVAL = newSVpv(value.c_str(), 0);
  } else {
    XSRETURN_UNDEF;
  }
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetEffectiveValues(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetEffectiveValues(arg_cpath, ret_strvec);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathDeleted(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  RETVAL = THIS->cfgPathDeleted(arg_cpath);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathAdded(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  RETVAL = THIS->cfgPathAdded(arg_cpath);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathChanged(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  RETVAL = THIS->cfgPathChanged(arg_cpath);
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetDeletedChildNodes(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetDeletedChildNodes(arg_cpath, ret_strvec);
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetDeletedValues(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetDeletedValues(arg_cpath, ret_strvec);
OUTPUT:
  RETVAL


STRSTRMAP *
Cstore::cfgPathGetChildNodesStatus(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  Cstore::MapT<string, string> ret_strstrmap;
  THIS->cfgPathGetChildNodesStatus(arg_cpath, ret_strstrmap);
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetValuesDA(CPATH *pref, bool active_cfg)
PREINIT:
  Cpath arg_cpath;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetValuesDA(arg_cpath, ret_strvec, active_cfg);
OUTPUT:
  RETVAL


SV *
Cstore::cfgPathGetValueDA(CPATH *pref, bool active_cfg)
PREINIT:
  Cpath arg_cpath;
CODE:
  string value;
  if (THIS->cfgPathGetValueDA(arg_cpath, value, active_cfg)) {
    RETVAL = newSVpv(value.c_str(), 0);
  } else {
    XSRETURN_UNDEF;
  }
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetChildNodesDA(CPATH *pref, bool active_cfg)
PREINIT:
  Cpath arg_cpath;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetChildNodesDA(arg_cpath, ret_strvec, active_cfg);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathDeactivated(CPATH *pref, bool active_cfg)
PREINIT:
  Cpath arg_cpath;
CODE:
  RETVAL = THIS->cfgPathDeactivated(arg_cpath, active_cfg);
OUTPUT:
  RETVAL


STRSTRMAP *
Cstore::cfgPathGetChildNodesStatusDA(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  Cstore::MapT<string, string> ret_strstrmap;
  THIS->cfgPathGetChildNodesStatusDA(arg_cpath, ret_strstrmap);
OUTPUT:
  RETVAL


STRVEC *
Cstore::tmplGetChildNodes(CPATH *pref)
PREINIT:
  Cpath arg_cpath;
CODE:
  vector<string> ret_strvec;
  THIS->tmplGetChildNodes(arg_cpath, ret_strvec);
OUTPUT:
  RETVAL


bool
Cstore::validateTmplPath(CPATH *pref, bool validate_vals)
PREINIT:
  Cpath arg_cpath;
CODE:
  RETVAL = THIS->validateTmplPath(arg_cpath, validate_vals);
OUTPUT:
  RETVAL


STRSTRMAP *
Cstore::getParsedTmpl(CPATH *pref, bool allow_val)
PREINIT:
  Cpath arg_cpath;
CODE:
  Cstore::MapT<string, string> ret_strstrmap;
  if (!THIS->getParsedTmpl(arg_cpath, ret_strstrmap, allow_val)) {
    XSRETURN_UNDEF;
  }
OUTPUT:
  RETVAL


SV *
Cstore::cfgPathGetComment(CPATH *pref, bool active_cfg)
PREINIT:
  Cpath arg_cpath;
CODE:
  string comment;
  if (THIS->cfgPathGetComment(arg_cpath, comment, active_cfg)) {
    RETVAL = newSVpv(comment.c_str(), 0);
  } else {
    XSRETURN_UNDEF;
  }
OUTPUT:
  RETVAL


bool
Cstore::sessionChanged()
CODE:
  RETVAL = THIS->sessionChanged();
OUTPUT:
  RETVAL


bool
Cstore::loadFile(char *filename)
CODE:
  RETVAL = THIS->loadFile(filename);
OUTPUT:
  RETVAL


bool
Cstore::inSession()
CODE:
  RETVAL = THIS->inSession();
OUTPUT:
  RETVAL
