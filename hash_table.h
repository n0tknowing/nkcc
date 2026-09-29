/*
 * hash_table.h - linear probing hash table
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

#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include "string_pool.h"

typedef struct {
    string_ref key;
    void *val; // hash table don't own the value
    uint64_t hash;
} ht_entry_t;

typedef struct {
    ht_entry_t *entries;
    unsigned int _count;
    unsigned int count;
    unsigned int capacity;
    unsigned int load_factor;
} ht_t;

void hash_table_setup(ht_t *, unsigned int);
void hash_table_clear(ht_t *);
void hash_table_cleanup(ht_t *);
// since value is not owned by the hash table, this function exists
// to help freeing allocated value by user
void hash_table_cleanup_with_free(ht_t *, void (*free_func)(void *));
// insert or replace old val, NULL is returned if old val is not replaced
// otherwise old val is returned
void *hash_table_insert(ht_t *, string_ref, void *);
// associated val is returned
void *hash_table_remove(ht_t *, string_ref);
void *hash_table_lookup(ht_t *, string_ref);

#endif
