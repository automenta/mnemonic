/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <common.h>

#define TRANSTABLEITEMLEN 3
#define FRAMESITEMLEN 4

/******************************************************************************
 ** Generally-useful functions for JNI programming.
 *****************************************************************************/

/**
 *  Throws a RuntimeException, with either an explicit message or the message
 *  corresponding to the current system error value.
 */
void throw(JNIEnv* env, const char* msg) {
  if (msg == NULL)
    msg = sys_errlist[errno];

  jclass xklass = (*env)->FindClass(env, "java/lang/RuntimeException");
  (*env)->ThrowNew(env, xklass, msg);
}

void* addr_from_java(jlong addr) {
  // This assert fails in a variety of ways on 32-bit systems.
  // It is impossible to predict whether native code that converts
  // pointers to longs will sign-extend or zero-extend the addresses.
  //assert(addr == (uintptr_t)addr, "must not be odd high bits");
  return (void*) (uintptr_t) addr;
}

jlong addr_to_java(void* p) {
  assert(p == (void*) (uintptr_t) p);
  return (long) (uintptr_t) p;
}

void destructNValInfo(struct NValueInfo *nvinfo) {
  if (NULL != nvinfo) {
    if (NULL != nvinfo->transtable) {
      free(nvinfo->transtable);
    }
    if (NULL != nvinfo->frames) {
      free(nvinfo->frames);
    }
    free(nvinfo);
  }
}

struct NValueInfo *constructNValInfo(JNIEnv* env, jobject vinfoobj) {
  struct NValueInfo *ret = NULL;

  static int inited = 0;
  static jclass vinfocls = NULL, dutenum = NULL;
  static jfieldID handler_fid = NULL, transtable_fid = NULL, frames_fid = NULL, dtype_fid = NULL;
  static jmethodID getval_mtd = NULL;

  if (NULL == vinfoobj) {
    return NULL;
  }

  if (0 == inited) {
    inited = 1;
    vinfocls = (*env)->FindClass(env, "org/apache/mnemonic/service/computingservice/ValueInfo");
    dutenum = (*env)->FindClass(env, "org/apache/mnemonic/DurableType");
    if (NULL != vinfocls && NULL != dutenum) {
      handler_fid = (*env)->GetFieldID(env, vinfocls, "handler", "J");
      transtable_fid = (*env)->GetFieldID(env, vinfocls, "transtable", "[[J");
      frames_fid = (*env)->GetFieldID(env, vinfocls, "frames", "[[J");
      dtype_fid = (*env)->GetFieldID(env, vinfocls, "dtype",
          "Lorg/apache/mnemonic/DurableType;");
      getval_mtd = (*env)->GetMethodID(env, dutenum, "getValue", "()I");
    } else {
      return NULL;
    }
  }

  if (NULL == vinfocls || NULL == handler_fid ||
      NULL == transtable_fid || NULL == frames_fid ||
      NULL == dtype_fid || NULL == getval_mtd) {
    return NULL;
  }

