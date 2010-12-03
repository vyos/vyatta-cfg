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

/* currently use the UnionfsCstore implementation */
#include <cstore/unionfs/cstore-unionfs.hpp>

typedef SV STRVEC;
typedef SV STRSTRMAP;

MODULE = Cstore		PACKAGE = Cstore		


Cstore *
Cstore::new()
CODE:
  RETVAL = new UnionfsCstore();
OUTPUT:
  RETVAL


bool
Cstore::cfgPathExists(STRVEC *vref, bool active_cfg)
PREINIT:
  vector<string> arg_strvec;
CODE:
  RETVAL = THIS->cfgPathExists(arg_strvec, active_cfg);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathDefault(STRVEC *vref, bool active_cfg)
PREINIT:
  vector<string> arg_strvec;
CODE:
  RETVAL = THIS->cfgPathDefault(arg_strvec, active_cfg);
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetChildNodes(STRVEC *vref, bool active_cfg)
PREINIT:
  vector<string> arg_strvec;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetChildNodes(arg_strvec, ret_strvec, active_cfg);
OUTPUT:
  RETVAL


SV *
Cstore::cfgPathGetValue(STRVEC *vref, bool active_cfg)
PREINIT:
  vector<string> arg_strvec;
CODE:
  string value;
  if (THIS->cfgPathGetValue(arg_strvec, value, active_cfg)) {
    RETVAL = newSVpv(value.c_str(), 0);
  } else {
    XSRETURN_UNDEF;
  }
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetValues(STRVEC *vref, bool active_cfg)
PREINIT:
  vector<string> arg_strvec;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetValues(arg_strvec, ret_strvec, active_cfg);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathEffective(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  RETVAL = THIS->cfgPathEffective(arg_strvec);
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetEffectiveChildNodes(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetEffectiveChildNodes(arg_strvec, ret_strvec);
OUTPUT:
  RETVAL


SV *
Cstore::cfgPathGetEffectiveValue(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  string value;
  if (THIS->cfgPathGetEffectiveValue(arg_strvec, value)) {
    RETVAL = newSVpv(value.c_str(), 0);
  } else {
    XSRETURN_UNDEF;
  }
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetEffectiveValues(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetEffectiveValues(arg_strvec, ret_strvec);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathDeleted(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  RETVAL = THIS->cfgPathDeleted(arg_strvec);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathAdded(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  RETVAL = THIS->cfgPathAdded(arg_strvec);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathChanged(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  RETVAL = THIS->cfgPathChanged(arg_strvec);
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetDeletedChildNodes(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetDeletedChildNodes(arg_strvec, ret_strvec);
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetDeletedValues(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetDeletedValues(arg_strvec, ret_strvec);
OUTPUT:
  RETVAL


STRSTRMAP *
Cstore::cfgPathGetChildNodesStatus(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  Cstore::MapT<string, string> ret_strstrmap;
  THIS->cfgPathGetChildNodesStatus(arg_strvec, ret_strstrmap);
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetValuesDA(STRVEC *vref, bool active_cfg)
PREINIT:
  vector<string> arg_strvec;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetValuesDA(arg_strvec, ret_strvec, active_cfg);
OUTPUT:
  RETVAL


SV *
Cstore::cfgPathGetValueDA(STRVEC *vref, bool active_cfg)
PREINIT:
  vector<string> arg_strvec;
CODE:
  string value;
  if (THIS->cfgPathGetValueDA(arg_strvec, value, active_cfg)) {
    RETVAL = newSVpv(value.c_str(), 0);
  } else {
    XSRETURN_UNDEF;
  }
OUTPUT:
  RETVAL


STRVEC *
Cstore::cfgPathGetChildNodesDA(STRVEC *vref, bool active_cfg)
PREINIT:
  vector<string> arg_strvec;
CODE:
  vector<string> ret_strvec;
  THIS->cfgPathGetChildNodesDA(arg_strvec, ret_strvec, active_cfg);
OUTPUT:
  RETVAL


bool
Cstore::cfgPathDeactivated(STRVEC *vref, bool active_cfg)
PREINIT:
  vector<string> arg_strvec;
CODE:
  RETVAL = THIS->cfgPathDeactivated(arg_strvec, active_cfg);
OUTPUT:
  RETVAL


STRSTRMAP *
Cstore::cfgPathGetChildNodesStatusDA(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  Cstore::MapT<string, string> ret_strstrmap;
  THIS->cfgPathGetChildNodesStatusDA(arg_strvec, ret_strstrmap);
OUTPUT:
  RETVAL


STRVEC *
Cstore::tmplGetChildNodes(STRVEC *vref)
PREINIT:
  vector<string> arg_strvec;
CODE:
  vector<string> ret_strvec;
  THIS->tmplGetChildNodes(arg_strvec, ret_strvec);
OUTPUT:
  RETVAL


bool
Cstore::validateTmplPath(STRVEC *vref, bool validate_vals)
PREINIT:
  vector<string> arg_strvec;
CODE:
  RETVAL = THIS->validateTmplPath(arg_strvec, validate_vals);
OUTPUT:
  RETVAL


STRSTRMAP *
Cstore::getParsedTmpl(STRVEC *vref, bool allow_val)
PREINIT:
  vector<string> arg_strvec;
CODE:
  Cstore::MapT<string, string> ret_strstrmap;
  if (!THIS->getParsedTmpl(arg_strvec, ret_strstrmap, allow_val)) {
    XSRETURN_UNDEF;
  }
OUTPUT:
  RETVAL


SV *
Cstore::cfgPathGetComment(STRVEC *vref, bool active_cfg)
PREINIT:
  vector<string> arg_strvec;
CODE:
  string comment;
  if (THIS->cfgPathGetComment(arg_strvec, comment, active_cfg)) {
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


