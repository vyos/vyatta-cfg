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
#include <cstdlib>
#include <cstring>

#include <cstore/cstore.hpp>
#include <cnode/cnode.hpp>
#include <cnode/cnode-algorithm.hpp>

using namespace std;
using namespace cnode;


////// constants
static const string PFX_DIFF_ADD = "+"; // added
static const string PFX_DIFF_DEL = "-"; // deleted
static const string PFX_DIFF_UPD = ">"; // changed
static const string PFX_DIFF_NONE = " ";
static const string PFX_DIFF_NULL = "";


////// static (internal) functions
static void
_show_diff(CfgNode *cfg1, CfgNode *cfg2, int level, bool show_def,
           bool hide_secret);

static void
_get_cmds_diff(CfgNode *cfg1, CfgNode *cfg2, vector<string>& cur_path,
               vector<vector<string> >& del_list,
               vector<vector<string> >& set_list,
               vector<vector<string> >& com_list);

/* compare the values of a "multi" node in the two configs. the values and
 * the "prefix" of each value are returned in "values" and "pfxs",
 * respectively.
 *
 * return value indicates whether the node is different in the two configs.
 *
 * comparison follows the original perl logic.
 */
static bool
_cmp_multi_values(CfgNode *cfg1, CfgNode *cfg2, vector<string>& values,
                  vector<const char *>& pfxs)
{
  const vector<string>& ovec = cfg1->getValues();
  const vector<string>& nvec = cfg2->getValues();
  Cstore::MapT<string, bool> nmap;
  bool changed = false;
  for (size_t i = 0; i < nvec.size(); i++) {
    nmap[nvec[i]] = true;
  }
  Cstore::MapT<string, bool> omap;
  for (size_t i = 0; i < ovec.size(); i++) {
    omap[ovec[i]] = true;
    if (nmap.find(ovec[i]) == nmap.end()) {
      values.push_back(ovec[i]);
      pfxs.push_back(PFX_DIFF_DEL.c_str());
      changed = true;
    }
  }

  for (size_t i = 0; i < nvec.size(); i++) {
    values.push_back(nvec[i]);
    if (omap.find(nvec[i]) == omap.end()) {
      pfxs.push_back(PFX_DIFF_ADD.c_str());
      changed = true;
    } else if (i < ovec.size() && nvec[i] == ovec[i]) {
      pfxs.push_back(PFX_DIFF_NONE.c_str());
    } else {
      pfxs.push_back(PFX_DIFF_UPD.c_str());
      changed = true;
    }
  }

  return changed;
}

static void
_cmp_non_leaf_nodes(CfgNode *cfg1, CfgNode *cfg2, vector<CfgNode *>& rcnodes1,
                    vector<CfgNode *>& rcnodes2, bool& not_tag_node,
                    bool& is_value, bool& is_leaf_typeless,
                    string& name, string& value)
{
  CfgNode *cfg = (cfg1 ? cfg1 : cfg2);
  is_value = cfg->isValue();
  not_tag_node = (!cfg->isTag() || is_value);
  is_leaf_typeless = cfg->isLeafTypeless();
  bool is_tag_node = !not_tag_node;
  name = cfg->getName();
  if (is_value) {
    value = cfg->getValue();
  }

  // handle child nodes
  vector<CfgNode *> cnodes1, cnodes2;
  if (cfg1) {
    cnodes1 = cfg1->getChildNodes();
  }
  if (cfg2) {
    cnodes2 = cfg2->getChildNodes();
  }

  Cstore::MapT<string, bool> map;
  Cstore::MapT<string, CfgNode *> nmap1, nmap2;
  for (size_t i = 0; i < cnodes1.size(); i++) {
    string key = (is_tag_node
                  ? cnodes1[i]->getValue() : cnodes1[i]->getName());
    map[key] = true;
    nmap1[key] = cnodes1[i];
  }
  for (size_t i = 0; i < cnodes2.size(); i++) {
    string key = (is_tag_node
                  ? cnodes2[i]->getValue() : cnodes2[i]->getName());
    map[key] = true;
    nmap2[key] = cnodes2[i];
  }

  vector<string> cnodes;
  Cstore::MapT<string, bool>::iterator it = map.begin();
  for (; it != map.end(); ++it) {
    cnodes.push_back((*it).first);
  }
  Cstore::sortNodes(cnodes);

  for (size_t i = 0; i < cnodes.size(); i++) {
    bool in1 = (nmap1.find(cnodes[i]) != nmap1.end());
    bool in2 = (nmap2.find(cnodes[i]) != nmap2.end());
    CfgNode *c1 = (in1 ? nmap1[cnodes[i]] : NULL);
    CfgNode *c2 = (in2 ? nmap2[cnodes[i]] : NULL);
    rcnodes1.push_back(c1);
    rcnodes2.push_back(c2);
  }
}

