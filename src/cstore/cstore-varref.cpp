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

#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>

#include <cli_cstore.h>
#include <cstore/cstore-varref.hpp>

using namespace cstore;

////// constructors/destructors
Cstore::VarRef::VarRef(Cstore *cstore, const string& ref_str, bool active)
  : _cstore(cstore), _active(active)
{
  /* NOTE: this class will change the paths in the cstore. caller must do
   *       save/restore for the cstore if necessary.
   */
  if (!_cstore) {
    // no cstore
    return;
  }

  _absolute = (ref_str[0] == '/');
  vector<string> tmp;
  while (!_absolute && !_cstore->cfg_path_at_root()) {
    string last;
    _cstore->pop_cfg_path(last);
    tmp.push_back(last);
  }
  while (tmp.size() > 0) {
    _orig_path_comps.push(tmp.back());
    tmp.pop_back();
  }
  _cstore->reset_paths();
  /* at this point, cstore paths are at root. _orig_path_comps contains
   * the path originally in cstore (or empty if _absolute).
   */

  size_t si = (_absolute ? 1 : 0);
  size_t sn = 0;
  Cpath rcomps;
  while (si < ref_str.length()
         && (sn = ref_str.find('/', si)) != ref_str.npos) {
    rcomps.push(ref_str.substr(si, sn - si));
    si = sn + 1;
  }
  if (si < ref_str.length()) {
    rcomps.push(ref_str.substr(si));
  }
  // NOTE: if path ends in '/', the trailing slash is ignored.

  // get the "at" string. this is set inside cli_new.c.
  _at_string = get_at_string();

  // process ref
  Cpath pcomps(_orig_path_comps);
  process_ref(rcomps, pcomps, ERROR_TYPE);
}

/* process the reference(s).
 * this is a recursive function and always keeps the cstore paths unchanged
 * between invocations.
 *
 * note: def_type is added into _paths along with the paths. when it's
 *       ERROR_TYPE, it means the path needs to be checked for existence.
 *       otherwise, the path is a "value" (or "values") read from the
 *       actual config (working or active).
 */