  ret = (struct NValueInfo *)calloc(1, sizeof(struct NValueInfo));
  if (NULL != ret) {
    jlongArray itmarr;
    jlong *itms;
    jsize itmarrlen, i;
    ret->handler = (*env)->GetLongField(env, vinfoobj, handler_fid);
    jobject dutobj = (*env)->GetObjectField(env, vinfoobj, dtype_fid);
    ret->dtype = (*env)->CallIntMethod(env, dutobj, getval_mtd);

    jobjectArray tbarr = (*env)->GetObjectField(env, vinfoobj, transtable_fid);
    jsize tbarrlen = (*env)->GetArrayLength(env, tbarr);
    if (NULL != tbarr && tbarrlen > 0){
      ret->transtable = (struct transitem *)calloc(
          tbarrlen, sizeof(struct transitem));
      if (NULL != ret->transtable) {
        ret->transtablesz = tbarrlen;
        for (i = 0; i < tbarrlen; ++i) {
          itmarr = (jlongArray)((*env)->GetObjectArrayElement(env, tbarr, i));
          itmarrlen = (*env)->GetArrayLength(env, itmarr);
          if (NULL != itmarr && TRANSTABLEITEMLEN == itmarrlen) {
            itms = (*env)->GetLongArrayElements(env, itmarr, NULL);
            (ret->transtable + i)->hdlbase = itms[0];
            (ret->transtable + i)->size = itms[1];
            (ret->transtable + i)->base = addr_from_java(itms[2]);
            (*env)->ReleaseLongArrayElements(env, itmarr, itms, JNI_ABORT);
          } else {
            return NULL;
          }
        }
      } else {
        destructNValInfo(ret);
        return NULL;
      }
    }

    jobjectArray fmarr = (*env)->GetObjectField(env, vinfoobj, frames_fid);
    jsize fmarrlen = (*env)->GetArrayLength(env, fmarr);
    if (NULL != fmarr && fmarrlen > 0){
      ret->frames = (struct frameitem *)calloc(
          fmarrlen, sizeof(struct frameitem));
      if (NULL != ret->frames) {
        ret->framessz = fmarrlen;
        for (i = 0; i < fmarrlen; ++i) {
          itmarr = (jlongArray)((*env)->GetObjectArrayElement(env, fmarr, i));
          itmarrlen = (*env)->GetArrayLength(env, itmarr);
          if (NULL != itmarr && FRAMESITEMLEN == itmarrlen) {
            itms = (*env)->GetLongArrayElements(env, itmarr, NULL);
            (ret->frames + i)->nextoff = itms[0];
            (ret->frames + i)->nextsz = itms[1];
            (ret->frames + i)->nlvloff = itms[2];
            (ret->frames + i)->nlvlsz = itms[3];
            (*env)->ReleaseLongArrayElements(env, itmarr, itms, JNI_ABORT);
          } else {
            return NULL;
          }
        }
      } else {
        destructNValInfo(ret);
        return NULL;
      }
    }
  } else {
    return NULL;
  }
  return ret;
}

struct NValueInfo **constructNValueInfos(JNIEnv* env,
    jobjectArray vinfos, size_t *sz) {
  if (NULL == sz) {
    return NULL;
  }
  size_t idx;
  struct NValueInfo **ret = NULL;
  struct NValueInfo *curnvi = NULL;
  jobject curviobj;

  *sz = (*env)->GetArrayLength(env, vinfos);
  if (0 >= *sz) {
    return NULL;
  }
  ret = (struct NValueInfo **)calloc(*sz, sizeof(struct NValueInfo*));
  if (NULL == ret) {
    return NULL;
  }

  for(idx = 0; idx < *sz; ++idx) {
    curviobj = (*env)->GetObjectArrayElement(env, vinfos, idx);
    if (NULL == curviobj) {
      continue;
    }
    curnvi = constructNValInfo(env, curviobj);
    if (NULL == curnvi) {
      destructNValueInfos(ret, *sz);
      return NULL;
    }
    *(ret + idx) = curnvi;
  }

  return ret;
}

void destructNValueInfos(struct NValueInfo **nvalinfos, size_t sz) {
  size_t idx;
  if (NULL != nvalinfos) {
    for(idx = 0; idx < sz; ++idx) {
      destructNValInfo(*(nvalinfos + idx));
    }
    free(nvalinfos);
  }
}

void printNValueInfos(struct NValueInfo **nvalinfos, size_t sz) {
  size_t idx, i;
  struct NValueInfo *itm;
  struct transitem *ttitm;
  struct frameitem *fitm;
  printf("\n--- Native ValueInfo List, Addr:%p, Size: %zu ---\n", nvalinfos, sz);
  if (NULL != nvalinfos) {
    for(idx = 0; idx < sz; ++idx) {
      itm = *(nvalinfos + idx);
      printf("** Item %2zu, Addr:%p ** \n", idx, itm);
      if (NULL != itm) {
        printf("Handler:%ld, DType Value:%d\n", itm->handler, itm->dtype);
        printf(">> TransTable: Addr:%p, Size:%zu <<\n", itm->transtable, itm->transtablesz);
        if (NULL != itm->transtable && itm->transtablesz > 0) {
          for (i = 0; i < itm->transtablesz; ++i) {
            ttitm = (itm->transtable + i);
            printf("%2zu)", i);
            if (NULL != ttitm) {
              printf("hdlbase:%ld, size:%ld, base:%p\n",
                  ttitm->hdlbase, ttitm->size, ttitm->base);
            } else {
              printf("NULL\n");
            }
          }
        } else {
          printf("NULL\n");
        }
        printf(">> Frames: Addr:%p, Size:%zu <<\n", itm->frames, itm->framessz);
        if (NULL != itm->frames && itm->framessz > 0) {
          for (i = 0; i < itm->framessz; ++i) {
            fitm = (itm->frames + i);
            printf("%2zu)", i);
            if (NULL != fitm) {
              printf("nextoff:%ld, nextsz:%ld, nlvloff:%ld, nlvlsz:%ld\n",
                  fitm->nextoff, fitm->nextsz, fitm->nlvloff, fitm->nlvlsz);
            } else {
              printf("NULL\n");
            }
          }
        } else {
          printf("NULL\n");
        }
      } else {
        printf("NULL\n");
      }
    }
  } else {
    printf("NULL\n");
  }
  printf("------------------------\n");
}

