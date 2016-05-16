/*
 * cclist.h
 *
 *  Created on: Mar 20, 2016
 *      Author: amyznikov
 */
#pragma once

#ifndef __cqueue_h__
#define __cqueue_h__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>


#ifdef __cplusplus
extern "C" {
#endif


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef
struct ccfifo {
  void * items;
  size_t capacity;
  size_t size;
  size_t item_size;
  size_t first;
  size_t last;
} ccfifo;


#define ccfifo_item_type(q)  uint8_t(*)[(q)->item_size]
#define ccfifo_item(q,pos)   (((ccfifo_item_type(q))(q)->items)+(pos))


static inline bool ccfifo_init(ccfifo * q, size_t capacity, size_t item_size)
{
  if ( (q->items = malloc(capacity * item_size)) ) {
    q->capacity = capacity;
    q->item_size = item_size;
    q->size = q->first = q->last = 0;
  }
  return q->items != NULL;
}

static inline void ccfifo_cleanup(ccfifo * q)
{
  free(q->items);
  memset(q, 0, sizeof(*q));
}


static inline void * ccfifo_push_bytes(ccfifo * q, const void * item, size_t size)
{
  void * itempos = NULL;

  if ( q->size < q->capacity ) {

    itempos = ccfifo_item(q, q->last);

    if ( item && size ) {
      memcpy(itempos, item, size );
    }

    ++q->size;
    if ( ++ q->last >= q->capacity ) {
      q->last = 0;
    }
  }

  return itempos;
}

static inline bool ccfifo_pop_bytes(ccfifo * q, void * pitem, size_t size)
{
  if ( !q->size ) {
    return false;
  }

  if ( pitem && size ) {
    memcpy(pitem, ccfifo_item(q, q->first), size);
  }

  --q->size;
  if ( ++q->first >= q->capacity ) {
    q->first = 0;
  }

  return true;
}

static inline void * ccfifo_push(ccfifo * q, const void * pitem)
{
  return ccfifo_push_bytes(q, pitem, q->item_size);
}

static inline void * ccfifo_ppush(ccfifo * q, const void * item)
{
  return ccfifo_push(q, &item);
}

static inline bool ccfifo_pop(ccfifo * q, void * pitem)
{
  return ccfifo_pop_bytes(q, pitem, q->item_size);
}

static inline void * ccfifo_ppop(ccfifo * q)
{
  void * item = NULL;
  ccfifo_pop(q, &item);
  return item;
}

static inline void * ccfifo_peek(const ccfifo * q, size_t index)
{
  return ccfifo_item(q, q->first + index);
}


static inline void * ccfifo_ppeek(const ccfifo * q, size_t index)
{
  return *(void**)(ccfifo_item(q, q->first + index));
}

static inline void * ccfifo_peek_front(const ccfifo * q)
{
  return (q->size ? ccfifo_item(q, q->first) : NULL);
}

static inline void * ccfifo_ppeek_front(const ccfifo * q)
{
  return (q->size ? *(void**)(ccfifo_item(q, q->first)) : NULL);
}

static inline size_t ccfifo_size(const ccfifo * q)
{
  return q->size;
}

static inline size_t ccfifo_capacity(const ccfifo * q)
{
  return q->capacity;
}

static inline bool ccfifo_is_full(const ccfifo * q)
{
  return q->size == q->capacity;
}

static inline bool ccfifo_is_empty(const ccfifo * q)
{
  return q->size == 0;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef
struct ccheap {
  void * blocks;
  ccfifo fifo;
} ccheap;


static inline bool ccheap_init(ccheap * h, size_t max_blocks, size_t block_size)
{
  bool fok = false;

  memset(h, 0, sizeof(*h));

  if ( !(h->blocks = malloc(max_blocks * block_size)) ) {
    goto end;
  }

  if ( !ccfifo_init(&h->fifo, max_blocks, sizeof(void*)) ) {
    goto end;
  }

  for ( size_t i = 0; i < max_blocks; ++i ) {
    //ccfifo_ppush(&h->fifo, (((uint8_t(*)[block_size])h->blocks) + i));
    ccfifo_ppush(&h->fifo, (((uint8_t(*)[block_size])h->blocks) + max_blocks - i - 1));
  }

  fok = true;

end:
  if ( !fok ) {
    ccfifo_cleanup(&h->fifo);
    free(h->blocks), h->blocks = NULL;
  }

  return fok;
}

static inline void ccheap_cleanup(ccheap * h)
{
  if ( h ) {
    ccfifo_cleanup(&h->fifo);
    free(h->blocks), h->blocks = NULL;
  }
}


static inline void * ccheap_alloc(ccheap * h)
{
  void * pb = ccfifo_ppop(&h->fifo);
  if ( !pb ) {
    errno = ENOMEM;
  }
  return pb;
}

static inline void ccheap_free(ccheap * h, void * pb)
{
  ccfifo_ppush(&h->fifo, pb);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef
struct cclist_node {
  struct cclist_node * prev, * next;
  union {
    void * sp;
    uint8_t u;
  } item[];
} cclist_node;

typedef
struct cclist {
  ccheap heap;
  struct cclist_node * head, * tail;
  size_t capacity;
  size_t item_size;
} cclist;


static inline size_t cclist_node_size(const cclist * cc)
{
  return (sizeof(struct cclist_node) + cc->item_size);
}

static inline bool cclist_init(cclist * cc, size_t capacity, size_t item_size)
{
  cc->head = cc->tail = NULL;
  cc->capacity = capacity;
  cc->item_size = item_size;
  return ccheap_init(&cc->heap, capacity, cclist_node_size(cc));
}

static inline void cclist_cleanup(cclist * cc)
{
  if ( cc ) {
    ccheap_cleanup(&cc->heap);
    cc->head = cc->tail = NULL;
    cc->capacity = cc->item_size = 0;
  }
}


static inline struct cclist_node * cclist_head(const cclist * cc)
{
  return cc->head;
}

static inline struct cclist_node * cclist_tail(const cclist * cc)
{
  return cc->tail;
}

static inline void * cclist_peek(struct cclist_node * node)
{
  return node ? node->item : NULL;
}

static inline void * cclist_ppeek(const struct cclist_node * node)
{
  return node ? node->item[0].sp : NULL;
//  if ( node ) {
//    // avoid compiler warning : dereferencing type-punned pointer will break strict-aliasing rules
//    void ** p = (void**)((void*)node->item);
//    return *p;
//  }
//  return NULL;
}

static inline struct cclist_node * cclist_push(cclist * cc, struct cclist_node * after, const void * pitem)
{
  struct cclist_node * node = NULL;

  if ( !after && cc->head ) {
    errno = EINVAL;
  }
  else if ( (node = ccheap_alloc(&cc->heap)) ) {

    node->prev = after;

    if ( !cc->head ) {
      cc->head = cc->tail = node;
    }

    if ( !after ) {
      node->next = NULL;
    }
    else {

      node->next = after->next;
      after->next = node;

      if ( after == cc->tail ) {
        cc->tail = node;
      }
    }

    if ( pitem ) {
      memcpy(node->item, pitem, cc->item_size);
    }
  }

  return node;
}

static inline struct cclist_node * cclist_ppush(cclist * cc, struct cclist_node * after, const void * item)
{
  return cclist_push(cc, after, &item);
}

static inline struct cclist_node * cclist_push_back(cclist * cc, const void * pitem)
{
  return cclist_push(cc, cclist_tail(cc), pitem);
}

static inline struct cclist_node * cclist_ppush_back(cclist * cc, const void * item)
{
  return cclist_push_back(cc, &item);
}

static inline struct cclist_node * cclist_insert(cclist * cc, struct cclist_node * before, const void * pitem)
{
  struct cclist_node * node = NULL;

  if ( !before && cc->tail ) {
    errno = EINVAL;
  }
  else if ( (node = ccheap_alloc(&cc->heap)) ) {

    node->next = before;

    if ( !cc->tail ) {
      cc->tail = cc->head = node;
    }

    if ( !before ) {
      node->prev = NULL;
    }
    else {

      node->prev = before->prev;
      before->prev = node;

      if ( before == cc->head ) {
        cc->head = node;
      }
    }

    memcpy(node->item, pitem, cc->item_size);
  }

  return node;
}


static inline struct cclist_node * cclist_pinsert(cclist * cc, struct cclist_node * before, const void * item)
{
  return cclist_insert(cc, before, &item);
}


static inline void cclist_erase(cclist * cc, struct cclist_node * node)
{
  if ( node ) {

    struct cclist_node * prev = node->prev;
    struct cclist_node * next = node->next;

    if ( prev ) {
      prev->next = next;
    }

    if ( next ) {
      next->prev = prev;
    }

    if ( node == cc->head ) {
      cc->head = next;
    }

    if ( node == cc->tail ) {
      cc->tail = prev;
    }

    ccheap_free(&cc->heap, node);
  }
}


#ifdef __cplusplus
}
#endif

#endif /* __cqueue_h__ */