static void
_add_path_to_list(vector<vector<string> >& list, vector<string>& path,
                  const string *nptr, const string *vptr)
{
  if (nptr) {
    path.push_back(*nptr);
  }
  if (vptr) {
    path.push_back(*vptr);
  }
  list.push_back(path);
  if (vptr) {
    path.pop_back();
  }
  if (nptr) {
    path.pop_back();
  }
}

static void
_print_value_str(const string& name, const char *vstr, bool hide_secret)
{
  // handle secret hiding first
  if (hide_secret) {
    static const char *sname[] = { "passphrase", "password",
                                   "pre-shared-secret", "key", NULL };
    static size_t slen[] = { 10, 8, 17, 3, 0 };
    size_t nlen = name.length();
    for (size_t i = 0; sname[i]; i++) {
      if (nlen < slen[i]) {
        // can't match
        continue;
      }
      if (name.find(sname[i], nlen - slen[i]) != name.npos) {
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

static void
_diff_print_indent(CfgNode *cfg1, CfgNode *cfg2, int level,
                   const char *pfx_diff)
{
  /* note: activate/deactivate state output was handled here. pending
   *       redesign, the output notation will be changed to "per-subtree"
   *       marking, so the output will be handled with the rest of the node.
   */
  printf("%s", pfx_diff);
  for (int i = 0; i < level; i++) {
    printf("    ");
  }
}

static void
_diff_print_comment(CfgNode *cfg1, CfgNode *cfg2, int level)
{
  const char *pfx_diff = PFX_DIFF_NONE.c_str();
  string comment = "";
  if (cfg1 != cfg2) {
    string c1 = (cfg1 ? cfg1->getComment() : "");
    string c2 = (cfg2 ? cfg2->getComment() : "");
    if (c1 != "") {
      if (c2 != "") {
        // in both
        comment = c2;
        if (c1 != c2) {
          pfx_diff = PFX_DIFF_UPD.c_str();
        }
      } else {
        // only in cfg1
        comment = c1;
        pfx_diff = PFX_DIFF_DEL.c_str();
      }
    } else {
      if (c2 != "") {
        // only in cfg2
        comment = c2;
        pfx_diff = PFX_DIFF_ADD.c_str();
      }
      // 4th case handled by default
    }
  } else {
    // same node => no diff
    pfx_diff = PFX_DIFF_NULL.c_str();
    comment = cfg1->getComment();
  }
  if (comment == "") {
    // no comment
    return;
  }
  _diff_print_indent(cfg1, cfg2, level, pfx_diff);
  printf("/* %s */\n", comment.c_str());
}

static bool
_diff_check_and_show_leaf(CfgNode *cfg1, CfgNode *cfg2, int level,
                          bool show_def, bool hide_secret)
{
  if ((cfg1 && !cfg1->isLeaf()) || (cfg2 && !cfg2->isLeaf())) {
    // not a leaf node
    return false;
  }

  CfgNode *cfg = NULL;
  const char *force_pfx_diff = NULL;
  if (!cfg1) {
    cfg = cfg2;
    force_pfx_diff = PFX_DIFF_ADD.c_str();
  } else {
    cfg = cfg1;
    if (!cfg2) {
      force_pfx_diff = PFX_DIFF_DEL.c_str();
    } else if (cfg1 == cfg2) {
      force_pfx_diff = PFX_DIFF_NULL.c_str();
    }
  }

  _diff_print_comment(cfg1, cfg2, level);
  if (cfg->isMulti()) {
    // multi-value node
    if (force_pfx_diff) {
      // simple case: just use the same diff prefix for all values
      const vector<string>& vvec = cfg->getValues();
      for (size_t i = 0; i < vvec.size(); i++) {
        _diff_print_indent(cfg1, cfg2, level, force_pfx_diff);
        printf("%s ", cfg->getName().c_str());
        _print_value_str(cfg->getName(), vvec[i].c_str(), hide_secret);
        printf("\n");
      }
    } else {
      // need to actually do a diff.
      vector<string> values;
      vector<const char *> pfxs;
      _cmp_multi_values(cfg1, cfg2, values, pfxs);
      for (size_t i = 0; i < values.size(); i++) {
        _diff_print_indent(cfg1, cfg2, level, pfxs[i]);
        printf("%s ", cfg->getName().c_str());
        _print_value_str(cfg->getName(), values[i].c_str(), hide_secret);
        printf("\n");
      }
    }
  } else {
    // single-value node
    if (show_def || !cfg->isDefault()) {
      string val = cfg->getValue();
      if (!force_pfx_diff) {
        const string& val1 = cfg1->getValue();
        val = cfg2->getValue();
        if (val == val1) {
          force_pfx_diff = PFX_DIFF_NONE.c_str();
        } else {
          force_pfx_diff = PFX_DIFF_UPD.c_str();
        }
      }
      _diff_print_indent(cfg1, cfg2, level, force_pfx_diff);
      printf("%s ", cfg->getName().c_str());
      _print_value_str(cfg->getName(), val.c_str(), hide_secret);
      printf("\n");
    }
  }

  return true;
}

static void 
_diff_show_other(CfgNode *cfg1, CfgNode *cfg2, int level, bool show_def,
                 bool hide_secret)
{
  const char *pfx_diff = PFX_DIFF_NONE.c_str();
  if (!cfg1) {
    pfx_diff = PFX_DIFF_ADD.c_str();
  } else {
    if (!cfg2) {
      pfx_diff = PFX_DIFF_DEL.c_str();
    } else if (cfg1 == cfg2) {
      pfx_diff = PFX_DIFF_NULL.c_str();
    }
  }

  string name, value;
  bool not_tag_node, is_value, is_leaf_typeless;
  vector<CfgNode *> rcnodes1, rcnodes2;
  _cmp_non_leaf_nodes(cfg1, cfg2, rcnodes1, rcnodes2, not_tag_node, is_value,
                      is_leaf_typeless, name, value);

  /* only print "this" node if it
   *   (1) is a tag value or an intermediate node,
   *   (2) is not "root", and
   *   (3) has a "name".
   */
  bool print_this = (not_tag_node && level >= 0 && name.size() > 0);
  if (print_this) {
    _diff_print_comment(cfg1, cfg2, level);
    _diff_print_indent(cfg1, cfg2, level, pfx_diff);
    if (is_value) {
      // at tag value
      printf("%s %s", name.c_str(), value.c_str());
    } else {
      // at intermediate node
      printf("%s", name.c_str());
    }
    printf("%s\n", (is_leaf_typeless ? "" : " {"));
  }

  for (size_t i = 0; i < rcnodes1.size(); i++) {
    int next_level = level + 1;
    if (!print_this) {
      next_level = (level >= 0 ? level : 0);
    }
    _show_diff(rcnodes1[i], rcnodes2[i], next_level, show_def, hide_secret);
  }

  // finish printing "this" node if necessary
  if (print_this && !is_leaf_typeless) {
    _diff_print_indent(cfg1, cfg2, level, pfx_diff);
    printf("}\n");
  }
}

static void
_show_diff(CfgNode *cfg1, CfgNode *cfg2, int level, bool show_def,
           bool hide_secret)
{
  // if doesn't exist, treat as NULL
  if (cfg1 && !cfg1->exists()) {
    cfg1 = NULL;
  }
  if (cfg2 && !cfg2->exists()) {
    cfg2 = NULL;
  }

  /* cfg1 and cfg2 point to the same config node in two configs. a "diff"
   * output is shown comparing the two configs recursively with this node
   * as the root of the config tree.
   *
   * there are four possible scenarios:
   *   (1) (cfg1 && cfg2) && (cfg1 != cfg2): node exists in both config.
   *   (2) (cfg1 && cfg2) && (cfg1 == cfg2): the two point to the same config.
   *       this will be just a "show".
   *   (3) (!cfg1 && cfg2): node exists in cfg2 but not in cfg1 (added).
   *   (4) (cfg1 && !cfg2): node exists in cfg1 but not in cfg1 (deleted).
   *
   * calling this with both NULL is invalid.
   */
  if (!cfg1 && !cfg2) {
    fprintf(stderr, "_show_diff error (both config NULL)\n");
    exit(1);
  }

  if (_diff_check_and_show_leaf(cfg1, cfg2, (level >= 0 ? level : 0),
                                show_def, hide_secret)) {
    // leaf node has been shown. done.
    return;
  } else {
    // intermediate node, tag node, or tag value
    _diff_show_other(cfg1, cfg2, level, show_def, hide_secret);
  }
}

static void
_get_comment_diff_cmd(CfgNode *cfg1, CfgNode *cfg2, vector<string>& cur_path,
                      vector<vector<string> >& com_list, const string *val)
{
  const string *comment = NULL;
  const string *name = NULL;
  string empty = "";
  string c1, c2;
  if (cfg1 != cfg2) {
    c1 = (cfg1 ? cfg1->getComment() : "");
    c2 = (cfg2 ? cfg2->getComment() : "");
    if (c1 != "") {
      name = &(cfg1->getName());
      if (c2 != "") {
        // in both
        if (c1 != c2) {
          // updated
          comment = &c2;
        }
      } else {
        // only in cfg1 => deleted
        comment = &empty;
      }
    } else {
      if (c2 != "") {
        // only in cfg2 => added
        name = &(cfg2->getName());
        comment = &c2;
      }
    }
  } else {
    // cfg1 == cfg2 => just getting all commands
    c1 = cfg1->getComment();
    if (c1 != "") {
      name = &(cfg1->getName());
      comment = &c1;
    }
  }
  if (comment) {
    if (val) {
      cur_path.push_back(*name);
      name = val;
    }
    _add_path_to_list(com_list, cur_path, name, comment);
    if (val) {
      cur_path.pop_back();
    }
  }
}

static bool
_get_cmds_diff_leaf(CfgNode *cfg1, CfgNode *cfg2, vector<string>& cur_path,
                    vector<vector<string> >& del_list,
                    vector<vector<string> >& set_list,
                    vector<vector<string> >& com_list)
{
  if ((cfg1 && !cfg1->isLeaf()) || (cfg2 && !cfg2->isLeaf())) {
    // not a leaf node
    return false;
  }

  CfgNode *cfg = NULL;
  vector<vector<string> > *list = NULL;
  if (cfg1) {
    cfg = cfg1;
    if (!cfg2) {
      // exists in cfg1 but not in cfg2 => delete and stop recursion
      _add_path_to_list(del_list, cur_path, &(cfg1->getName()), NULL);
      return true;
    } else if (cfg1 == cfg2) {
      // same config => just translating config to set commands
      list = &set_list;
    }
  } else {
    // !cfg1 => cfg2 must not be NULL
    cfg = cfg2;
    list = &set_list;
  }

  _get_comment_diff_cmd(cfg1, cfg2, cur_path, com_list, NULL);
  if (cfg->isMulti()) {
    // multi-value node
    if (list) {
      const vector<string>& vvec = cfg->getValues();
      for (size_t i = 0; i < vvec.size(); i++) {
        _add_path_to_list(*list, cur_path, &(cfg->getName()), &(vvec[i]));
      }
    } else {
      // need to actually do a diff.
      vector<string> dummy_vals;
      vector<const char *> dummy_pfxs;
      if (_cmp_multi_values(cfg1, cfg2, dummy_vals, dummy_pfxs)) {
        /* something changed. to get the correct ordering for multi-node
         * values, need to delete the node and then set the new values.
         */
        const vector<string>& nvec = cfg2->getValues();
        _add_path_to_list(del_list, cur_path, &(cfg->getName()), NULL);
        for (size_t i = 0; i < nvec.size(); i++) {
          _add_path_to_list(set_list, cur_path, &(cfg->getName()), &(nvec[i]));
        }
      }
    }
  } else {
    // single-value node
    string val = cfg->getValue();
    if (!list) {
      const string& val1 = cfg1->getValue();
      val = cfg2->getValue();
      if (val != val1) {
        // changed => need to set it
        list = &set_list;
      }
    }
    if (list) {
      _add_path_to_list(*list, cur_path, &(cfg->getName()), &val);
    }
  }

  return true;
}

static void
_get_cmds_diff_other(CfgNode *cfg1, CfgNode *cfg2, vector<string>& cur_path,
                     vector<vector<string> >& del_list,
                     vector<vector<string> >& set_list,
                     vector<vector<string> >& com_list)
{
  vector<vector<string> > *list = NULL;
  if (cfg1) {
    if (!cfg2) {
      // exists in cfg1 but not in cfg2 => delete and stop recursion
      _add_path_to_list(del_list, cur_path, &(cfg1->getName()),
                        (cfg1->isValue() ? &(cfg1->getValue()) : NULL));
      return;
    } else if (cfg1 == cfg2) {
      // same config => just translating config to set commands
      list = &set_list;
    }
  } else {
    // !cfg1 => cfg2 must not be NULL
    list = &set_list;
  }

  string name, value;
  bool not_tag_node, is_value, is_leaf_typeless;
  vector<CfgNode *> rcnodes1, rcnodes2;
  _cmp_non_leaf_nodes(cfg1, cfg2, rcnodes1, rcnodes2, not_tag_node, is_value,
                      is_leaf_typeless, name, value);
  if (rcnodes1.size() < 1 && list) {
    // subtree is empty
    _add_path_to_list(*list, cur_path, &name, (is_value ? &value : NULL));
    return;
  }

  bool add_this = (not_tag_node && name.size() > 0);
  if (add_this) {
    const string *val = (is_value ? &value : NULL);
    _get_comment_diff_cmd(cfg1, cfg2, cur_path, com_list, val);

    cur_path.push_back(name);
    if (is_value) {
      cur_path.push_back(value);
    }
  }
  for (size_t i = 0; i < rcnodes1.size(); i++) {
    _get_cmds_diff(rcnodes1[i], rcnodes2[i], cur_path, del_list, set_list,
                   com_list);
  }
  if (add_this) {
    if (is_value) {
      cur_path.pop_back();
    }
    cur_path.pop_back();
  }
}

static void
_get_cmds_diff(CfgNode *cfg1, CfgNode *cfg2, vector<string>& cur_path,
               vector<vector<string> >& del_list,
               vector<vector<string> >& set_list,
               vector<vector<string> >& com_list)
{
  // if doesn't exist, treat as NULL
  if (cfg1 && !cfg1->exists()) {
    cfg1 = NULL;
  }
  if (cfg2 && !cfg2->exists()) {
    cfg2 = NULL;
  }

  if (!cfg1 && !cfg2) {
    fprintf(stderr, "_get_cmds_diff error (both config NULL)\n");
    exit(1);
  }

  if (_get_cmds_diff_leaf(cfg1, cfg2, cur_path, del_list, set_list,
                          com_list)) {
    // leaf node has been shown. done.
    return;
  } else {
    // intermediate node, tag node, or tag value
    _get_cmds_diff_other(cfg1, cfg2, cur_path, del_list, set_list, com_list);
  }
}

static void
_print_cmds_list(const char *op, vector<vector<string> >& list)
{
  for (size_t i = 0; i < list.size(); i++) {
    printf("%s", op);
    for (size_t j = 0; j < list[i].size(); j++) {
      printf(" '%s'", list[i][j].c_str());
    }
    printf("\n");
  }
}

////// algorithms
void
cnode::show_cfg_diff(const CfgNode& cfg1, const CfgNode& cfg2, bool show_def,
                     bool hide_secret)
{
  if (cfg1.isInvalid() || cfg2.isInvalid()) {
    printf("Specified configuration path is not valid\n");
    return;
  }
  if ((cfg1.isEmpty() && cfg2.isEmpty())
      || (!cfg1.exists() && !cfg2.exists())) {
    printf("Configuration under specified path is empty\n");
    return;
  }
  _show_diff(const_cast<CfgNode *>(&cfg1), const_cast<CfgNode *>(&cfg2), -1,
             show_def, hide_secret);
}

void
cnode::show_cfg(const CfgNode& cfg, bool show_def, bool hide_secret)
{
  show_cfg_diff(cfg, cfg, show_def, hide_secret);
}

void
cnode::show_cmds_diff(const CfgNode& cfg1, const CfgNode& cfg2)
{
  vector<string> cur_path;
  vector<vector<string> > del_list;
  vector<vector<string> > set_list;
  vector<vector<string> > com_list;
  _get_cmds_diff(const_cast<CfgNode *>(&cfg1), const_cast<CfgNode *>(&cfg2),
                 cur_path, del_list, set_list, com_list);

  _print_cmds_list("delete", del_list);
  _print_cmds_list("set", set_list);
  _print_cmds_list("comment", com_list);
}

void
cnode::show_cmds(const CfgNode& cfg)
{
  show_cmds_diff(cfg, cfg);
}

void
cnode::get_cmds_diff(const CfgNode& cfg1, const CfgNode& cfg2,
                     vector<vector<string> >& del_list,
                     vector<vector<string> >& set_list,
                     vector<vector<string> >& com_list)
{
  vector<string> cur_path;
  _get_cmds_diff(const_cast<CfgNode *>(&cfg1), const_cast<CfgNode *>(&cfg2),
                 cur_path, del_list, set_list, com_list);
}

void
cnode::get_cmds(const CfgNode& cfg, vector<vector<string> >& set_list,
                vector<vector<string> >& com_list)
{
  vector<string> cur_path;
  vector<vector<string> > del_list;
  _get_cmds_diff(const_cast<CfgNode *>(&cfg), const_cast<CfgNode *>(&cfg),
                 cur_path, del_list, set_list, com_list);
}

