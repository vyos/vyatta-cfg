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
#include <tr1/memory>

#include <cstore/cstore.hpp>
#include <cnode/cnode.hpp>
#include <cparse/cparse.hpp>
#include <cnode/cnode-algorithm.hpp>

using namespace cnode;


////// constants
static const string PFX_DIFF_ADD = "+"; // added
static const string PFX_DIFF_DEL = "-"; // deleted
static const string PFX_DIFF_UPD = ">"; // changed
static const string PFX_DIFF_NONE = " ";
static const string PFX_DIFF_NULL = "";

const string cnode::ACTIVE_CFG = "@ACTIVE";
const string cnode::WORKING_CFG = "@WORKING";

////// static (internal) functions
static void
_show_diff(const CfgNode *cfg1, const CfgNode *cfg2, int level,
           Cpath& cur_path, Cpath& last_ctx, bool show_def,
           bool hide_secret, bool context_diff);

static void
_get_cmds_diff(const CfgNode *cfg1, const CfgNode *cfg2,
               Cpath& cur_path, vector<Cpath>& del_list,
               vector<Cpath>& set_list, vector<Cpath>& com_list);

/* compare the values of a "multi" node in the two configs. the values and
 * the "prefix" of each value are returned in "values" and "pfxs",
 * respectively.
 *
 * return value indicates whether the node is different in the two configs.
 *
 * comparison follows the original perl logic.
 */
