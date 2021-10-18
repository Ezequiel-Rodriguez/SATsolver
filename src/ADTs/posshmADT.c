// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/**
 * @file: posshmADT.c
 * @authors: Ezequiel Rodriguez, Juan I. Garcia M. & Jerónimo Brave.
 */

/* Local Includes */
#include "../include/shmADT.h"

/* Standard & Sys lib's */
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct t_shm *Pt_shm;

typedef struct t_shm {
  int w_pointer;
  int r_pointer;
  char buf[MAX_LENGTH];
} t_shm;

typedef struct shmCDT {
  Pt_shm shm;
  sem_t *sem, *writer_in;
  int shm_fd;
  char shm_name[MAX_NAME_LENGTH];
  char sem_name[MAX_NAME_LENGTH];
} shmCDT;

shmADT newShm(const char *shm_name, const char *sem_name, int flags, int mode) {
  shmADT shm_adt = malloc(sizeof(shmCDT));
  if (shm_adt == NULL) {
    return NULL;
  }

  strcpy(shm_adt->shm_name, shm_name);
  strcpy(shm_adt->sem_name, sem_name);

  shm_adt->shm_fd = shm_open(shm_name, flags, mode);
  if (shm_adt->shm_fd == -1) {
    free(shm_adt);
    return NULL;
  }

  if (flags & O_CREAT) {
    if (ftruncate(shm_adt->shm_fd, sizeof(t_shm)) == -1) {
      closeShm(shm_adt, flags & O_CREAT);
      return NULL;
    }
  }

  shm_adt->shm = mmap(NULL, sizeof(t_shm),
                      ((flags & O_RDWR) ? (PROT_READ | PROT_WRITE) : PROT_READ),
                      MAP_SHARED, shm_adt->shm_fd, 0);
  if (shm_adt->shm == (void *)-1) {
    closeShm(shm_adt, flags & O_CREAT);
    return NULL;
  }

  if (flags & O_CREAT) {
    shm_adt->sem = sem_open(shm_adt->sem_name, O_CREAT, mode, 0);
    if (shm_adt->sem == SEM_FAILED) {
      closeShm(shm_adt, true);
      return NULL;
    }
    shm_adt->writer_in = sem_open(WRITE_SEM_NAME, O_CREAT, mode, 1);
    if (shm_adt->writer_in == SEM_FAILED) {
      closeShm(shm_adt, true);
      return NULL;
    }
  } else {
    shm_adt->sem = sem_open(shm_adt->sem_name, 0);
    if (shm_adt->sem == SEM_FAILED) {
      closeShm(shm_adt, false);
      return NULL;
    }
    shm_adt->writer_in = sem_open(shm_adt->sem_name, 0);
    if (shm_adt->writer_in == SEM_FAILED) {
      closeShm(shm_adt, false);
      return NULL;
    }
  }

  return shm_adt;
}

ssize_t readShm(shmADT shared, char *buf, size_t count) {
  if (sem_wait(shared->sem) == -1) {
    return -1;
  }

  ssize_t i = 0;
  while (shared->shm->r_pointer < shared->shm->w_pointer && i < count &&
         shared->shm->buf[shared->shm->r_pointer] != '\0') {
    buf[i++] = shared->shm->buf[shared->shm->r_pointer++];
  }

  if (shared->shm->w_pointer ==
      0) { // This should not happen with a correctly sized MAX_LENGTH.
    shared->shm->r_pointer = 0;
  } else if (shared->shm->buf[shared->shm->r_pointer] == '\0') {
    shared->shm->r_pointer++;
  }

  if (i < count) {
    buf[i] = '\0';
  }

  return i;
}

ssize_t writeShm(shmADT shared, const char *buf, size_t count) {
  ssize_t i = 0;
  while (shared->shm->w_pointer < MAX_LENGTH && i < count && buf[i] != '\0') {
    shared->shm->buf[shared->shm->w_pointer++] = buf[i++];
  }

  if (shared->shm->w_pointer < MAX_LENGTH)
    shared->shm->buf[shared->shm->w_pointer++] = '\0';

  if (shared->shm->w_pointer ==
      MAX_LENGTH) { /* This can be avoided with a set buffer length of 2^N
                       and a circular buffer with 2^N sized w & r pointers. */
    shared->shm->w_pointer = 0;
    errno = ENOMEM;
    return -1;
  }

  if (sem_post(shared->sem) == -1) {
    return -1;
  }

  return i;
}

int closeShm(shmADT shared, bool creator) {
  close(shared->shm_fd);
  munmap(shared->shm, sizeof(shmCDT));

  if (creator) {
    sem_unlink(shared->sem_name);
    shm_unlink(shared->shm_name);
  }

  sem_close(shared->sem);
  sem_close(shared->writer_in);
  
  free(shared);
  return 0;
}
