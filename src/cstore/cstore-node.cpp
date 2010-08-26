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
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#include <cli_cstore.h>
#include <cstore/cstore-node.hpp>

using namespace std;

////// constants
const string CstoreCfgNode::PFX_DIFF_ADD = "+"; // added
const string CstoreCfgNode::PFX_DIFF_DEL = "-"; // deleted
const string CstoreCfgNode::PFX_DIFF_UPD = ">"; // changed
const string CstoreCfgNode::PFX_DIFF_NONE = " ";

const string CstoreCfgNode::PFX_DEACT_D = "!";  // deactivated
const string CstoreCfgNode::PFX_DEACT_DP = "D"; // deactivate pending
const string CstoreCfgNode::PFX_DEACT_AP = "A"; // activate pending
const string CstoreCfgNode::PFX_DEACT_NONE = " ";

bool CstoreCfgNode::_init = false;
Cstore::MapT<string, NodeStatusT> CstoreCfgNode::_st_map;

////// constructors/destructors
CstoreCfgNode::CstoreCfgNode(Cstore& cstore, vector<string>& path_comps,
                             bool active_only, const string& name,
                             NodeStatusT status)
  : _cstore(&cstore), _name(name), _status(status),
    _pfx_deact(PFX_DEACT_NONE.c_str()),
    _is_tag(false), _is_leaf(false), _is_multi(false), _is_value(false),
    _is_default(false), _is_invalid(false), _is_empty(false),
    _active_only(false), _comment(""), _comment_status(ST_STATIC)
{
  init();

  if (!_cstore) {
    return;
  }

  // active_only if specified or not in session
  _active_only = (!_cstore->inSession() || active_only);

  /* first get the def (only if path is not empty). if path is empty, treat
   * it as an intermediate node.
   */
  if (path_comps.size() > 0) {
    vtw_def def;
    if (_cstore->validateTmplPath(path_comps, false, def)) {
      // got the def
      if (def.is_value && !def.tag) {
        // leaf value. should never happen (recursion should have terminated).
        return;
      }
      _is_tag = (def.tag && !def.is_value);
      _is_leaf = (!_is_tag && !def.is_value && def.def_type != ERROR_TYPE);
      _is_multi = (_is_leaf && def.multi);
      _is_value = def.is_value;

      _is_default = _cstore->cfgPathDefault(path_comps, _active_only);

      bool adeact = _cstore->cfgPathDeactivated(path_comps, true);
      /* if active only, pretend deactivate status in working is the same
       * as active to get the right output.
       */
      bool wdeact = (_active_only
                     ? adeact
                       : _cstore->cfgPathDeactivated(path_comps, false));
      if (adeact) {
        if (wdeact) {
          // deactivated in both active and working => deactivated
          _pfx_deact = PFX_DEACT_D.c_str();
        } else {
          // deactivated only in active => activate pending
          _pfx_deact = PFX_DEACT_AP.c_str();
        }
      } else {
        if (wdeact) {
          // deactivated only in working => deactivate pending
          _pfx_deact = PFX_DEACT_DP.c_str();
        }
        // 4th case handled by default
      }
    } else {
      // not a valid node. this should only happen at root.
      _is_invalid = true;
      return;
    }
  }

  // handle comment
  string ac_str;
  bool ac = _cstore->cfgPathGetComment(path_comps, ac_str, true);
  string wc_str;
  bool wc = _cstore->cfgPathGetComment(path_comps, wc_str, false);
  if (ac) {
    if (wc) {
      // has comment in both active and working
      _comment = wc_str;
      if (ac_str != wc_str) {
        _comment_status = ST_CHANGED;
      }
    } else {
      // comment only in active
      _comment = ac_str;
      _comment_status = ST_DELETED;
    }
  } else {
    if (wc) {
      // comment only in working
      _comment = wc_str;
      _comment_status = ST_ADDED;
    }
    // 4th case (neither) handled by default
  }

  if (_is_leaf) {
    /* leaf node. set the name if this is the root (note path_comps must be
     * non-empty if this is leaf).
     */
    if (_name == "") {
      _name = path_comps[path_comps.size() - 1];
    }
    if (_is_multi) {
      // multi-value node
      NodeStatusT st = (_active_only ? ST_STATIC : _status);
      if (st == ST_STATIC || st == ST_DELETED || st == ST_ADDED) {
        bool get_active = (st != ST_ADDED);
        _cstore->cfgPathGetValuesDA(path_comps, _values, get_active, true);
        // ignore return value

        // all values have the same status
        for (size_t i = 0; i < _values.size(); i++) {
          _values_status.push_back(st);
        }
      } else {
        // values changed => need to do a diff between active and working
        // this follows the original perl logic
        vector<string> ovals; // active values
        vector<string> nvals; // working values
        _cstore->cfgPathGetValuesDA(path_comps, ovals, true);
        _cstore->cfgPathGetValuesDA(path_comps, nvals, false);
        Cstore::MapT<string, bool> nmap;
        for (size_t i = 0; i < nvals.size(); i++) {
          nmap[nvals[i]] = true;
        }
        Cstore::MapT<string, bool> omap;
        for (size_t i = 0; i < ovals.size(); i++) {
          omap[ovals[i]] = true;
          if (nmap.find(ovals[i]) == nmap.end()) {
            _values.push_back(ovals[i]);
            _values_status.push_back(ST_DELETED);
          }
        }
        for (size_t i = 0; i < nvals.size(); i++) {
          _values.push_back(nvals[i]);
          if (omap.find(nvals[i]) == omap.end()) {
            // new value not in working
            _values_status.push_back(ST_ADDED);
          } else if (i < ovals.size() && nvals[i] == ovals[i]) {
            // value also in working and in the same position
            _values_status.push_back(ST_STATIC);
          } else {
            // value position has changed
            _values_status.push_back(ST_CHANGED);
          }
        }
      }
    } else {
      // single-value node
      // if the node has been deleted, get the value from active too.
      bool get_active = (_active_only || _status == ST_DELETED);
      _cstore->cfgPathGetValueDA(path_comps, _value, get_active, true);
      // ignore return value
    }
    return;
  }

  // intermediate (typeless) or tag node
  Cstore::MapT<string, string> cmap;
  vector<string> cnodes;
  if (_active_only) {
    // only show active config
    _cstore->cfgPathGetChildNodesDA(path_comps, cnodes, true, true);
    for (size_t i = 0; i < cnodes.size(); i++) {
      cmap[cnodes[i]] = Cstore::C_NODE_STATUS_STATIC;
    }
  } else {
    // show config session
    _cstore->cfgPathGetChildNodesStatusDA(path_comps, cmap, cnodes);
  }
  if (cmap.empty()) {
    // empty subtree. finished.
    _is_empty = true;
    return;
  }

  for (size_t i = 0; i < cnodes.size(); i++) {
    path_comps.push_back(cnodes[i]);
    CstoreCfgNode *cn = new CstoreCfgNode(cstore, path_comps, _active_only,
                                          cnodes[i], _st_map[cmap[cnodes[i]]]);
    if (_is_tag && !_is_value) {
      // tag node
      cn->setTagName(_name);
      _tag_values.push_back(cn);
    } else {
      // intermediate node or tag value
      _child_nodes.push_back(cn);
    }
    path_comps.pop_back();
  }
}