static bool
_cmp_multi_values(const CfgNode *cfg1, const CfgNode *cfg2,
                  vector<string>& values, vector<const char *>& pfxs)
{
  const vector<string>& ovec = cfg1->getValues();
  const vector<string>& nvec = cfg2->getValues();
  MapT<string, bool> nmap;
  bool changed = false;
  for (size_t i = 0; i < nvec.size(); i++) {
    nmap[nvec[i]] = true;
  }
  MapT<string, bool> omap;
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
_cmp_non_leaf_nodes(const CfgNode *cfg1, const CfgNode *cfg2,
                    vector<CfgNode *>& rcnodes1,
                    vector<CfgNode *>& rcnodes2, bool& not_tag_node,
                    bool& is_value, bool& is_leaf_typeless,
                    string& name, string& value)
{
  const CfgNode *cfg = (cfg1 ? cfg1 : cfg2);
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

  MapT<string, bool> map;
  MapT<string, CfgNode *> nmap1, nmap2;
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
  MapT<string, bool>::iterator it = map.begin();
  for (; it != map.end(); ++it) {
    cnodes.push_back((*it).first);
  }
  Cstore::sortNodes(cnodes);

  for (size_t i = 0; i < cnodes.size(); i++) {
    MapT<string, CfgNode *>::iterator p1 = nmap1.find(cnodes[i]);
    MapT<string, CfgNode *>::iterator p2 = nmap2.find(cnodes[i]);
    bool in1 = (p1 != nmap1.end());
    bool in2 = (p2 != nmap2.end());
    CfgNode *c1 = (in1 ? p1->second : NULL);
    CfgNode *c2 = (in2 ? p2->second : NULL);
    rcnodes1.push_back(c1);
    rcnodes2.push_back(c2);
  }
}

static void
_add_path_to_list(vector<Cpath>& list, Cpath& path, const string *nptr,
                  const string *vptr)
{
  if (nptr) {
    path.push(*nptr);
  }
  if (vptr) {
    path.push(*vptr);
  }
  list.push_back(path);
  if (vptr) {
    path.pop();
  }
  if (nptr) {
    path.pop();
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
_diff_print_indent(const CfgNode *cfg1, const CfgNode *cfg2, int level,
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

/* this is used by "context diff" to print the context in "edit notation"
 * like in JUNOS "show | compare".
 */
static void
_diff_print_context(Cpath& cur_path, Cpath& last_ctx)
{
  if (last_ctx == cur_path) {
    // don't repeat the context if it's still the same as the last one
    return;
  }
  last_ctx = cur_path;
  printf("[edit");
  for (size_t i = 0; i < cur_path.size(); i++) {
    printf(" %s", cur_path[i]);
  }
  printf("]\n");
}

/* print the comment (if any) at the specified node, including "change
 * prefix" (if any).
 *
 * since a node's comment is printed before the node itself, this will
 * print the "context" first if doing context diff.
 *
 *   cur_path: the current context (for context diff).
 *   context_diff: whether we're doing context diff.
 *
 * return whether anything is printed. this is only used for context diff
 * (caller does different things according to return value).
 */
static bool
_diff_print_comment(const CfgNode *cfg1, const CfgNode *cfg2, int level,
                    Cpath& cur_path, Cpath& last_ctx, bool context_diff)
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
    return false;
  }
  /* print comment if
   *   * not doing context diff
   *   OR
   *   * doing context diff and there is actually a difference
   */
  if (!context_diff || (pfx_diff != PFX_DIFF_NONE.c_str()
                        && pfx_diff != PFX_DIFF_NULL.c_str())) {
    if (context_diff) {
      // print context first
      _diff_print_context(cur_path, last_ctx);
    }
    _diff_print_indent(cfg1, cfg2, level, pfx_diff);
    printf("/* %s */\n", comment.c_str());
    return true;
  } else {
    return false;
  }
}

static bool
_diff_check_and_show_leaf(const CfgNode *cfg1, const CfgNode *cfg2, int level,
                          Cpath& cur_path, Cpath& last_ctx, bool show_def,
                          bool hide_secret, bool context_diff)
{
  if ((cfg1 && !cfg1->isLeaf()) || (cfg2 && !cfg2->isLeaf())) {
    // not a leaf node
    return false;
  }

  const CfgNode *cfg = NULL;
  const char *force_pfx_diff = NULL;
  bool is_default = (cfg2 ? cfg2->isDefault() : cfg1->isDefault());
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

  bool cprint = _diff_print_comment(cfg1, cfg2, level, cur_path, last_ctx,
                                    context_diff);
  if (cprint) {
    /* when doing context diff, normally we only show the node if there is a
     * difference. however, if something was printed for comment, the node
     * should be printed even if there is no difference. so set context_diff
     * to false to force the node to be displayed.
     */
    context_diff = false;
  }
  if (cfg->isMulti()) {
    // multi-value node
    if (force_pfx_diff) {
      // simple case: just use the same diff prefix for all values
      if (!cprint && context_diff) {
        /* if nothing was printed for comment and we're doing context diff,
         * then context hasn't been displayed yet. so print it first.
         */
        _diff_print_context(cur_path, last_ctx);
      }
      if (!context_diff || force_pfx_diff != PFX_DIFF_NULL.c_str()) {
        // not context diff OR there is a difference => print the node
        const vector<string>& vvec = cfg->getValues();
        for (size_t i = 0; i < vvec.size(); i++) {
          _diff_print_indent(cfg1, cfg2, level, force_pfx_diff);
          printf("%s ", cfg->getName().c_str());
          _print_value_str(cfg->getName(), vvec[i].c_str(), hide_secret);
          printf("\n");
        }
      }
    } else {
      // need to actually do a diff.
      vector<string> values;
      vector<const char *> pfxs;
      bool changed = _cmp_multi_values(cfg1, cfg2, values, pfxs);
      if (!context_diff || changed) {
        // not context diff OR there is a difference => print the node
        for (size_t i = 0; i < values.size(); i++) {
          if (context_diff && pfxs[i] == PFX_DIFF_NONE.c_str()) {
            // not printing unchanged values if doing context diff
            continue;
          }
          if (!cprint && context_diff) {
            /* if nothing was printed for comment and we're doing context
             * diff, then context hasn't been displayed yet. so print it
             * first. note that we only want to see the context once, so
             * set cprint to true so that later iterations won't print it
             * again.
             */
            _diff_print_context(cur_path, last_ctx);
            cprint = true;
          }
          _diff_print_indent(cfg1, cfg2, level, pfxs[i]);
          printf("%s ", cfg->getName().c_str());
          _print_value_str(cfg->getName(), values[i].c_str(), hide_secret);
          printf("\n");
        }
      }
    }
  } else {
    // single-value node
    if (show_def || !is_default) {
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
      bool changed = (force_pfx_diff != PFX_DIFF_NONE.c_str()
                      && force_pfx_diff != PFX_DIFF_NULL.c_str());
      if (!context_diff || changed) {
        // not context diff OR there is a difference => print the node
        if (!cprint && context_diff) {
          /* if nothing was printed for comment and we're doing context diff,
           * then context hasn't been displayed yet. so print it first.
           */
          _diff_print_context(cur_path, last_ctx);
        }
        _diff_print_indent(cfg1, cfg2, level, force_pfx_diff);
        printf("%s ", cfg->getName().c_str());
        _print_value_str(cfg->getName(), val.c_str(), hide_secret);
        printf("\n");
      }
    }
  }

  return true;
}

static void 
_diff_show_other(const CfgNode *cfg1, const CfgNode *cfg2, int level,
                 Cpath& cur_path, Cpath& last_ctx, bool show_def,
                 bool hide_secret, bool context_diff)
{
  bool orig_cdiff = context_diff;
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
  int next_level = level + 1;
  if (print_this) {
    bool cprint = _diff_print_comment(cfg1, cfg2, level, cur_path, last_ctx,
                                      orig_cdiff);
    if (orig_cdiff && pfx_diff != PFX_DIFF_NONE.c_str()) {
      /* note:
       *   orig_cdiff is the original value of context_diff.
       *   pfx_diff is set above.
       * so the condition here means
       *   (1) when this function is called, we are doing a context diff
       *   AND
       *   (2) pfx_diff is either PFX_DIFF_ADD or PFX_DIFF_DEL, i.e., one
       *       and only one of cfg1 and cfg2 is NULL.
       *
       *       note that pfx_diff cannot be PFX_DIFF_NULL since
       *       (cfg1 == cfg2) cannot be true if context_diff is true (see
       *       caller's logic).
       *
       * since one and only one of cfg1 and cfg2 is NULL, it means that the
       * whole subtree rooted at the current node is "added" or "deleted".
       * therefore, set context_diff to false for the recursion into this
       * subtree so that it will be displayed normally.
       */
      context_diff = false;
    }
    if (cprint || !orig_cdiff || pfx_diff != PFX_DIFF_NONE.c_str()) {
      /* print this node if
       *   (1) something was printed for comment, in which case the node
       *       should be printed regardless of whether there is a difference.
       *   OR
       *   (2) not doing context diff.
       *   OR
       *   (3) there is a difference.
       */
      if (!cprint && orig_cdiff) {
        /* if nothing was printed for comment and we're doing context diff,
         * then context hasn't been displayed yet. so print it first.
         */
        _diff_print_context(cur_path, last_ctx);
      }
      _diff_print_indent(cfg1, cfg2, level, pfx_diff);
      if (is_value) {
        // at tag value
        printf("%s %s", name.c_str(), value.c_str());
      } else {
        // at intermediate node
        printf("%s", name.c_str());
      }
      if (cprint && orig_cdiff && pfx_diff == PFX_DIFF_NONE.c_str()) {
        /* the condition means:
         *   (1) something was printed for comment.
         *   AND
         *   (2) doing context diff.
         *   AND
         *   (3) there is no difference for the node itself.
         *
         * this means we are only printing the node for the comment. so just
         * print "{ ... }" to represent the subtree like JUNOS
         * "show | compare". any difference in the subtree will be handled
         * later by the recursion into it.
         *
         * in this case also set is_leaf_typeless to true to prevent a
         * dangling "}\n" from being printed at the end of this function.
         */
        printf(" { ... }\n");
        is_leaf_typeless = true;
      } else {
        printf("%s\n", (is_leaf_typeless ? "" : " {"));
      }
    }

    cur_path.push(name);
    if (is_value) {
      cur_path.push(value);
    }
  } else {
    next_level = (level >= 0 ? level : 0);
  }

  for (size_t i = 0; i < rcnodes1.size(); i++) {
    _show_diff(rcnodes1[i], rcnodes2[i], next_level, cur_path, last_ctx,
               show_def, hide_secret, context_diff);
  }

  // finish printing "this" node if necessary
  if (print_this) {
    cur_path.pop();
    if (is_value) {
      cur_path.pop();
    }
    if (!orig_cdiff || pfx_diff != PFX_DIFF_NONE.c_str()) {
      /* not context diff OR there is a difference => print closing '}'
       * if necessary. also note the exception above where is_leaf_typeless
       * is set to true to prevent this.
       */
      if (!is_leaf_typeless) {
        _diff_print_indent(cfg1, cfg2, level, pfx_diff);
        printf("}\n");
      }
    }
  }
}

static void
_show_diff(const CfgNode *cfg1, const CfgNode *cfg2, int level,
           Cpath& cur_path, Cpath& last_ctx, bool show_def,
           bool hide_secret, bool context_diff)
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

  if (context_diff) {
    if (cfg1 == cfg2) {
      /* nothing to do for context diff so stop the recursion. this also
       * means that during the recursion cfg1 and cfg2 must be different
       * if doing context diff.
       */
      return;
    }
    /* when doing context diff, the display indentation level always starts
     * at 0.
     */
    level = 0;
  }

  if (_diff_check_and_show_leaf(cfg1, cfg2, (level >= 0 ? level : 0),
                                cur_path, last_ctx, show_def, hide_secret,
                                context_diff)) {
    // leaf node has been shown. done.
    return;
  } else {
    // intermediate node, tag node, or tag value
    _diff_show_other(cfg1, cfg2, level, cur_path, last_ctx, show_def,
                     hide_secret, context_diff);
  }
}

static void
_get_comment_diff_cmd(const CfgNode *cfg1, const CfgNode *cfg2,
                      Cpath& cur_path, vector<Cpath>& com_list,
                      const string *val)
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
      cur_path.push(*name);
      name = val;
    }
    _add_path_to_list(com_list, cur_path, name, comment);
    if (val) {
      cur_path.pop();
    }
  }
}

