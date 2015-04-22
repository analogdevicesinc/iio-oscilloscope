/*
 * libini - Library to read INI configuration files
 *
 * Copyright (C) 2014 Paul Cercueil <paul@crapouillou.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#ifndef __INI_H
#define __INI_H

#include <stdlib.h>

struct INI;

struct INI *ini_open(const char *file);
struct INI *ini_open_mem(const char *buf, size_t len);

void ini_close(struct INI *ini);

/* Jump to the next section.
 * if 'name' is set, the pointer passed as argument
 * points to the name of the section. 'name_len' is set to the length
 * of the char array.
 * XXX: the pointer will be invalid as soon as ini_close() is called.
 *
 * Returns:
 * 	-EIO if an error occured while reading the file,
 * 	0 if no more section can be found,
 * 	1 otherwise.
 */
int ini_next_section(struct INI *ini, const char **name, size_t *name_len);

/* Read a key/value pair.
 * 'key' and 'value' must be valid pointers. The pointers passed as arguments
 * will point to the key and value read. 'key_len' and 'value_len' are
 * set to the length of their respective char arrays.
 * XXX: the pointers will be invalid as soon as ini_close() is called.
 *
 * Returns:
 *  -EIO if an error occured while reading the file,
 *  0 if no more key/value pairs can be found,
 *  1 otherwise.
 */
int ini_read_pair(struct INI *ini,
			const char **key, size_t *key_len,
			const char **value, size_t *value_len);

/* Set the read head to a specified offset. */
void ini_set_read_pointer(struct INI *ini, const char *pointer);

/* Get the number of the line that contains the specified address.
 *
 * Returns:
 * -EINVAL if the pointer points outside the INI string,
 *  The line number otherwise.
 */
int ini_get_line_number(struct INI *ini, const char *pointer);

#endif /* __INI_H */