////// public functions
void
CstoreCfgNode::show_as_root(bool show_default, bool hide_secret)
{
  if (_is_invalid) {
    printf("Specified configuration path is not valid\n");
    return;
  }
  if (_is_empty) {
    printf("Configuration under specified path is empty\n");
    return;
  }

  show(-1, show_default, hide_secret);
}


////// private functions
void
CstoreCfgNode::show(int level, bool show_def, bool hide_secret)
{
  if (_is_leaf) {
    // leaf node
    if (_is_multi) {
      // multi-value node
      print_comment(level);
      for (size_t i = 0; i < _values.size()
                         && i < _values_status.size(); i++) {
        print_indent(level, _values_status[i]);
        printf("%s ", _name.c_str());
        print_value_str(_values[i].c_str(), hide_secret);
        printf("\n");
      }
    } else {
      // handle "default" for single-value node
      if (show_def || !_is_default) {
        // single-value node
        print_comment(level);
        print_indent(level);
        printf("%s ", _name.c_str());
        print_value_str(_value.c_str(), hide_secret);
        printf("\n");
      }
    }
    return;
  }

  if (_is_tag) {
    // tag node
    for (size_t i = 0; i < _tag_values.size(); i++) {
      /* note: if the root is a tag node (level == -1), then need to make
       *       level 0 when calling tag values' show().
       */
      _tag_values[i]->show((level >= 0 ? level : 0), show_def, hide_secret);
    }
  } else {
    // intermediate node or tag value
    bool print_this = (level >= 0 && _name != "");
    if (print_this) {
      print_comment(level);
      print_indent(level);
      if (_is_value && _tag_name != "") {
        // at tag value and there is a tag node parent => print node name
        printf("%s ", _tag_name.c_str());
      }
      printf("%s {\n", _name.c_str());
    }
    for (size_t i = 0; i < _child_nodes.size(); i++) {
      _child_nodes[i]->show(level + 1, show_def, hide_secret);
    }
    if (print_this) {
      print_indent(level);
      printf("}\n");
    }
  }
}