static bool
_get_cmds_diff_leaf(const CfgNode *cfg1, const CfgNode *cfg2,
                    Cpath& cur_path, vector<Cpath>& del_list,
                    vector<Cpath>& set_list, vector<Cpath>& com_list)
{
  if ((cfg1 && !cfg1->isLeaf()) || (cfg2 && !cfg2->isLeaf())) {
    // not a leaf node
    return false;
  }

  const CfgNode *cfg = NULL;
  vector<Cpath> *list = NULL;
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
_get_cmds_diff_other(const CfgNode *cfg1, const CfgNode *cfg2,
                     Cpath& cur_path, vector<Cpath>& del_list,
                     vector<Cpath>& set_list, vector<Cpath>& com_list)
{
  vector<Cpath> *list = NULL;
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

    cur_path.push(name);
    if (is_value) {
      cur_path.push(value);
    }
  }
  for (size_t i = 0; i < rcnodes1.size(); i++) {
    _get_cmds_diff(rcnodes1[i], rcnodes2[i], cur_path, del_list, set_list,
                   com_list);
  }
  if (add_this) {
    if (is_value) {
      cur_path.pop();
    }
    cur_path.pop();
  }
}

static void
_get_cmds_diff(const CfgNode *cfg1, const CfgNode *cfg2,
               Cpath& cur_path, vector<Cpath>& del_list,
               vector<Cpath>& set_list, vector<Cpath>& com_list)
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
_print_cmds_list(const char *op, vector<Cpath>& list)
{
  for (size_t i = 0; i < list.size(); i++) {
    printf("%s", op);
    for (size_t j = 0; j < list[i].size(); j++) {
      printf(" '%s'", list[i][j]);
    }
    printf("\n");
  }
}

