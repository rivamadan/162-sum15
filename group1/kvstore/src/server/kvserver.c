#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include "kvconstants.h"
#include "kvcache.h"
#include "kvstore.h"
#include "kvmessage.h"
#include "kvserver.h"
#include "tpclog.h"
#include "socket_server.h"

/* Initializes a kvserver. Will return 0 if successful, or a negative error
 * code if not. DIRNAME is the directory which should be used to store entries
 * for this server.  The server's cache will have NUM_SETS cache sets, each
 * with ELEM_PER_SET elements.  HOSTNAME and PORT indicate where SERVER will be
 * made available for requests.  USE_TPC indicates whether this server should
 * use TPC logic (for PUTs and DELs) or not. */
int kvserver_init(kvserver_t *server, char *dirname, unsigned int num_sets,
    unsigned int elem_per_set, unsigned int max_threads, const char *hostname,
    int port, bool use_tpc) {
  server->state = TPC_INIT;
  int ret;
  ret = kvcache_init(&server->cache, num_sets, elem_per_set);
  if (ret < 0) return ret;
  ret = kvstore_init(&server->store, dirname);
  if (ret < 0) return ret;
  if (use_tpc) {
      ret = tpclog_init(&server->log, dirname);
      if (ret < 0) return ret;
  }
  server->hostname = malloc(strlen(hostname) + 1);
  if (server->hostname == NULL)
    return ENOMEM;
  strcpy(server->hostname, hostname);
  server->port = port;
  server->use_tpc = use_tpc;
  server->max_threads = max_threads;
  server->handle = kvserver_handle;
  return 0;
}

/* Sends a message to register SERVER with a TPCMaster over a socket located at
 * SOCKFD which has previously been connected. Does not close the socket when
 * done. Returns -1 if an error was encountered.
 *
 * Checkpoint 2 only. */
int kvserver_register_master(kvserver_t *server, int sockfd) {

  kvmessage_t *response, registerMessage;
  memset(&registerMessage, 0, sizeof(kvmessage_t));

  registerMessage.type = REGISTER;
  
  registerMessage.key = calloc(1, strlen(server->hostname) + 1);
  strcpy(registerMessage.key, server->hostname);

  char port[6];
  sprintf(port, "%d", server->port);
  registerMessage.value = calloc(1, strlen(port) + 1);
  strcpy(registerMessage.value, port);

  kvmessage_send(&registerMessage, sockfd);
  
  free(registerMessage.key);
  free(registerMessage.value);

  response = kvmessage_parse(sockfd);
  int err = 0;
  if (response != NULL) {
    if (strcmp(response->message, MSG_SUCCESS) != 0) {
      err = ERRINVLDMSG;
    }
    
    kvmessage_free(response);
  } else {
    err = ERRINVLDMSG;
  }

  return err;
}

/* Attempts to get KEY from SERVER. Returns 0 if successful, else a negative
 * error code.  If successful, VALUE will point to a string which should later
 * be free()d.  If the KEY is in cache, take the value from there. Otherwise,
 * go to the store and update the value in the cache. */
int kvserver_get(kvserver_t *server, char *key, char **value) {
  if (strlen(key) > MAX_KEYLEN) 
    return ERRKEYLEN;

  int outcome;
  pthread_rwlock_t *l;

  l = kvcache_getlock(&server->cache, key);

  pthread_rwlock_rdlock(l);
  outcome = kvcache_get(&server->cache, key, value);
  pthread_rwlock_unlock(l);

  if (outcome < 0) {
    outcome = kvstore_get(&server->store, key, value);

    if (outcome == 0) {
      pthread_rwlock_wrlock(l);
      kvcache_put(&server->cache, key, *value);
      pthread_rwlock_unlock(l);
    }
  }
  return outcome;
}

/* Checks if the given KEY, VALUE pair can be inserted into this server's
 * store. Returns 0 if it can, else a negative error code. */
int kvserver_put_check(kvserver_t *server, char *key, char *value) {
  int err = kvstore_put_check(&server->store, key, value);
  return err;
}

/* Inserts the given KEY, VALUE pair into this server's store and cache. Access
 * to the cache should be concurrent if the keys are in different cache sets.
 * Returns 0 if successful, else a negative error code. */
int kvserver_put(kvserver_t *server, char *key, char *value) {
  int err;
  err = kvserver_put_check(server, key, value);
  if (err < 0) {
      return err;
  }

  err = kvstore_put(&server->store, key, value);
  if (err < 0) {
      return err;
  }

  pthread_rwlock_t *lock;
  lock = kvcache_getlock(&server->cache, key);
  pthread_rwlock_wrlock(lock);
  err = kvcache_put(&server->cache, key, value);
  pthread_rwlock_unlock(lock);
  return err;
}

/* Checks if the given KEY can be deleted from this server's store.
 * Returns 0 if it can, else a negative error code. */
