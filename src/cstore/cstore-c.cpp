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

#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>

#include <cstore/cstore.hpp>
#include <cstore/cstore-c.h>

using namespace cstore;

static void
_get_str_vec(vector<string>& vec, const char *strs[], int num_strs)
{
  for (int i = 0; i < num_strs; i++) {
    vec.push_back(strs[i]);
  }
}

void *
cstore_init(void)
{
  Cstore *handle = Cstore::createCstore(false);
  return (void *) handle;
}

void
cstore_free(void *handle)
{
  Cstore *h = (Cstore *) handle;
  delete h;
}

int
cstore_validate_tmpl_path(void *handle, const char *path_comps[],
                          int num_comps, int validate_tags)
{
  if (handle) {
    vector<string> vs;
    _get_str_vec(vs, path_comps, num_comps);
    Cstore *cs = (Cstore *) handle;
    return (cs->validateTmplPath(vs, validate_tags) ? 1 : 0);
  }
  return 0;
}

int
cstore_validate_tmpl_path_d(void *handle, const char *path_comps[],
                            int num_comps, int validate_tags, vtw_def *def)
{
  if (handle) {
    vector<string> vs;
    _get_str_vec(vs, path_comps, num_comps);
    Cstore *cs = (Cstore *) handle;
    return (cs->validateTmplPath(vs, validate_tags, *def) ? 1 : 0);
  }
  return 0;
}

int
cstore_cfg_path_exists(void *handle, const char *path_comps[], int num_comps)
{
  if (handle) {
    vector<string> vs;
    _get_str_vec(vs, path_comps, num_comps);
    Cstore *cs = (Cstore *) handle;
    return (cs->cfgPathExists(vs) ? 1 : 0);
  }
  return 0;
}

int
cstore_get_var_ref(void *handle, const char *ref_str, vtw_type_e *type,
                   char **val, int from_active)
{
  if (handle) {
    Cstore *cs = (Cstore *) handle;
    *val = cs->getVarRef(ref_str, *type, from_active);
    return (*val ? 1 : 0);
  }
  return 0;
}

int
cstore_set_var_ref(void *handle, const char *ref_str, const char *value,
                   int to_active)
{
  if (handle) {
    Cstore *cs = (Cstore *) handle;
    return (cs->setVarRef(ref_str, value, to_active) ? 1 : 0);
  }
  return 0;
}

int
cstore_cfg_path_deactivated(void *handle, const char *path_comps[],
                            int num_comps, int in_active)
{
  if (handle) {
    vector<string> vs;
    _get_str_vec(vs, path_comps, num_comps);
    Cstore *cs = (Cstore *) handle;
    return (cs->cfgPathDeactivated(vs, in_active) ? 1 : 0);
  }
  return 0;
}

char *
cstore_cfg_path_get_effective_value(void *handle, const char *path_comps[],
                                    int num_comps)
{
  if (handle) {
    vector<string> vs;
    _get_str_vec(vs, path_comps, num_comps);
    Cstore *cs = (Cstore *) handle;
    string val;
    if (!cs->cfgPathGetEffectiveValue(vs, val)) {
      return NULL;
    }

    int vsize = val.length();
    char *buf = (char *) malloc(vsize + 1);
    if (!buf) {
      return NULL;
    }
    strncpy(buf, val.c_str(), vsize);
    buf[vsize] = 0;

    return buf;
  }
  return NULL;
}

int
cstore_unmark_cfg_path_changed(void *handle, const char *path_comps[],
                               int num_comps)
{
  if (handle) {
    vector<string> vs;
    _get_str_vec(vs, path_comps, num_comps);
    Cstore *cs = (Cstore *) handle;
    return (cs->unmarkCfgPathChanged(vs) ? 1 : 0);
  }
  return 0;
}

char **
cstore_path_string_to_path_comps(const char *path_str, int *num_comps)
{
  char *pstr = strdup(path_str);
  size_t len = strlen(pstr);
  vector<string> vec;
  char *start = NULL;
  for (size_t i = 0; i < len; i++) {
    if (pstr[i] == '/') {
      if (start) {
        pstr[i] = 0;
        vec.push_back(start);
        pstr[i] = '/';
        start = NULL;
      }
      continue;
    } else if (!start) {
      start = &(pstr[i]);
    }
  }
  if (start) {
    vec.push_back(start);
  }
  char **ret = (char **) malloc(sizeof(char *) * vec.size());
  if (ret) {
    for (size_t i = 0; i < vec.size(); i++) {
      ret[i] = strdup(vec[i].c_str());
    }
    *num_comps = vec.size();
  }
  free(pstr);
  return ret;
}

void
cstore_free_path_comps(char **path_comps, int num_comps)
{
  for (int i = 0; i < num_comps; i++) {
    free(path_comps[i]);
  }
  free(path_comps);
}