////// algorithms
void
cnode::show_cfg_diff(const CfgNode& cfg1, const CfgNode& cfg2,
                     Cpath& cur_path, bool show_def, bool hide_secret,
                     bool context_diff)
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
  // use an invalid value for initial last_ctx
  Cpath last_ctx;
  _show_diff(&cfg1, &cfg2, -1, cur_path, last_ctx, show_def, hide_secret,
             context_diff);
}

void
cnode::show_cfg(const CfgNode& cfg, bool show_def, bool hide_secret)
{
  Cpath cur_path;
  show_cfg_diff(cfg, cfg, cur_path, show_def, hide_secret);
}

void
cnode::show_cmds_diff(const CfgNode& cfg1, const CfgNode& cfg2)
{
  Cpath cur_path;
  vector<Cpath> del_list;
  vector<Cpath> set_list;
  vector<Cpath> com_list;
  _get_cmds_diff(&cfg1, &cfg2, cur_path, del_list, set_list, com_list);

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
                     vector<Cpath>& del_list, vector<Cpath>& set_list,
                     vector<Cpath>& com_list)
{
  Cpath cur_path;
  _get_cmds_diff(&cfg1, &cfg2, cur_path, del_list, set_list, com_list);
}

void
cnode::get_cmds(const CfgNode& cfg, vector<Cpath>& set_list,
                vector<Cpath>& com_list)
{
  Cpath cur_path;
  vector<Cpath> del_list;
  _get_cmds_diff(&cfg, &cfg, cur_path, del_list, set_list, com_list);
}