jlongArray constructJLongArray(JNIEnv* env, long arr[], size_t sz) {
  jlongArray ret = (*env)->NewLongArray(env, sz);
  if (NULL == ret) {
    return NULL;
  }
  (*env)->SetLongArrayRegion(env, ret, 0, sz, arr);
  return ret;
}

inline void *to_e(JNIEnv* env, struct NValueInfo *nvinfo, long p) {
  size_t i;
  struct transitem * ti;
  if (NULL != nvinfo || NULL != nvinfo->transtable) {
    for (i = 0; i < nvinfo->transtablesz; ++i) {
      ti = nvinfo->transtable + i;
      if (p >= ti->hdlbase && p < ti->size) {
        return ti->base + p;
      }
    }
    throw(env, "No item found in Translate Table.");
  } else {
    throw(env, "NValueInfo or Translate Table is NULL.");
  }
  return NULL;
}

inline long to_p(JNIEnv* env, struct NValueInfo *nvinfo, void *e) {
  size_t i;
  struct transitem * ti;
  if (NULL != nvinfo || NULL != nvinfo->transtable) {
    for (i = 0; i < nvinfo->transtablesz; ++i) {
      ti = nvinfo->transtable + i;
      if (e >= ti->base && e < ti->base + ti->size) {
        return e - ti->base;
      }
    }
    throw(env, "No item found in Translate Table.");
  } else {
    throw(env, "NValueInfo or Translate Table is NULL.");
  }
  return -1L;
}

void iterMatrix(JNIEnv* env, struct NValueInfo *nvinfo, size_t dims[], long hdls[],
    size_t dimidx, valueHandler valhandler) {
  void *addr = NULL;
  long curoff = (nvinfo->frames + dimidx)->nextoff;
  long curnloff = (nvinfo->frames + dimidx)->nlvloff;
  long curnlsz = (nvinfo->frames + dimidx)->nlvlsz;
  if (dimidx < nvinfo->framessz - 1) {
    while(0L != hdls[dimidx]) {
      hdls[dimidx + 1] = *(long*)(to_e(env, nvinfo, hdls[dimidx]) + curnloff);
      iterMatrix(env, nvinfo, dims, hdls, dimidx + 1, valhandler);
      hdls[dimidx] = *(long*)(to_e(env, nvinfo, hdls[dimidx]) + curoff);
      ++dims[dimidx];
    }
    dims[dimidx] = 0;
  } else {
    if (-1L != curoff) {
      while(0L != hdls[dimidx]) {
        addr = to_e(env, nvinfo, hdls[dimidx]) + curnloff;
        valhandler(env, dims, dimidx + 1, addr, curnlsz, nvinfo->dtype);
        hdls[dimidx] = *(long*)(to_e(env, nvinfo, hdls[dimidx]) + curoff);
        ++dims[dimidx];
      }
      dims[dimidx] = 0;
    } else {
      addr = to_e(env, nvinfo, hdls[dimidx]) + curnloff;
      valhandler(env, dims, dimidx, addr, curnlsz, nvinfo->dtype);
    }
  }
}

int handleValueInfo(JNIEnv* env, struct NValueInfo *nvinfo, valueHandler valhandler) {
  if (NULL == nvinfo->frames || 0 == nvinfo->framessz) {
    return -1;
  }
  size_t dims[nvinfo->framessz];
  long hdls[nvinfo->framessz];
  size_t i, di = 0;
  for (i = 0; i < nvinfo->framessz; ++i) {
    dims[i] = 0;
    hdls[i] = 0L;
  }
  hdls[di] = nvinfo->handler;
  iterMatrix(env, nvinfo, dims, hdls, 0, valhandler);
  return 0;
}

