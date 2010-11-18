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

static const string PFX_DEACT_D = "!";  // deactivated
static const string PFX_DEACT_DP = "D"; // deactivate pending
static const string PFX_DEACT_AP = "A"; // activate pending
static const string PFX_DEACT_NONE = " ";


////// static (internal) functions
static void
_show_diff(CfgNode *cfg1, CfgNode *cfg2, int level, bool show_def,
           bool hide_secret);

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
  const char *pfx_deact = PFX_DEACT_NONE.c_str();
  bool de1 = (cfg1 ? cfg1->isDeactivated() : false);
  bool de2 = (cfg2 ? cfg2->isDeactivated() : false);
  if (de1) {
    if (de2) {
      pfx_deact = PFX_DEACT_D.c_str();
    } else {
      pfx_deact = PFX_DEACT_AP.c_str();
    }
  } else {
    if (de2) {
      pfx_deact = PFX_DEACT_DP.c_str();
    }
    // 4th case handled by default
  }

  printf("%s %s", pfx_deact, pfx_diff);
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
      // this follows the original perl logic.
      const vector<string>& ovec = cfg1->getValues();
      const vector<string>& nvec = cfg1->getValues();
      vector<string> values;
      vector<const char *> pfxs;
      Cstore::MapT<string, bool> nmap;
      for (size_t i = 0; i < nvec.size(); i++) {
        nmap[nvec[i]] = true;
      }
      Cstore::MapT<string, bool> omap;
      for (size_t i = 0; i < ovec.size(); i++) {
        omap[ovec[i]] = true;
        if (nmap.find(ovec[i]) == nmap.end()) {
          values.push_back(ovec[i]);
          pfxs.push_back(PFX_DIFF_DEL.c_str());
        }
      }

      for (size_t i = 0; i < nvec.size(); i++) {
        values.push_back(nvec[i]);
        if (omap.find(nvec[i]) == omap.end()) {
          pfxs.push_back(PFX_DIFF_ADD.c_str());
        } else if (i < ovec.size() && nvec[i] == ovec[i]) {
          pfxs.push_back(PFX_DIFF_NONE.c_str());
        } else {
          pfxs.push_back(PFX_DIFF_UPD.c_str());
        }
      }

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
      const string& val = cfg->getValue();
      if (!force_pfx_diff) {
        const string& val1 = cfg1->getValue();
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
  CfgNode *cfg = NULL;
  const char *pfx_diff = PFX_DIFF_NONE.c_str();
  if (!cfg1) {
    cfg = cfg2;
    pfx_diff = PFX_DIFF_ADD.c_str();
  } else {
    cfg = cfg1;
    if (!cfg2) {
      pfx_diff = PFX_DIFF_DEL.c_str();
    } else if (cfg1 == cfg2) {
      pfx_diff = PFX_DIFF_NULL.c_str();
    }
  }

  /* only print "this" node if it
   *   (1) is a tag value or an intermediate node,
   *   (2) is not "root", and
   *   (3) has a "name".
   */
  const string& name = cfg->getName();
  bool print_this = (((cfg1 && (!cfg1->isTag() || cfg1->isValue()))
                      || (cfg2 && (!cfg2->isTag() || cfg2->isValue())))
                     && level >= 0 && name.size() > 0);
  if (print_this) {
    _diff_print_comment(cfg1, cfg2, level);
    _diff_print_indent(cfg1, cfg2, level, pfx_diff);
    if (cfg->isValue()) {
      // at tag value
      printf("%s %s", name.c_str(), cfg->getValue().c_str());
    } else {
      // at intermediate node
      printf("%s", name.c_str());
    }
    printf("%s\n", (cfg->isLeafTypeless() ? "" : " {"));
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
    string key
      = ((cfg->isTag() && !cfg->isValue())
         ? cnodes1[i]->getValue() : cnodes1[i]->getName());
    map[key] = true;
    nmap1[key] = cnodes1[i];
  }
  for (size_t i = 0; i < cnodes2.size(); i++) {
    string key
      = ((cfg->isTag() && !cfg->isValue())
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

    int next_level = level + 1;
    if (!print_this) {
      next_level = (level >= 0 ? level : 0);
    }
    _show_diff(c1, c2, next_level, show_def, hide_secret);
  }

  // finish printing "this" node if necessary
  if (print_this && !cfg->isLeafTypeless()) {
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


////// algorithms
void
cnode::show_diff(const CfgNode& cfg1, const CfgNode& cfg2, bool show_def,
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