void
cnode::showConfig(const string& cfg1, const string& cfg2,
                  const Cpath& path, bool show_def, bool hide_secret,
                  bool context_diff, bool show_cmds, bool ignore_edit)
{
  tr1::shared_ptr<CfgNode> aroot, wroot, croot1, croot2;
  tr1::shared_ptr<Cstore> cstore;
  Cpath rpath(path);
  Cpath cur_path;

  if (!ignore_edit && (cfg1 == ACTIVE_CFG || cfg1 == WORKING_CFG)
      && (cfg2 == ACTIVE_CFG || cfg2 == WORKING_CFG)) {
    // active/working config only => use edit level and path
    cstore.reset(Cstore::createCstore(true));
    cstore->getEditLevel(cur_path);
  } else {
    // at least one config file => don't use edit level and path
    cstore.reset(Cstore::createCstore(false));
    rpath.clear();
  }
  if (cfg1 == ACTIVE_CFG || cfg2 == ACTIVE_CFG) {
    aroot.reset(new CfgNode(*cstore, rpath, true, true));
  }
  if (cfg1 == WORKING_CFG || cfg2 == WORKING_CFG) {
    // note: if there is no config session, this will abort
    wroot.reset(new CfgNode(*cstore, rpath, false, true));
  }

  if (cfg1 == ACTIVE_CFG) {
    croot1 = aroot;
  } else if (cfg1 == WORKING_CFG) {
    croot1 = wroot;
  } else {
    croot1.reset(cparse::parse_file(cfg1.c_str(), *cstore));
  }
  if (cfg2 == ACTIVE_CFG) {
    croot2 = aroot;
  } else if (cfg2 == WORKING_CFG) {
    croot2 = wroot;
  } else {
    croot2.reset(cparse::parse_file(cfg2.c_str(), *cstore));
  }
  if (!croot1.get() || !croot2.get()) {
    printf("Cannot parse specified config file(s)\n");
    return;
  }

  if (show_cmds) {
    show_cmds_diff(*croot1, *croot2);
  } else {
    show_cfg_diff(*croot1, *croot2, cur_path, show_def, hide_secret,
                  context_diff);
  }
}

