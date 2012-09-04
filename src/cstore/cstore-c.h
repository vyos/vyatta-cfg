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

#ifndef _CSTORE_C_H_
#define _CSTORE_C_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <cli_cstore.h>

void *cstore_init(void);
void cstore_free(void *handle);
int cstore_validate_tmpl_path(void *handle, const char *path_comps[],
                              int num_comps, int validate_tags);
int cstore_cfg_path_exists(void *handle, const char *path_comps[],
                           int num_comps);
int cstore_cfg_path_exists_effective(void *handle, const char *path_comps[],
                                     int num_comps);
int cstore_cfg_path_deactivated(void *handle, const char *path_comps[],
                                int num_comps, int in_active);

char *cstore_cfg_path_get_effective_value(void *handle,
                                          const char *path_comps[],
                                          int num_comps);

int cstore_unmark_cfg_path_changed(void *handle, const char *path_comps[],
                                   int num_comps);

/* the following are internal APIs for the library. they can only be used
 * during cstore operations since they operate on "current" paths constructed
 * by the operations.
 */
int cstore_get_var_ref(void *handle, const char *ref_str, vtw_type_e *type,
                       char **val, int from_active);
int cstore_set_var_ref(void *handle, const char *ref_str, const char *value,
                       int to_active);

/* util functions */
char **cstore_path_string_to_path_comps(const char *path_str, int *num_comps);
void cstore_free_path_comps(char **path_comps, int num_comps);

#ifdef __cplusplus
}
#endif
#endif /* _CSTORE_C_H_ */