void
CstoreCfgNode::print_indent(int level, NodeStatusT st, bool force_changed)
{
  if (st == ST_CHANGED && !force_changed && !_is_leaf) {
    /* normally only output "changed" status for leaf nodes. in special cases
     * (currently only for "comment"), "changed" is also needed, so only
     * convert to "static" if not forcing "changed".
     */
    st = ST_STATIC;
  }
  print_prefix(st);
  for (int i = 0; i < level; i++) {
    printf("    ");
  };
}

void
CstoreCfgNode::print_prefix(NodeStatusT st)
{
  printf("%s ", _pfx_deact);
  if (!_active_only) {
    /* this follows the original implementation: when outputting the acitve
     * configuration only (e.g., in op mode), only generate two columns.
     */
    printf("%s", get_prefix_diff(st));
  }
}

const char *
CstoreCfgNode::get_prefix_diff(NodeStatusT st)
{
  if (st == ST_DELETED) {
    return PFX_DIFF_DEL.c_str();
  } else if (st == ST_ADDED) {
    return PFX_DIFF_ADD.c_str();
  } else if (st == ST_CHANGED) {
    return PFX_DIFF_UPD.c_str();
  }
  return PFX_DIFF_NONE.c_str();
}

void
CstoreCfgNode::print_comment(int level)
{
  if (_comment == "") {
    // no comment
    return;
  }
  // forcing "changed" since it's needed for comment
  print_indent(level, _comment_status, true);
  printf("/* %s */\n", _comment.c_str());
}

/* prints a value string, double-quoting it if necessary.
 * this follows the original perl logic, i.e., double quoting is needed if:
 *   (/^$/ or /[\s\*}{;]/)
 *
 * also follow the original "secret hiding" logic:
 *   /^.*(passphrase|password|pre-shared-secret|key)$/
 */
void
CstoreCfgNode::print_value_str(const char *vstr, bool hide_secret)
{
  // handle secret hiding first
  if (hide_secret) {
    static const char *sname[] = { "passphrase", "password",
                                   "pre-shared-secret", "key", NULL };
    static size_t slen[] = { 10, 8, 17, 3, 0 };
    size_t nlen = _name.length();
    for (size_t i = 0; sname[i]; i++) {
      if (nlen < slen[i]) {
        // can't match
        continue;
      }
      if (_name.find(sname[i], nlen - slen[i]) != _name.npos) {
        // found secret
        printf("****************");
        return;
      }
    }
  }

  const char *quote = "";
  size_t vlen = strlen(vstr);
  if (*vstr == 0 || strcspn(vstr, "*}{;\011\012\013\014\015 ") < vlen) {
    quote = "\"";
  }
  printf("%s%s%s", quote, vstr, quote);
}

