/*
 * string_pool.h - string interning
 *
 * Copyright (C) 2026  Widianto Nur F <xnaltasee@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef STRING_POOL_H
#define STRING_POOL_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t string_ref;

void string_pool_setup(void);
void string_pool_cleanup(void);
uint32_t string_pool_count(void);
string_ref string_ref_new(const char *);
string_ref string_ref_newlen(const char *, unsigned int);
const char *string_ref_ptr(string_ref);
size_t string_ref_len(string_ref);
uint64_t string_ref_hash(string_ref);

#endif
