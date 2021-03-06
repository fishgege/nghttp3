/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef NGHTTP3_TNODE_H
#define NGHTTP3_TNODE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <nghttp3/nghttp3.h>

#include "nghttp3_pq.h"

#define NGHTTP3_DEFAULT_WEIGHT 16
#define NGHTTP3_MAX_WEIGHT 256
#define NGHTTP3_TNODE_MAX_CYCLE_GAP ((1llu << 24) * 256 + 255)

typedef enum {
  /* Use the same value with nghttp3_elem_dep_type. */
  NGHTTP3_NODE_ID_TYPE_STREAM = NGHTTP3_ELEM_DEP_TYPE_REQUEST,
  NGHTTP3_NODE_ID_TYPE_PUSH = NGHTTP3_ELEM_DEP_TYPE_PUSH,
  NGHTTP3_NODE_ID_TYPE_PLACEHOLDER = NGHTTP3_ELEM_DEP_TYPE_PLACEHOLDER,
  NGHTTP3_NODE_ID_TYPE_ROOT = NGHTTP3_ELEM_DEP_TYPE_ROOT,
} nghttp3_node_id_type;

typedef struct {
  nghttp3_node_id_type type;
  int64_t id;
} nghttp3_node_id;

nghttp3_node_id *nghttp3_node_id_init(nghttp3_node_id *nid,
                                      nghttp3_node_id_type type, int64_t id);

int nghttp3_node_id_eq(const nghttp3_node_id *a, const nghttp3_node_id *b);

struct nghttp3_tnode;
typedef struct nghttp3_tnode nghttp3_tnode;

struct nghttp3_tnode {
  nghttp3_pq_entry pe;
  nghttp3_pq pq;
  nghttp3_tnode *parent;
  nghttp3_tnode *first_child;
  nghttp3_tnode *next_sibling;
  size_t num_children;
  nghttp3_node_id nid;
  uint64_t seq;
  uint64_t cycle;
  uint32_t pending_penalty;
  uint32_t weight;
  /* active is nonzero if this node is scheduled by itself. In other
     words, it is not scheduled just because one of its descendants is
     scheduled. */
  int active;
};

void nghttp3_tnode_init(nghttp3_tnode *tnode, const nghttp3_node_id *nid,
                        uint64_t seq, uint32_t weight, nghttp3_tnode *parent,
                        const nghttp3_mem *mem);

void nghttp3_tnode_free(nghttp3_tnode *tnode);

void nghttp3_tnode_unschedule(nghttp3_tnode *tnode);

/*
 * nghttp3_tnode_schedule schedules |tnode| using |nwrite| as penalty.
 * If |tnode| has already been scheduled, it is rescheduled by the
 * amount of |nwrite|.
 */
int nghttp3_tnode_schedule(nghttp3_tnode *tnode, size_t nwrite);

/*
 * nghttp3_tnode_is_scheduled returns nonzero if |tnode| is scheduled.
 */
int nghttp3_tnode_is_scheduled(nghttp3_tnode *tnode);

/*
 * nghttp3_tnode_get_next returns node which has highest priority.
 * This function returns NULL if there is no node.
 */
nghttp3_tnode *nghttp3_tnode_get_next(nghttp3_tnode *node);

/*
 * nghttp3_tnode_insert inserts |tnode| as a first child of |parent|.
 * |tnode| might have its descendants.
 */
void nghttp3_tnode_insert(nghttp3_tnode *tnode, nghttp3_tnode *parent);

/*
 * nghttp3_tnode_remove removes |tnode| along with its subtree from
 * its parent.
 */
void nghttp3_tnode_remove(nghttp3_tnode *tnode);

/*
 * nghttp3_tnode_squash removes |tnode| from its parent.  The weight
 * of |tnode| is distributed to the direct descendants of |tnode|.
 * They are inserted to the former parent of |tnode|.
 */
int nghttp3_tnode_squash(nghttp3_tnode *tnode);

/*
 * nghttp3_tnode_find_ascendant returns an ascendant of |tnode| whose
 * node ID is |nid|.  If no such node exists, this function returns
 * NULL.
 */
nghttp3_tnode *nghttp3_tnode_find_ascendant(nghttp3_tnode *tnode,
                                            const nghttp3_node_id *nid);

int nghttp3_tnode_has_active_descendant(nghttp3_tnode *tnode);

#endif /* NGHTTP3_TNODE_H */
