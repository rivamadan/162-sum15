#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include "kvconstants.h"
#include "kvmessage.h"
#include "socket_server.h"
#include "time.h"
#include "tpcmaster.h"

int tpcmaster_send_get(tpcslave_t *slave, tpcmaster_t *master, char *key, char **val);

/* Initializes a tpcmaster. Will return 0 if successful, or a negative error
 * code if not. SLAVE_CAPACITY indicates the maximum number of slaves that
 * the master will support. REDUNDANCY is the number of replicas (slaves) that
 * each key will be stored in. The master's cache will have NUM_SETS cache sets,
 * each with ELEM_PER_SET elements. */
int tpcmaster_init(tpcmaster_t *master, unsigned int slave_capacity,
    unsigned int redundancy, unsigned int num_sets, unsigned int elem_per_set) {
  int ret;
  ret = kvcache_init(&master->cache, num_sets, elem_per_set);
  if (ret < 0) return ret;
  ret = pthread_rwlock_init(&master->slave_lock, NULL);
  if (ret < 0) return ret;
  master->slave_count = 0;
  master->slave_capacity = slave_capacity;
  if (redundancy > slave_capacity) {
    master->redundancy = slave_capacity;
  } else {
    master->redundancy = redundancy;
  }
  master->slaves_head = NULL;
  master->handle = tpcmaster_handle;
  return 0;
}

/* Converts Strings to 64-bit longs. Borrowed from http://goo.gl/le1o0W,
 * adapted from the Java builtin String.hashcode().
 * DO NOT CHANGE THIS FUNCTION. */
int64_t hash_64_bit(char *s) {
  int64_t h = 1125899906842597LL;
  int i;
  for (i = 0; s[i] != 0; i++) {
    h = (31 * h) + s[i];
  }
  return h;
}

/* Handles an incoming kvmessage REQMSG, and populates the appropriate fields
 * of RESPMSG as a response. RESPMSG and REQMSG both must point to valid
 * kvmessage_t structs. Assigns an ID to the slave by hashing a string in the
 * format PORT:HOSTNAME, then tries to add its info to the MASTER's list of
 * slaves. If the slave is already in the list, do nothing (success).
 * There can never be more slaves than the MASTER's slave_capacity. RESPMSG
 * will have MSG_SUCCESS if registration succeeds, or an error otherwise.
 *
 * Checkpoint 2 only. */
void tpcmaster_register(tpcmaster_t *master, kvmessage_t *reqmsg,
    kvmessage_t *respmsg) {

    int err = 0;
    unsigned int port = atoi(reqmsg->value);
    char *hostname = malloc(strlen(reqmsg->key)+1);
    strcpy(hostname, reqmsg->key);
    char *string = malloc(strlen(reqmsg->value)+1+strlen(reqmsg->key)+1);
    strcpy(string, reqmsg->value);
    strcat(string, ":");
    strcat(string, hostname);
    int64_t hash;
    hash = hash_64_bit(string);
    free(string);
    
    pthread_rwlock_wrlock(&master->slave_lock);
    if (master-> slave_count >= master->slave_capacity) {
       err = -1;
    } else {
        tpcslave_t* slave = malloc(sizeof(tpcslave_t));
        slave->id = hash;
        slave->host = hostname;
        slave->port = port;
        slave->next = NULL;
        slave->prev = NULL;
        int exists = 0;
        if (master->slaves_head == NULL) {
           master->slaves_head = slave;
        } else {
           if (hash < master->slaves_head->id) {
	            slave->next = master->slaves_head;
              master->slaves_head->prev = slave;
	            master->slaves_head = slave;
      	   } else {
      	       tpcslave_t* current;
                for (current = master->slaves_head; current->next != NULL && current->id < hash; current = current->next) {
      	           if (current->id == hash) {
                         exists = 1;
                    }
                }
                if (!exists) {
                    if (current->next == NULL) {
                       current->next = slave;
                       slave->prev = current;
                     } else {
                        slave->next = current->next;
                        slave->next->prev = slave;
      	                current->next = slave;
                        slave->prev = current;
        	           }
                }
            }
	      }
        if (!exists) {
           master->slave_count++;
        }
     }
     pthread_rwlock_unlock(&master->slave_lock);
     if (err == 0) {
       respmsg->type = RESP;
       respmsg->key = NULL;
       respmsg->value = NULL;
       respmsg->message = MSG_SUCCESS;
     } else {
       respmsg->message = ERRMSG_GENERIC_ERROR;
     }
 
}

/* Hashes KEY and finds the first slave that should contain it.
 * It should return the first slave whose ID is greater than the
 * KEY's hash, and the one with lowest ID if none matches the
 * requirement.
 *
 * Checkpoint 2 only. */
