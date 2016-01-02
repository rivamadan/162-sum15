#include <pthread.h>
#include <errno.h>
#include <stdbool.h>
#include "uthash.h"
#include "utlist.h"
#include "kvconstants.h"
#include "kvcacheset.h"

/* Initializes CACHESET to hold a maximum of ELEM_PER_SET elements.
 * ELEM_PER_SET must be at least 2.
 * Returns 0 if successful, else a negative error code. */
int kvcacheset_init(kvcacheset_t *cacheset, unsigned int elem_per_set) {
  int ret;
  if (elem_per_set < 2)
    return -1;
  cacheset->elem_per_set = elem_per_set;
  if ((ret = pthread_rwlock_init(&cacheset->lock, NULL)) < 0)
    return ret;
  cacheset->num_entries = 0;
  cacheset->list_ptr = NULL;
  cacheset->hash_ptr = NULL;
  return 0;
}


/* Get the entry corresponding to KEY from CACHESET. Returns 0 if successful,
 * else returns a negative error code. If successful, populates VALUE with a
 * malloced string which should later be freed. */
int kvcacheset_get(kvcacheset_t *cacheset, char *key, char **value) {
  struct kvcacheentry *entry;
  HASH_FIND_STR(cacheset->hash_ptr, key, entry);

  if (!entry) {
    return ERRNOKEY;
  }

  entry->refbit = true;
  *value = (char*) malloc(strlen(entry->value) + 1);
  strcpy(*value, entry->value);
  return 0;
}

/* Add the given KEY, VALUE pair to CACHESET. Returns 0 if successful, else
 * returns a negative error code. Should evict elements if necessary to not
 * exceed CACHESET->elem_per_set total entries. */
int kvcacheset_put(kvcacheset_t *cacheset, char *key, char *value) {
  struct kvcacheentry *entry, *elt, *tmp;

  HASH_FIND_STR(cacheset->hash_ptr, key, entry);
  if (entry) {
    free(entry->value);
    entry->refbit = true;
  } else {
    entry = malloc(sizeof(struct kvcacheentry));
    entry->key = (char*) malloc(strlen(key) + 1);
    if (!entry->key) {
      return -1;
    }

    strcpy(entry->key, key);
    entry->refbit = false;
  }

  entry->value = (char*) malloc(strlen(value) + 1);
  if (!entry->value) {
    return -1;
  }
  strcpy(entry->value, value);

  if (!entry->refbit) {
    if (cacheset->num_entries < cacheset->elem_per_set) {
      cacheset->num_entries++;
    } else {
      DL_FOREACH_SAFE(cacheset->list_ptr, elt, tmp) {
        DL_DELETE(cacheset->list_ptr, elt);

        if (!elt->refbit) {
          HASH_DEL(cacheset->hash_ptr, elt);
          free(elt->key);
          free(elt->value);
          free(elt);
          break;
        }

        elt->refbit = false;
        DL_APPEND(cacheset->list_ptr, elt);
      }
    }

    DL_APPEND(cacheset->list_ptr, entry);
    HASH_ADD_KEYPTR(hh, cacheset->hash_ptr, entry->key, strlen(entry->key), entry);
  }
  return 0;
}

/* Deletes the entry corresponding to KEY from CACHESET. Returns 0 if
 * successful, else returns a negative error code. */
int kvcacheset_del(kvcacheset_t *cacheset, char *key) {
  struct kvcacheentry *elt;
  HASH_FIND_STR(cacheset->hash_ptr, key, elt);
  if (elt) {
    HASH_DEL(cacheset->hash_ptr, elt);
    DL_DELETE(cacheset->list_ptr, elt);
    free(elt->key);
    free(elt->value);
    free(elt);
    cacheset->num_entries--;
    return 0;
  }
  return ERRNOKEY;
}

/* Completely clears this cache set. For testing purposes. */
void kvcacheset_clear(kvcacheset_t *cacheset) {
  struct kvcacheentry *elt, *tmp;

  HASH_ITER(hh, cacheset->hash_ptr, elt, tmp) {
    HASH_DEL(cacheset->hash_ptr, elt);
    DL_DELETE(cacheset->list_ptr, elt);
    free(elt->key);
    free(elt->value);
    free(elt);
  }
}

// -------- ADDED FOR GROUP 1 TESTS ---------
int kvcacheset_ref(kvcacheset_t *cacheset, char *key) {
  struct kvcacheentry *elt;
  HASH_FIND_STR(cacheset->hash_ptr, key, elt);

  if (!elt)
  {
    return ERRNOKEY;
  }
    
  return elt->refbit;
}
// -------- ADDED FOR GROUP 1 TESTS ---------