int kvserver_del_check(kvserver_t *server, char *key) {
  int err = kvstore_del_check(&server->store, key);
  return err;
}

/* Removes the given KEY from this server's store and cache. Access to the
 * cache should be concurrent if the keys are in different cache sets. Returns
 * 0 if successful, else a negative error code. */
int kvserver_del(kvserver_t *server, char *key) {
  pthread_rwlock_t* lock;
  lock = kvcache_getlock(&server->cache, key);

  pthread_rwlock_wrlock(lock);
  int outcome = kvcache_del(&server->cache, key); 
  pthread_rwlock_unlock(lock);

  int check = kvserver_del_check(server, key);
  if (!check) {
    outcome = kvstore_del(&server->store, key);
  }

  return outcome;
}

/* Returns an info string about SERVER including its hostname and port. */
char *kvserver_get_info_message(kvserver_t *server) {
  char info[1024], buf[256];
  time_t ltime = time(NULL);
  strcpy(info, asctime(localtime(&ltime)));
  sprintf(buf, "{%s, %d}", server->hostname, server->port);
  strcat(info, buf);
  char *msg = malloc(strlen(info));
  strcpy(msg, info);
  return msg;
}

/* Handles an incoming kvmessage REQMSG, and populates the appropriate fields
 * of RESPMSG as a response. RESPMSG and REQMSG both must point to valid
 * kvmessage_t structs. Assumes that the request should be handled as a TPC
 * message. This should also log enough information in the server's TPC log to
 * be able to recreate the current state of the server upon recovering from
 * failure.  See the spec for details on logic and error messages.
 *
 * Checkpoint 2 only. */
void kvserver_handle_tpc(kvserver_t *server, kvmessage_t *reqmsg,
    kvmessage_t *respmsg) {
  int err;

  respmsg->type = RESP;
  switch (reqmsg->type)
  {
    case GETREQ: 
    {
      char **value = malloc(sizeof(char **));
      err = kvserver_get(server, reqmsg->key, value);
      if (!err) {
        respmsg->type = GETRESP;
        respmsg->key = reqmsg->key;
        respmsg->value = *value;
      }
      break;
    }
    case PUTREQ:
    {

      if (server->state == TPC_WAIT) {
        respmsg->message = ERRMSG_INVALID_REQUEST;
        return;
      }

      tpclog_log(&server->log, PUTREQ, reqmsg->key, reqmsg->value);
      err = kvserver_put_check(server, reqmsg->key, reqmsg->value);
      if (!err) {
        respmsg->type = VOTE_COMMIT;
        respmsg->key = reqmsg->key;
        respmsg->value = reqmsg->value;
        server->state = TPC_WAIT;
      } else {
        respmsg->type = VOTE_ABORT;
        respmsg->key = reqmsg->key;
        respmsg->value = reqmsg->value;
        server->state = TPC_READY;
      }
      break; 
    }
    case DELREQ:
    {

      if (server->state == TPC_WAIT) {
        respmsg->message = ERRMSG_INVALID_REQUEST;
        return;
      }

      tpclog_log(&server->log, DELREQ, reqmsg->key, NULL);
      err = kvserver_del_check(server, reqmsg->key);
      if (!err) {
        respmsg->type = VOTE_COMMIT;
        respmsg->key = reqmsg->key;
        server->state = TPC_WAIT;
      } else {
        respmsg->type = VOTE_ABORT;
        respmsg->key = reqmsg->key;
        server->state = TPC_READY;
      }
      break;
    }
    case COMMIT:
    {
      logentry_t* entry;
      tpclog_iterate_begin(&server->log);
      while(tpclog_iterate_has_next(&server->log))
      {
        entry = tpclog_iterate_next(&server->log);
      }
      tpclog_log(&server->log, COMMIT, NULL, NULL);
      int keylen = strlen(entry->data);
      char val[MAX_VALLEN];
      strcpy(val, entry->data + keylen + 1);
      if (entry->type == PUTREQ) {
        err = kvserver_put(server, entry->data, val);
      } if (entry->type == DELREQ) {
        err = kvserver_del(server, entry->data);
      }
      respmsg->type = ACK;
      server->state = TPC_READY;
      break;
    }
    case ABORT:
    {
      tpclog_log(&server->log, ABORT, NULL, NULL);
      respmsg->type = ACK;
      server->state = TPC_READY;
      break;
    }
    default:
    {
      break;
    }
  }

  if (err) {
    respmsg->message = GETMSG(err);
  } else {
    respmsg->message = MSG_SUCCESS;
  }
}

/* Handles an incoming kvmessage REQMSG, and populates the appropriate fields
 * of RESPMSG as a response. RESPMSG and REQMSG both must point to valid
 * kvmessage_t structs. Assumes that the request should be handled as a non-TPC
 * message. See the spec for details on logic and error messages. */