tpcslave_t *tpcmaster_get_primary(tpcmaster_t *master, char *key) {
  int64_t hash = hash_64_bit(key);
  pthread_rwlock_rdlock(&master->slave_lock);
  tpcslave_t* current;
  for (current = master->slaves_head; current->next && current->id <= hash; current = current->next) {
     }
   if (current->next == NULL) {
      if (current->id <= hash) {
         current = master->slaves_head;
      }
    }
   pthread_rwlock_unlock(&master->slave_lock);
   return current;
}

/* Returns the slave whose ID comes after PREDECESSOR's, sorted
 * in increasing order.
 *
 * Checkpoint 2 only. */
tpcslave_t *tpcmaster_get_successor(tpcmaster_t *master,
    tpcslave_t *predecessor) {
  pthread_rwlock_rdlock(&master->slave_lock);
  tpcslave_t* current;
  if (predecessor->next == NULL) {
     current = master->slaves_head;
  } else {
     current = predecessor->next;
  }
  pthread_rwlock_unlock(&master->slave_lock);
  return current;
}

/* Handles an incoming GET request REQMSG, and populates the appropriate fields
 * of RESPMSG as a response. RESPMSG and REQMSG both must point to valid
 * kvmessage_t structs.
 *
 * Checkpoint 2 only. */
void tpcmaster_handle_get(tpcmaster_t *master, kvmessage_t *reqmsg,
    kvmessage_t *respmsg) {

    int err = 0;
    respmsg->type = GETRESP;
    respmsg->key = reqmsg->key;
    respmsg->value = (char*) malloc(MAX_VALLEN * sizeof(char));
    err = kvcache_get(&master->cache, reqmsg->key, &respmsg->value);
    if (!err) {
      return;
    }
    tpcslave_t *node = tpcmaster_get_primary(master, reqmsg->key);
    err = tpcmaster_send_get(node, master, reqmsg->key, &respmsg->value); 
    if (!err) {
      return;
    }
    int i = 1;
    while (i < master->redundancy) {
      node = tpcmaster_get_successor(master, node);
      err = tpcmaster_send_get(node, master, reqmsg->key, &respmsg->value);
      if (!err) {
        break;
      }
      i++;
    }
    if (err<0) {
      respmsg->type = RESP;
      respmsg->message = ERRMSG_NO_KEY;
      respmsg->key = NULL;
      respmsg->value = NULL;
    }
}

int tpcmaster_send_get(tpcslave_t *slave, tpcmaster_t *master, char *key, char **val) {
    pthread_rwlock_rdlock(&master->slave_lock);
    int sockfd = connect_to(slave->host, slave->port, 2);
    pthread_rwlock_unlock(&master->slave_lock);
    kvmessage_t *slave_resp;
    kvmessage_t *slave_send = (kvmessage_t*) malloc(sizeof(kvmessage_t));
    slave_send->type = GETREQ;
    slave_send->key = key;
    // slave_send->value = NULL;
    // slave_send->message = NULL;
    kvmessage_send(slave_send, sockfd);
    slave_resp = kvmessage_parse(sockfd);
    if (slave_resp == NULL || slave_resp->type != GETRESP) {
      return -1;
    } else {
       *val = slave_resp->value;
       return 0;
    }
}

/* Handles an incoming TPC request REQMSG, and populates the appropriate fields
 * of RESPMSG as a response. RESPMSG and REQMSG both must point to valid
 * kvmessage_t structs. Implements the TPC algorithm, polling all the slaves
 * for a vote first and sending a COMMIT or ABORT message in the second phase.
 * Must wait for an ACK from every slave after sending the second phase messages. 
 * 
 * The CALLBACK field is used for testing purposes. You MUST include the following
 * calls to the CALLBACK function whenever CALLBACK is not null, or you will fail
 * some of the tests:
 * - During both phases of contacting slaves, whenever a slave cannot be reached (i.e. you
 *   attempt to connect and receive a socket fd of -1), call CALLBACK(slave), where
 *   slave is a pointer to the tpcslave you are attempting to contact.
 * - Between the two phases, call CALLBACK(NULL) to indicate that you are transitioning
 *   between the two phases.  
 * 
 * Checkpoint 2 only. */
