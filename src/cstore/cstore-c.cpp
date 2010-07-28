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
#include <vector>
#include <string>

#include "cstore-c.h"
#include "cstore/unionfs/cstore-unionfs.hpp"

void *
cstore_init(void)
{
  Cstore *handle = new UnionfsCstore();
  return (void *) handle;
}

void
cstore_free(void *handle)
{
  UnionfsCstore *h = (UnionfsCstore *) handle;
  delete h;
}

int
cstore_validate_tmpl_path(void *handle, const char *path_comps[],
                          int num_comps, int validate_tags)
{
  if (handle) {
    vector<string> vs;
    for (int i = 0; i < num_comps; i++) {
      vs.push_back(path_comps[i]);
    }
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
    for (int i = 0; i < num_comps; i++) {
      vs.push_back(path_comps[i]);
    }
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
    for (int i = 0; i < num_comps; i++) {
      vs.push_back(path_comps[i]);
    }
    Cstore *cs = (Cstore *) handle;
    return (cs->cfgPathExists(vs) ? 1 : 0);
  }
  return 0;
}

int
cstore_get_var_ref(void *handle, const char *ref_str, clind_val *cval,
                   int from_active)
{
  if (handle) {
    Cstore *cs = (Cstore *) handle;
    return (cs->getVarRef(ref_str, *cval, from_active) ? 1 : 0);
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
    for (int i = 0; i < num_comps; i++) {
      vs.push_back(path_comps[i]);
    }
    Cstore *cs = (Cstore *) handle;
    return (cs->cfgPathDeactivated(vs, in_active) ? 1 : 0);
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
  for (unsigned int i = 0; i < len; i++) {
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
  for (unsigned int i = 0; i < vec.size(); i++) {
    ret[i] = strdup(vec[i].c_str());
  }
  *num_comps = vec.size();
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