void
Cstore::VarRef::process_ref(const Cpath& ref_comps,
                            const Cpath& cur_path_comps,
                            vtw_type_e def_type)
{
  if (ref_comps.size() == 0) {
    // done
    _paths.push_back(pair<Cpath, vtw_type_e>(cur_path_comps, def_type));
    return;
  }

  Cpath rcomps;
  Cpath pcomps(cur_path_comps);
  string cr_comp = ref_comps[0];
  for (size_t i = 1; i < ref_comps.size(); i++) {
    rcomps.push(ref_comps[i]);
  }

  tr1::shared_ptr<Ctemplate> def(_cstore->parseTmpl(pcomps, false));
  bool got_tmpl = (def.get() != 0);
  bool handle_leaf = false;
  if (cr_comp == "@") {
    if (!got_tmpl) {
      // invalid path
      return;
    }
    if (def->isTypeless()) {
      // no value for typeless node
      return;
    }
    if (pcomps.size() == _orig_path_comps.size()) {
      if (pcomps.size() == 0 || pcomps == _orig_path_comps) {
        /* we are at the original path. this is a self-reference, e.g.,
         * $VAR(@), so use the "at string".
         */
        pcomps.push(_at_string);
        process_ref(rcomps, pcomps, def->getType(1));
        return;
      }
    }
    if (!def->isSingleLeafNode() && !def->isMultiLeafNode()) {
      if (pcomps.size() < _orig_path_comps.size()) {
        // within the original path. @ translates to the path comp.
        pcomps.push(_orig_path_comps[pcomps.size()]);
        process_ref(rcomps, pcomps, def->getType(1));
      }
      return;
    }
    // handle leaf node
    handle_leaf = true;
  } else if (cr_comp == ".") {
    process_ref(rcomps, pcomps, ERROR_TYPE);
  } else if (cr_comp == "..") {
    if (!got_tmpl || pcomps.size() == 0) {
      // invalid path
      return;
    }
    pcomps.pop();
    def = _cstore->parseTmpl(pcomps, false);
    if (!def.get()) {
      // invalid tmpl path
      return;
    }
    if (def->isTagValue()) {
      // at "tag value", need to pop one more.
      if (pcomps.size() == 0) {
        // invalid path
        return;
      }
      pcomps.pop();
    }
    process_ref(rcomps, pcomps, ERROR_TYPE);
  } else if (cr_comp == "@@") {
    if (!got_tmpl) {
      // invalid path
      return;
    }
    if (def->isTypeless()) {
      // no value for typeless node
      return;
    }
    if (def->isValue()) {
      // invalid ref
      return;
    }
    if (def->isTag()) {
      // tag node
      vector<string> cnodes;
      _cstore->cfgPathGetChildNodes(pcomps, cnodes, _active);
      for (size_t i = 0; i < cnodes.size(); i++) {
        pcomps.push(cnodes[i]);
        process_ref(rcomps, pcomps, def->getType(1));
        pcomps.pop();
      }
    } else {
      // handle leaf node
      handle_leaf = true;
    }
  } else {
    // just text. go down 1 level.
    if (got_tmpl && def->isTagNode()) {
      // at "tag node". need to go down 1 more level.
      if (pcomps.size() > _orig_path_comps.size()) {
        // already under the original node. invalid ref.
        return;
      } else if (pcomps.size() == _orig_path_comps.size()) {
        // at the tag value. use the at_string.
        pcomps.push(_at_string);
      } else  {
        // within the original path. take the original tag value.
        pcomps.push(_orig_path_comps[pcomps.size()]);
      }
    }
    pcomps.push(cr_comp);
    process_ref(rcomps, pcomps, ERROR_TYPE);
  }

  if (handle_leaf) {
    if (def->isMulti()) {
      // multi-value node
      vector<string> vals;
      if (!_cstore->cfgPathGetValues(pcomps, vals, _active)) {
        return;
      }
      string val;
      for (size_t i = 0; i < vals.size(); i++) {
        if (val.length() > 0) {
          val += " ";
        }
        val += vals[i];
      }
      pcomps.push(val);
      // treat "joined" multi-values as TEXT_TYPE
      _paths.push_back(pair<Cpath, vtw_type_e>(pcomps, TEXT_TYPE));
      // at leaf. stop recursion.
    } else {
      // single-value node
      string val;
      vtw_type_e t = def->getType(1);
      if (!_cstore->cfgPathGetValue(pcomps, val, _active)) {
        /* can't get value => treat it as non-existent (empty value
         * and type ERROR_TYPE)
         */
        val = "";
        t = ERROR_TYPE;
      }
      pcomps.push(val);
      _paths.push_back(pair<Cpath, vtw_type_e>(pcomps, t));
      // at leaf. stop recursion.
    }
  }
}

bool
Cstore::VarRef::getValue(string& value, vtw_type_e& def_type)
{
  vector<string> result;
  MapT<string, bool> added;
  def_type = ERROR_TYPE;
  for (size_t i = 0; i < _paths.size(); i++) {
    if (_paths[i].first.size() == 0) {
      // empty path
      continue;
    }
    if (added.find(_paths[i].first.back()) != added.end()) {
      // already added
      continue;
    }
    if (_paths[i].second == ERROR_TYPE
        && !_cstore->cfgPathExists(_paths[i].first, _active)) {
      // path doesn't exist => empty string
      added[""] = true;
      result.push_back("");
      continue;
    }
    if (_paths[i].second != ERROR_TYPE) {
      // set def_type. all types should be the same if multiple entries exist.
      def_type = _paths[i].second;
    }
    added[_paths[i].first.back()] = true;
    result.push_back(_paths[i].first.back());
  }
  if (result.size() == 0) {
    // got nothing
    return false;
  }
  if (result.size() > 1 || def_type == ERROR_TYPE) {
    /* if no type is available or we are returning "joined" multiple values,
     * treat it as text type.
     */
    def_type = TEXT_TYPE;
  }
  value = "";
  for (size_t i = 0; i < result.size(); i++) {
    if (i > 0) {
      value += " ";
    }
    value += result[i];
  }
  return true;
}

bool
Cstore::VarRef::getSetPath(Cpath& path_comps)
{
  /* XXX this function is currently unused and untested. see setVarRef()
   *     in Cstore for more information.
   */
  if (_paths.size() != 1) {
    // for set_var_ref operation, there can be only one path.
    return false;
  }
  path_comps = _paths[0].first;
  /* note that for "varref set" operation, the varref must refer to the
   * "value" of a single-value leaf node, e.g.,
   * "$VAR(plaintext-password/@)". so pop the last comp to give the
   * correct path for "set". the caller is responsible for verifying
   * whether the path points to a single-value leaf node.
   */
  path_comps.pop();
  return true;
}