void tpcmaster_handle_tpc(tpcmaster_t *master, kvmessage_t *reqmsg,
    kvmessage_t *respmsg, callback_t callback) {
  tpcslave_t *slave;    
  kvmessage_t *response, *message;
  int abort_tpc = 0;
  int counter = 0;
  int sockfd = -1;
  

  slave = master->slaves_head;
  while (slave && counter < master->slave_count) {
    sockfd = connect_to(slave->host, slave->port, 2);
    if (sockfd < 0 && callback) {
      callback(slave);
    }
    
    message = (kvmessage_t*) malloc(sizeof(kvmessage_t));
    message->type = reqmsg->type;
    message->key = reqmsg->key;
    message->value = reqmsg->value;
    kvmessage_send(message, sockfd);

    response = kvmessage_parse(sockfd);
    if (response != NULL) {
      if (response->type == VOTE_ABORT) {
        abort_tpc = 1;
      }
      kvmessage_free(response);
    }

    slave = slave->next;
    counter++;

  }

  if (callback) {
    callback(NULL);
  }

  counter = 0;
  slave = master->slaves_head;
  while (slave && counter < master->slave_count) {
    sockfd = connect_to(slave->host, slave->port, 2);
    if (sockfd < 0 && callback) {
      callback(slave);
    }

    message = (kvmessage_t*) malloc(sizeof(kvmessage_t));

    if (abort_tpc) {
      message->type = ABORT;
    } else {
      message->type = COMMIT;
    }
    
    message->key = NULL;
    message->value = NULL;
    message->message = NULL;

    kvmessage_send(message, sockfd);

    response = kvmessage_parse(sockfd);
    while (response == NULL || response->type != ACK) {
      if (response != NULL) {
        kvmessage_free(response);
      }

      message = (kvmessage_t*) malloc(sizeof(kvmessage_t));

      if (abort_tpc) {
        message->type = ABORT;
      } else {
        message->type = COMMIT;
      }

      kvmessage_send(message, sockfd);

      response = kvmessage_parse(sockfd);
    }

    if (response != NULL) {
      kvmessage_free(response);
    }

    slave = slave->next;
    counter++;

  }

  respmsg->type = RESP;
  respmsg->message = MSG_SUCCESS;

}

/* Handles an incoming kvmessage REQMSG, and populates the appropriate fields
 * of RESPMSG as a response. RESPMSG and REQMSG both must point to valid
 * kvmessage_t structs. Provides information about the slaves that are
 * currently alive.
 *
 * Checkpoint 2 only. */
void tpcmaster_info(tpcmaster_t *master, kvmessage_t *reqmsg,
    kvmessage_t *respmsg) {

    int i;
    int fd;
    char temp[1024];
    char buf[100];
    time_t ltime = time(NULL);
    strcpy(temp, asctime(localtime(&ltime)));
    strcat(temp, "Slaves:");
    tpcslave_t *slave = tpcmaster_get_primary(master, reqmsg->key);
    for (i = 0; i < master->slave_count; i++) {
        fd = connect_to(slave->host, slave->port, 2);
        if (fd != -1) {
           strcat(temp, "\n");
           strcat(temp, "{");
           strcat(temp, slave->host);
           strcat(temp, ", ");
           sprintf(buf, "%d", slave->port);
           strcat(temp, buf);
           strcat(temp, "}");
        }
        slave = tpcmaster_get_successor(master, slave);
     }
     strcat(temp, "\0");
     respmsg->message = temp;
}


/* Generic entrypoint for this MASTER. Takes in a socket on SOCKFD, which
 * should already be connected to an incoming request. Processes the request
 * and sends back a response message.  This should call out to the appropriate
 * internal handler. */
void tpcmaster_handle(tpcmaster_t *master, int sockfd, callback_t callback) {
  kvmessage_t *reqmsg, respmsg;
  reqmsg = kvmessage_parse(sockfd);
  memset(&respmsg, 0, sizeof(kvmessage_t));
  respmsg.type = RESP;
  if (reqmsg->key != NULL) {
    respmsg.key = calloc(1, strlen(reqmsg->key));
    strcpy(respmsg.key, reqmsg->key);
  }
  if (reqmsg->type == INFO) {
    tpcmaster_info(master, reqmsg, &respmsg);
  } else if (reqmsg == NULL || reqmsg->key == NULL) {
    respmsg.message = ERRMSG_INVALID_REQUEST;
  } else if (reqmsg->type == REGISTER) {
    tpcmaster_register(master, reqmsg, &respmsg);
  } else if (reqmsg->type == GETREQ) {
    tpcmaster_handle_get(master, reqmsg, &respmsg);
  } else {
    tpcmaster_handle_tpc(master, reqmsg, &respmsg, callback);
  }
  kvmessage_send(&respmsg, sockfd);
  kvmessage_free(reqmsg);
  if (respmsg.key != NULL)
    free(respmsg.key);
}

/* Completely clears this TPCMaster's cache. For testing purposes. */
void tpcmaster_clear_cache(tpcmaster_t *tpcmaster) {
  kvcache_clear(&tpcmaster->cache);
}
