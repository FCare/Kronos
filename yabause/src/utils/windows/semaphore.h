#pragma once

/*
    Copyright (c) 2011, Dongsheng Song <songdongsheng@live.cn>

    Licensed to the Apache Software Foundation (ASF) under one or more
    contributor license agreements.  See the NOTICE file distributed with
    this work for additional information regarding copyright ownership.
    The ASF licenses this file to You under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with
    the License.  You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/*
    Simple Windows replacement for POSIX semaphores
    Modified by Daniel Tillett from libpthread <http://github.com/songdongsheng/libpthread>
    Copyright (c) 2015, Daniel Tillett <daniel.tillett @ gmail.com>
*/

#ifndef _SEMAPHORE_H_
#define _SEMAPHORE_H_

#include <errno.h> 
#include <fcntl.h>
#include <stdio.h>
#include <winsock.h>

#define PTHREAD_PROCESS_PRIVATE	0
#define PTHREAD_PROCESS_SHARED	1

#define _POSIX_SEMAPHORES       200809L

#define SEM_VALUE_MAX           INT_MAX

#define SEM_FAILED              NULL

#ifdef __cplusplus
extern "C" {
#endif

typedef void  *sem_t;

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout);
int sem_post(sem_t *sem);
int sem_getvalue(sem_t *sem, int *value);
int sem_destroy(sem_t *sem);
sem_t *sem_open(const char *name, int oflag, unsigned short mode /*refer to mode_t in posix*/, unsigned int value);
int sem_close(sem_t *sem);
int sem_unlink(const char *name);

#ifdef __cplusplus
	}
#endif

#endif
