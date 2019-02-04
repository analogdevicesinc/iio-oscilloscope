
#ifndef _COMPAT_H_
#define _COMPAT_H_

#include <dirent.h>

static inline bool is_dirent_reqular_file(const struct dirent *ent)
{
#ifdef _DIRENT_HAVE_D_TYPE
	/* assume regular file; though not very correct */
	if (!ent)
		return true;
	if (ent->d_type == DT_REG)
		return true;
	/* assume regular file if d_type is 0 */
	if (ent->d_type == 0)
		return true;
	return false;
#else
	/* assume regular file */
	return true;
#endif
}

#endif