void kvserver_handle_no_tpc(kvserver_t *server, kvmessage_t *reqmsg,
    kvmessage_t *respmsg) {
  int err;

  respmsg->type = RESP;

  switch (reqmsg->type)
  {
    case GETREQ: 
    {
      char **value = malloc(sizeof(char **));
      err = kvserver_get(server, reqmsg->key, value);
      if (!err) {
        respmsg->type = GETRESP;
        respmsg->key = reqmsg->key;
        respmsg->value = *value;
      }
      break;
    }
    case PUTREQ:
    {
      err = kvserver_put(server, reqmsg->key, reqmsg->value);   
      break;   
    }
    case DELREQ:
    {
      err = kvserver_del(server, reqmsg->key);
      break;
    }
    default:
    {
      break;
    }
  }

  if (err) {
    respmsg->message = GETMSG(err);
  } else {
    respmsg->message = MSG_SUCCESS;
  }
}

/* Generic entrypoint for this SERVER. Takes in a socket on SOCKFD, which
 * should already be connected to an incoming request. Processes the request
 * and sends back a response message.  This should call out to the appropriate
 * internal handler. */
void kvserver_handle(kvserver_t *server, int sockfd, void *extra) {
  kvmessage_t *reqmsg, *respmsg;
  respmsg = calloc(1, sizeof(kvmessage_t));
  reqmsg = kvmessage_parse(sockfd);
  void (*server_handler)(kvserver_t *server, kvmessage_t *reqmsg,
      kvmessage_t *respmsg);
  server_handler = server->use_tpc ?
    kvserver_handle_tpc : kvserver_handle_no_tpc;
  if (reqmsg == NULL) {
    respmsg->type = RESP;
    respmsg->message = ERRMSG_INVALID_REQUEST;
  } else {
    server_handler(server, reqmsg, respmsg);
  }
  kvmessage_send(respmsg, sockfd);
  if (reqmsg != NULL)
    kvmessage_free(reqmsg);
}

void send_msg(int sockfd, msgtype_t type) {
  kvmessage_t *message;
  message = calloc(1, sizeof(kvmessage_t));

  message->type = type;

  kvmessage_send(message, sockfd);
}


/* Restore SERVER back to the state it should be in, according to the
 * associated LOG.  Must be called on an initialized  SERVER. Only restores the
 * state of the most recent TPC transaction, assuming that all previous actions
 * have been written to persistent storage. Should restore SERVER to its exact
 * state; e.g. if SERVER had written into its log that it received a PUTREQ but
 * no corresponding COMMIT/ABORT, after calling this function SERVER should
 * again be waiting for a COMMIT/ABORT.  This should also ensure that as soon
 * as a server logs a COMMIT, even if it crashes immediately after (before the
 * KVStore has a chance to write to disk), the COMMIT will be finished upon
 * rebuild. The cache need not be the same as before rebuilding.
 *
 * Checkpoint 2 only. */
int kvserver_rebuild_state(kvserver_t *server) {
  logentry_t *entry, *prev;
  
  tpclog_iterate_begin(&server->log);
  while(tpclog_iterate_has_next(&server->log))
  {
    entry = tpclog_iterate_next(&server->log);
    if (entry->type != COMMIT && entry->type != ABORT) {
      prev = entry;
    }
  }
  
  int sockfd = connect_to(server->hostname, server->port, 0);

  if (entry != NULL) {
    if (entry->type == DELREQ) {
      if (!(kvserver_del_check(server, entry->data))) {
        send_msg(sockfd, VOTE_COMMIT);
        server->state = TPC_WAIT;
      } else {
        send_msg(sockfd, VOTE_ABORT);
        server->state = TPC_READY;
      }
    } else if (entry->type == PUTREQ) {
      int keylen = strlen(entry->data);
      char val[MAX_VALLEN];
      strcpy(val, entry->data + keylen + 1);
      if (!(kvserver_put_check(server, entry->data, val))) {
        send_msg(sockfd, VOTE_COMMIT);
        server->state = TPC_WAIT;
      } else {
        send_msg(sockfd, VOTE_ABORT);
        server->state = TPC_READY;
      }
    } else if (entry->type == COMMIT) {
        int keylen = strlen(prev->data);
        char val[MAX_VALLEN];
        strcpy(val, prev->data + keylen + 1);
        if (prev->type == PUTREQ) {
          kvserver_put(server, prev->data, val);
        } if (prev->type == DELREQ) {
          kvserver_del(server, prev->data);
        }
        send_msg(sockfd, ACK);
    }
  }
  
  return 1;
}

/* Deletes all current entries in SERVER's store and removes the store
 * directory.  Also cleans the associated log. */
int kvserver_clean(kvserver_t *server) {
  return kvstore_clean(&server->store);
}
