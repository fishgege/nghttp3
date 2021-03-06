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
#include "nghttp3_conn_test.h"

#include <CUnit/CUnit.h>

#include "nghttp3_conn.h"
#include "nghttp3_macro.h"
#include "nghttp3_conv.h"
#include "nghttp3_frame.h"
#include "nghttp3_vec.h"
#include "nghttp3_test_helper.h"

static uint8_t nulldata[4096];

typedef struct {
  struct {
    size_t left;
    size_t step;
  } data;
  struct {
    size_t acc;
  } ack;
} userdata;

static int acked_stream_data(nghttp3_conn *conn, int64_t stream_id,
                             size_t datalen, void *user_data,
                             void *stream_user_data) {
  userdata *ud = user_data;

  (void)conn;
  (void)stream_id;
  (void)stream_user_data;

  ud->ack.acc += datalen;

  return 0;
}

static int begin_headers(nghttp3_conn *conn, int64_t stream_id, void *user_data,
                         void *stream_user_data) {
  (void)conn;
  (void)stream_id;
  (void)stream_user_data;
  (void)user_data;
  return 0;
}

static int recv_header(nghttp3_conn *conn, int64_t stream_id, int32_t token,
                       nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags,
                       void *user_data, void *stream_user_data) {
  (void)conn;
  (void)stream_id;
  (void)token;
  (void)name;
  (void)value;
  (void)flags;
  (void)stream_user_data;
  (void)user_data;
  return 0;
}

static int end_headers(nghttp3_conn *conn, int64_t stream_id, void *user_data,
                       void *stream_user_data) {
  (void)conn;
  (void)stream_id;
  (void)stream_user_data;
  (void)user_data;
  return 0;
}

static int step_read_data(nghttp3_conn *conn, int64_t stream_id,
                          const uint8_t **pdata, size_t *pdatalen,
                          uint32_t *pflags, void *user_data,
                          void *stream_user_data) {
  userdata *ud = user_data;
  size_t n = nghttp3_min(ud->data.left, ud->data.step);

  (void)conn;
  (void)stream_id;
  (void)stream_user_data;

  ud->data.left -= n;
  if (ud->data.left == 0) {
    *pflags = NGHTTP3_DATA_FLAG_EOF;
  }

  *pdata = nulldata;
  *pdatalen = n;

  return 0;
}

void test_nghttp3_conn_read_control(void) {
  const nghttp3_mem *mem = nghttp3_mem_default();
  nghttp3_conn *conn;
  nghttp3_conn_callbacks callbacks;
  nghttp3_conn_settings settings;
  int rv;
  uint8_t rawbuf[2048];
  nghttp3_buf buf;
  struct {
    nghttp3_frame_settings settings;
    nghttp3_settings_entry iv[15];
  } fr;
  ssize_t nconsumed;
  nghttp3_settings_entry *iv;
  size_t i;

  memset(&callbacks, 0, sizeof(callbacks));
  nghttp3_conn_settings_default(&settings);
  nghttp3_buf_wrap_init(&buf, rawbuf, sizeof(rawbuf));

  buf.last = nghttp3_put_varint(buf.last, NGHTTP3_STREAM_TYPE_CONTROL);

  fr.settings.hd.type = NGHTTP3_FRAME_SETTINGS;
  iv = fr.settings.iv;
  iv[0].id = NGHTTP3_SETTINGS_ID_MAX_HEADER_LIST_SIZE;
  iv[0].value = 65536;
  iv[1].id = 1000000009;
  iv[1].value = 1000000007;
  iv[2].id = NGHTTP3_SETTINGS_ID_QPACK_MAX_TABLE_CAPACITY;
  iv[2].value = 4096;
  iv[3].id = NGHTTP3_SETTINGS_ID_QPACK_BLOCKED_STREAMS;
  iv[3].value = 99;
  fr.settings.niv = 4;

  nghttp3_write_frame(&buf, (nghttp3_frame *)&fr);

  rv = nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);

  CU_ASSERT(0 == rv);

  nconsumed = nghttp3_conn_read_stream(conn, 2, buf.pos, nghttp3_buf_len(&buf),
                                       /* fin = */ 0);

  CU_ASSERT(nconsumed == (ssize_t)nghttp3_buf_len(&buf));
  CU_ASSERT(65536 == conn->remote.settings.max_header_list_size);
  CU_ASSERT(0 == conn->remote.settings.num_placeholders);
  CU_ASSERT(4096 == conn->remote.settings.qpack_max_table_capacity);
  CU_ASSERT(99 == conn->remote.settings.qpack_blocked_streams);

  nghttp3_conn_del(conn);

  /* Feed 1 byte at a time to verify that state machine works */
  rv = nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);

  CU_ASSERT(0 == rv);

  for (i = 0; i < nghttp3_buf_len(&buf); ++i) {
    nconsumed =
        nghttp3_conn_read_stream(conn, 2, buf.pos + i, 1, /* fin = */ 0);

    CU_ASSERT(1 == nconsumed);
  }

  CU_ASSERT(65536 == conn->remote.settings.max_header_list_size);
  CU_ASSERT(0 == conn->remote.settings.num_placeholders);
  CU_ASSERT(4096 == conn->remote.settings.qpack_max_table_capacity);
  CU_ASSERT(99 == conn->remote.settings.qpack_blocked_streams);

  nghttp3_conn_del(conn);
}

void test_nghttp3_conn_write_control(void) {
  const nghttp3_mem *mem = nghttp3_mem_default();
  nghttp3_conn *conn;
  nghttp3_conn_callbacks callbacks;
  nghttp3_conn_settings settings;
  nghttp3_vec vec[256];
  ssize_t sveccnt;
  int rv;
  int64_t stream_id;
  int fin;

  memset(&callbacks, 0, sizeof(callbacks));
  nghttp3_conn_settings_default(&settings);

  rv = nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);

  CU_ASSERT(0 == rv);

  rv = nghttp3_conn_bind_control_stream(conn, 3);

  CU_ASSERT(0 == rv);
  CU_ASSERT(NULL != conn->tx.ctrl);
  CU_ASSERT(NGHTTP3_STREAM_TYPE_CONTROL == conn->tx.ctrl->type);

  sveccnt = nghttp3_conn_writev_stream(conn, &stream_id, &fin, vec,
                                       nghttp3_arraylen(vec));

  CU_ASSERT(3 == stream_id);
  CU_ASSERT(1 == sveccnt);
  CU_ASSERT(vec[0].len > 1);
  CU_ASSERT(NGHTTP3_STREAM_TYPE_CONTROL == vec[0].base[0]);

  nghttp3_conn_del(conn);
}

void test_nghttp3_conn_submit_request(void) {
  const nghttp3_mem *mem = nghttp3_mem_default();
  nghttp3_conn *conn;
  nghttp3_conn_callbacks callbacks;
  nghttp3_conn_settings settings;
  nghttp3_vec vec[256];
  ssize_t sveccnt;
  int rv;
  int64_t stream_id;
  const nghttp3_nv nva[] = {
      MAKE_NV(":path", "/"),
      MAKE_NV(":authority", "example.com"),
      MAKE_NV(":scheme", "https"),
      MAKE_NV(":method", "GET"),
  };
  size_t len;
  size_t i;
  nghttp3_stream *stream;
  userdata ud;
  nghttp3_data_reader dr;
  int fin;
  nghttp3_priority pri;

  memset(&callbacks, 0, sizeof(callbacks));
  memset(&ud, 0, sizeof(ud));
  nghttp3_conn_settings_default(&settings);

  callbacks.acked_stream_data = acked_stream_data;

  ud.data.left = 2000;
  ud.data.step = 1200;

  rv = nghttp3_conn_client_new(&conn, &callbacks, &settings, mem, &ud);

  CU_ASSERT(0 == rv);

  rv = nghttp3_conn_bind_qpack_streams(conn, 6, 10);

  CU_ASSERT(0 == rv);
  CU_ASSERT(NULL != conn->tx.qenc);
  CU_ASSERT(NGHTTP3_STREAM_TYPE_QPACK_ENCODER == conn->tx.qenc->type);
  CU_ASSERT(NULL != conn->tx.qdec);
  CU_ASSERT(NGHTTP3_STREAM_TYPE_QPACK_DECODER == conn->tx.qdec->type);

  dr.read_data = step_read_data;
  rv = nghttp3_conn_submit_request(
      conn, 0, nghttp3_priority_init(&pri, NGHTTP3_ELEM_DEP_TYPE_ROOT, 0, 256),
      nva, nghttp3_arraylen(nva), &dr, NULL);

  CU_ASSERT(0 == rv);

  /* This will write QPACK decoder stream; just stream type */
  sveccnt = nghttp3_conn_writev_stream(conn, &stream_id, &fin, vec,
                                       nghttp3_arraylen(vec));

  CU_ASSERT(10 == stream_id);
  CU_ASSERT(1 == sveccnt);
  CU_ASSERT(1 == nghttp3_ringbuf_len(&conn->tx.qdec->outq));
  CU_ASSERT(0 == conn->tx.qdec->outq_idx);
  CU_ASSERT(0 == conn->tx.qdec->outq_offset);

  /* Calling twice will return the same result */
  sveccnt = nghttp3_conn_writev_stream(conn, &stream_id, &fin, vec,
                                       nghttp3_arraylen(vec));

  CU_ASSERT(10 == stream_id);
  CU_ASSERT(1 == sveccnt);

  rv = nghttp3_conn_add_write_offset(conn, 10, vec[0].len);

  CU_ASSERT(0 == rv);
  CU_ASSERT(1 == nghttp3_ringbuf_len(&conn->tx.qdec->outq));
  CU_ASSERT(0 == conn->tx.qdec->outq_idx);
  CU_ASSERT(vec[0].len == conn->tx.qdec->outq_offset);

  rv = nghttp3_conn_add_ack_offset(conn, 10, vec[0].len);

  CU_ASSERT(0 == rv);
  CU_ASSERT(0 == nghttp3_ringbuf_len(&conn->tx.qdec->outq));
  CU_ASSERT(0 == conn->tx.qdec->outq_idx);
  CU_ASSERT(0 == conn->tx.qdec->outq_offset);
  CU_ASSERT(0 == conn->tx.qdec->ack_offset);

  /* This will write QPACK encoder stream; just stream type */
  sveccnt = nghttp3_conn_writev_stream(conn, &stream_id, &fin, vec,
                                       nghttp3_arraylen(vec));

  CU_ASSERT(6 == stream_id);
  CU_ASSERT(1 == sveccnt);

  rv = nghttp3_conn_add_write_offset(conn, 6, vec[0].len);

  CU_ASSERT(0 == rv);

  rv = nghttp3_conn_add_ack_offset(conn, 6, vec[0].len);

  CU_ASSERT(0 == rv);

  /* This will write request stream */
  sveccnt = nghttp3_conn_writev_stream(conn, &stream_id, &fin, vec,
                                       nghttp3_arraylen(vec));

  CU_ASSERT(0 == stream_id);
  CU_ASSERT(7 == sveccnt);

  len = nghttp3_vec_len(vec, (size_t)sveccnt);

  for (i = 0; i < len; ++i) {
    rv = nghttp3_conn_add_write_offset(conn, 0, 1);

    CU_ASSERT(0 == rv);

    rv = nghttp3_conn_add_ack_offset(conn, 0, 1);

    CU_ASSERT(0 == rv);
  }

  stream = nghttp3_conn_find_stream(conn, 0);

  CU_ASSERT(0 == nghttp3_ringbuf_len(&stream->outq));
  CU_ASSERT(0 == nghttp3_ringbuf_len(&stream->chunks));
  CU_ASSERT(0 == stream->outq_idx);
  CU_ASSERT(0 == stream->outq_offset);
  CU_ASSERT(0 == stream->ack_offset);
  CU_ASSERT(2000 == ud.ack.acc);

  nghttp3_conn_del(conn);
}

static void conn_read_write(nghttp3_conn *cl, nghttp3_conn *sv) {
  nghttp3_vec vec[256];
  ssize_t sveccnt;
  ssize_t sconsumed;
  int64_t stream_id;
  int fin;
  int rv;
  size_t i, j;
  nghttp3_conn *sender = cl, *receiver = sv;

  for (j = 0; j < 2; ++j) {
    for (;;) {
      sveccnt = nghttp3_conn_writev_stream(sender, &stream_id, &fin, vec,
                                           nghttp3_arraylen(vec));

      CU_ASSERT(sveccnt >= 0);

      if (sveccnt <= 0) {
        break;
      }

      rv = nghttp3_conn_add_write_offset(sender, stream_id,
                                         nghttp3_vec_len(vec, (size_t)sveccnt));

      CU_ASSERT(0 == rv);

      for (i = 0; i < (size_t)sveccnt; ++i) {
        sconsumed = nghttp3_conn_read_stream(receiver, stream_id, vec[i].base,
                                             vec[i].len,
                                             fin && i == (size_t)sveccnt - 1);
        CU_ASSERT(sconsumed >= 0);
      }

      rv = nghttp3_conn_add_ack_offset(sender, stream_id,
                                       nghttp3_vec_len(vec, (size_t)sveccnt));

      CU_ASSERT(0 == rv);
    }

    sender = sv;
    receiver = cl;
  }
}

void test_nghttp3_conn_submit_priority(void) {
  const nghttp3_mem *mem = nghttp3_mem_default();
  nghttp3_conn *cl, *sv;
  nghttp3_conn_callbacks callbacks;
  nghttp3_conn_settings settings;
  int rv;
  nghttp3_placeholder *ph;

  memset(&callbacks, 0, sizeof(callbacks));
  nghttp3_conn_settings_default(&settings);

  nghttp3_conn_client_new(&cl, &callbacks, &settings, mem, NULL);

  settings.num_placeholders = 10;
  nghttp3_conn_server_new(&sv, &callbacks, &settings, mem, NULL);

  nghttp3_conn_set_max_client_streams_bidi(sv, 100);

  nghttp3_conn_bind_control_stream(cl, 2);
  nghttp3_conn_bind_control_stream(sv, 3);

  nghttp3_conn_bind_qpack_streams(cl, 6, 10);
  nghttp3_conn_bind_qpack_streams(sv, 7, 11);

  conn_read_write(cl, sv);

  rv = nghttp3_conn_submit_priority(cl, NGHTTP3_PRI_ELEM_TYPE_PLACEHOLDER, 7,
                                    NGHTTP3_ELEM_DEP_TYPE_ROOT, 0, 249);

  CU_ASSERT(0 == rv);

  conn_read_write(cl, sv);

  ph = nghttp3_conn_find_placeholder(sv, 7);

  CU_ASSERT(&sv->root == ph->node.parent);
  CU_ASSERT(249 == ph->node.weight);

  nghttp3_conn_del(sv);
  nghttp3_conn_del(cl);
}

void test_nghttp3_conn_http_request(void) {
  const nghttp3_mem *mem = nghttp3_mem_default();
  nghttp3_conn *cl, *sv;
  nghttp3_conn_callbacks callbacks;
  nghttp3_conn_settings settings;
  nghttp3_vec vec[256];
  ssize_t sveccnt;
  ssize_t sconsumed;
  int rv;
  int64_t stream_id;
  const nghttp3_nv reqnva[] = {
      MAKE_NV(":path", "/"),
      MAKE_NV(":authority", "example.com"),
      MAKE_NV(":scheme", "https"),
      MAKE_NV(":method", "GET"),
  };
  const nghttp3_nv respnva[] = {
      MAKE_NV(":status", "200"),
      MAKE_NV("server", "nghttp3"),
      MAKE_NV("content-length", "1999"),
  };
  nghttp3_data_reader dr;
  int fin;
  userdata clud, svud;
  size_t i;

  memset(&callbacks, 0, sizeof(callbacks));
  nghttp3_conn_settings_default(&settings);
  memset(&clud, 0, sizeof(clud));

  callbacks.begin_headers = begin_headers;
  callbacks.recv_header = recv_header;
  callbacks.end_headers = end_headers;

  settings.qpack_max_table_capacity = 4096;
  settings.qpack_blocked_streams = 100;

  clud.data.left = 2000;
  clud.data.step = 1200;

  svud.data.left = 1999;
  svud.data.step = 1000;

  nghttp3_conn_client_new(&cl, &callbacks, &settings, mem, &clud);
  nghttp3_conn_server_new(&sv, &callbacks, &settings, mem, &svud);

  nghttp3_conn_bind_control_stream(cl, 2);
  nghttp3_conn_bind_control_stream(sv, 3);

  nghttp3_conn_bind_qpack_streams(cl, 6, 10);
  nghttp3_conn_bind_qpack_streams(sv, 7, 11);

  dr.read_data = step_read_data;
  rv = nghttp3_conn_submit_request(cl, 0, NULL, reqnva,
                                   nghttp3_arraylen(reqnva), &dr, NULL);

  CU_ASSERT(0 == rv);

  rv = nghttp3_conn_end_stream(cl, 0);

  CU_ASSERT(0 == rv);

  for (;;) {
    sveccnt = nghttp3_conn_writev_stream(cl, &stream_id, &fin, vec,
                                         nghttp3_arraylen(vec));

    CU_ASSERT(sveccnt >= 0);

    if (sveccnt <= 0) {
      break;
    }

    rv = nghttp3_conn_add_write_offset(cl, stream_id,
                                       nghttp3_vec_len(vec, (size_t)sveccnt));

    CU_ASSERT(0 == rv);

    for (i = 0; i < (size_t)sveccnt; ++i) {
      sconsumed =
          nghttp3_conn_read_stream(sv, stream_id, vec[i].base, vec[i].len,
                                   fin && i == (size_t)sveccnt - 1);
      CU_ASSERT(sconsumed >= 0);
    }

    rv = nghttp3_conn_add_ack_offset(cl, stream_id,
                                     nghttp3_vec_len(vec, (size_t)sveccnt));

    CU_ASSERT(0 == rv);
  }

  rv = nghttp3_conn_submit_response(sv, 0, respnva, nghttp3_arraylen(respnva),
                                    &dr);

  CU_ASSERT(0 == rv);

  rv = nghttp3_conn_end_stream(sv, 0);

  CU_ASSERT(0 == rv);

  for (;;) {
    sveccnt = nghttp3_conn_writev_stream(sv, &stream_id, &fin, vec,
                                         nghttp3_arraylen(vec));

    CU_ASSERT(sveccnt >= 0);

    if (sveccnt <= 0) {
      break;
    }

    rv = nghttp3_conn_add_write_offset(sv, stream_id,
                                       nghttp3_vec_len(vec, (size_t)sveccnt));

    CU_ASSERT(0 == rv);

    for (i = 0; i < (size_t)sveccnt; ++i) {
      sconsumed =
          nghttp3_conn_read_stream(cl, stream_id, vec[i].base, vec[i].len,
                                   fin && i == (size_t)sveccnt - 1);
      CU_ASSERT(sconsumed >= 0);
    }

    rv = nghttp3_conn_add_ack_offset(sv, stream_id,
                                     nghttp3_vec_len(vec, (size_t)sveccnt));

    CU_ASSERT(0 == rv);
  }

  nghttp3_conn_del(sv);
  nghttp3_conn_del(cl);
}

void test_nghttp3_conn_recv_request_priority(void) {
  const nghttp3_mem *mem = nghttp3_mem_default();
  nghttp3_conn *conn;
  nghttp3_conn_callbacks callbacks;
  nghttp3_conn_settings settings;
  uint8_t rawbuf[256];
  nghttp3_buf buf;
  nghttp3_frame_priority fr;
  ssize_t sconsumed;
  nghttp3_stream *stream;
  nghttp3_tnode *parent;
  nghttp3_placeholder *ph;

  memset(&callbacks, 0, sizeof(callbacks));
  nghttp3_conn_settings_default(&settings);

  settings.num_placeholders = 2;

  /* Depends on stream which has not been created yet */
  nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);
  nghttp3_conn_set_max_client_streams_bidi(conn, 2);

  fr.hd.type = NGHTTP3_FRAME_PRIORITY;
  fr.pt = NGHTTP3_PRI_ELEM_TYPE_CURRENT;
  fr.dt = NGHTTP3_ELEM_DEP_TYPE_REQUEST;
  fr.elem_dep_id = 0;
  fr.weight = 199;

  nghttp3_buf_wrap_init(&buf, rawbuf, sizeof(rawbuf));
  nghttp3_write_frame(&buf, (nghttp3_frame *)&fr);

  sconsumed = nghttp3_conn_read_stream(conn, 4, buf.pos, nghttp3_buf_len(&buf),
                                       /* fin = */ 0);

  CU_ASSERT((ssize_t)nghttp3_buf_len(&buf) == sconsumed);

  stream = nghttp3_conn_find_stream(conn, 4);
  parent = stream->node.parent;

  CU_ASSERT(NGHTTP3_NODE_ID_TYPE_STREAM == parent->nid.type);
  CU_ASSERT(0 == parent->nid.id);
  CU_ASSERT(199 == stream->node.weight);
  CU_ASSERT(!nghttp3_tnode_is_scheduled(&stream->node));

  nghttp3_conn_del(conn);

  /* Making dependency to itself is not allowed */
  nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);

  fr.hd.type = NGHTTP3_FRAME_PRIORITY;
  fr.pt = NGHTTP3_PRI_ELEM_TYPE_CURRENT;
  fr.dt = NGHTTP3_ELEM_DEP_TYPE_REQUEST;
  fr.elem_dep_id = 0;
  fr.weight = 199;

  nghttp3_buf_wrap_init(&buf, rawbuf, sizeof(rawbuf));
  nghttp3_write_frame(&buf, (nghttp3_frame *)&fr);

  sconsumed = nghttp3_conn_read_stream(conn, 0, buf.pos, nghttp3_buf_len(&buf),
                                       /* fin = */ 0);

  CU_ASSERT(NGHTTP3_ERR_HTTP_MALFORMED_FRAME - NGHTTP3_FRAME_PRIORITY ==
            sconsumed);

  nghttp3_conn_del(conn);

  /* Depends on placeholder which has not been created yet */
  nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);

  fr.hd.type = NGHTTP3_FRAME_PRIORITY;
  fr.pt = NGHTTP3_PRI_ELEM_TYPE_CURRENT;
  fr.dt = NGHTTP3_ELEM_DEP_TYPE_PLACEHOLDER;
  fr.elem_dep_id = 1;
  fr.weight = 99;

  nghttp3_buf_wrap_init(&buf, rawbuf, sizeof(rawbuf));
  nghttp3_write_frame(&buf, (nghttp3_frame *)&fr);

  sconsumed = nghttp3_conn_read_stream(conn, 0, buf.pos, nghttp3_buf_len(&buf),
                                       /* fin = */ 0);

  CU_ASSERT((ssize_t)nghttp3_buf_len(&buf) == sconsumed);

  ph = nghttp3_conn_find_placeholder(conn, 1);

  CU_ASSERT(NULL != ph);
  CU_ASSERT(NGHTTP3_DEFAULT_WEIGHT == ph->node.weight);
  CU_ASSERT(!nghttp3_tnode_is_scheduled(&ph->node));

  stream = nghttp3_conn_find_stream(conn, 0);

  CU_ASSERT(&ph->node == stream->node.parent);
  CU_ASSERT(99 == stream->node.weight);
  CU_ASSERT(!nghttp3_tnode_is_scheduled(&stream->node));

  nghttp3_conn_del(conn);
}

void test_nghttp3_conn_recv_control_priority(void) {
  const nghttp3_mem *mem = nghttp3_mem_default();
  nghttp3_conn *conn;
  nghttp3_conn_callbacks callbacks;
  nghttp3_conn_settings settings;
  uint8_t rawbuf[256];
  nghttp3_buf buf;
  nghttp3_frame fr;
  ssize_t sconsumed;
  nghttp3_stream *stream;
  nghttp3_tnode *parent;
  nghttp3_placeholder *ph;

  memset(&callbacks, 0, sizeof(callbacks));
  nghttp3_conn_settings_default(&settings);

  settings.num_placeholders = 2;

  /* Making dependency between 2 streams which have not been
     created */
  nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);
  nghttp3_conn_set_max_client_streams_bidi(conn, 2);

  nghttp3_buf_wrap_init(&buf, rawbuf, sizeof(rawbuf));

  buf.last = nghttp3_put_varint(buf.last, NGHTTP3_STREAM_TYPE_CONTROL);

  fr.hd.type = NGHTTP3_FRAME_SETTINGS;
  fr.settings.niv = 0;

  nghttp3_write_frame(&buf, &fr);

  fr.hd.type = NGHTTP3_FRAME_PRIORITY;
  fr.priority.pt = NGHTTP3_PRI_ELEM_TYPE_REQUEST;
  fr.priority.dt = NGHTTP3_ELEM_DEP_TYPE_REQUEST;
  fr.priority.pri_elem_id = 0;
  fr.priority.elem_dep_id = 4;
  fr.priority.weight = 111;

  nghttp3_write_frame(&buf, &fr);

  sconsumed = nghttp3_conn_read_stream(conn, 2, buf.pos, nghttp3_buf_len(&buf),
                                       /* fin = */ 0);

  CU_ASSERT((ssize_t)nghttp3_buf_len(&buf) == sconsumed);

  stream = nghttp3_conn_find_stream(conn, 0);
  parent = stream->node.parent;

  CU_ASSERT(111 == stream->node.weight);
  CU_ASSERT(NGHTTP3_NODE_ID_TYPE_STREAM == parent->nid.type);
  CU_ASSERT(4 == parent->nid.id);
  CU_ASSERT(&conn->root == parent->parent);
  CU_ASSERT(NGHTTP3_DEFAULT_WEIGHT == parent->weight);

  nghttp3_conn_del(conn);

  /* Making dependency between 2 placeholders which have not been
     created */
  nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);

  nghttp3_buf_wrap_init(&buf, rawbuf, sizeof(rawbuf));

  buf.last = nghttp3_put_varint(buf.last, NGHTTP3_STREAM_TYPE_CONTROL);

  fr.hd.type = NGHTTP3_FRAME_SETTINGS;
  fr.settings.niv = 0;

  nghttp3_write_frame(&buf, &fr);

  fr.hd.type = NGHTTP3_FRAME_PRIORITY;
  fr.priority.pt = NGHTTP3_PRI_ELEM_TYPE_PLACEHOLDER;
  fr.priority.dt = NGHTTP3_ELEM_DEP_TYPE_PLACEHOLDER;
  fr.priority.pri_elem_id = 1;
  fr.priority.elem_dep_id = 0;
  fr.priority.weight = 256;

  nghttp3_write_frame(&buf, &fr);

  sconsumed = nghttp3_conn_read_stream(conn, 2, buf.pos, nghttp3_buf_len(&buf),
                                       /* fin = */ 0);

  CU_ASSERT((ssize_t)nghttp3_buf_len(&buf) == sconsumed);

  ph = nghttp3_conn_find_placeholder(conn, 1);
  parent = ph->node.parent;

  CU_ASSERT(256 == ph->node.weight);
  CU_ASSERT(NGHTTP3_NODE_ID_TYPE_PLACEHOLDER == parent->nid.type);
  CU_ASSERT(0 == parent->nid.id);
  CU_ASSERT(&conn->root == parent->parent);
  CU_ASSERT(NGHTTP3_DEFAULT_WEIGHT == parent->weight);

  nghttp3_conn_del(conn);

  /* Making dependency to its descendant */
  nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);
  nghttp3_conn_set_max_client_streams_bidi(conn, 2);

  nghttp3_buf_wrap_init(&buf, rawbuf, sizeof(rawbuf));

  buf.last = nghttp3_put_varint(buf.last, NGHTTP3_STREAM_TYPE_CONTROL);

  fr.hd.type = NGHTTP3_FRAME_SETTINGS;
  fr.settings.niv = 0;

  nghttp3_write_frame(&buf, &fr);

  fr.hd.type = NGHTTP3_FRAME_PRIORITY;
  fr.priority.pt = NGHTTP3_PRI_ELEM_TYPE_REQUEST;
  fr.priority.dt = NGHTTP3_ELEM_DEP_TYPE_REQUEST;
  fr.priority.pri_elem_id = 0;
  fr.priority.elem_dep_id = 4;
  fr.priority.weight = 111;

  nghttp3_write_frame(&buf, &fr);

  fr.hd.type = NGHTTP3_FRAME_PRIORITY;
  fr.priority.pt = NGHTTP3_PRI_ELEM_TYPE_REQUEST;
  fr.priority.dt = NGHTTP3_ELEM_DEP_TYPE_REQUEST;
  fr.priority.pri_elem_id = 4;
  fr.priority.elem_dep_id = 0;
  fr.priority.weight = 12;

  nghttp3_write_frame(&buf, &fr);

  sconsumed = nghttp3_conn_read_stream(conn, 2, buf.pos, nghttp3_buf_len(&buf),
                                       /* fin = */ 0);

  CU_ASSERT((ssize_t)nghttp3_buf_len(&buf) == sconsumed);

  stream = nghttp3_conn_find_stream(conn, 4);
  parent = stream->node.parent;

  CU_ASSERT(12 == stream->node.weight);
  CU_ASSERT(NGHTTP3_NODE_ID_TYPE_STREAM == parent->nid.type);
  CU_ASSERT(0 == parent->nid.id);
  CU_ASSERT(&conn->root == parent->parent);
  CU_ASSERT(111 == parent->weight);

  nghttp3_conn_del(conn);

  /* Making dependency between stream and root */
  nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);
  nghttp3_conn_set_max_client_streams_bidi(conn, 100);

  nghttp3_buf_wrap_init(&buf, rawbuf, sizeof(rawbuf));

  buf.last = nghttp3_put_varint(buf.last, NGHTTP3_STREAM_TYPE_CONTROL);

  fr.hd.type = NGHTTP3_FRAME_SETTINGS;
  fr.settings.niv = 0;

  nghttp3_write_frame(&buf, &fr);

  fr.hd.type = NGHTTP3_FRAME_PRIORITY;
  fr.priority.pt = NGHTTP3_PRI_ELEM_TYPE_REQUEST;
  fr.priority.dt = NGHTTP3_ELEM_DEP_TYPE_ROOT;
  fr.priority.pri_elem_id = 100;
  fr.priority.weight = 111;

  nghttp3_write_frame(&buf, &fr);

  sconsumed = nghttp3_conn_read_stream(conn, 2, buf.pos, nghttp3_buf_len(&buf),
                                       /* fin = */ 0);

  CU_ASSERT((ssize_t)nghttp3_buf_len(&buf) == sconsumed);

  stream = nghttp3_conn_find_stream(conn, 100);
  parent = stream->node.parent;

  CU_ASSERT(111 == stream->node.weight);
  CU_ASSERT(&conn->root == parent);

  nghttp3_conn_del(conn);

  /* Using NGHTTP3_PRI_ELEM_TYPE_CURRENT is an error */
  nghttp3_conn_server_new(&conn, &callbacks, &settings, mem, NULL);
  nghttp3_conn_set_max_client_streams_bidi(conn, 100);

  nghttp3_buf_wrap_init(&buf, rawbuf, sizeof(rawbuf));

  buf.last = nghttp3_put_varint(buf.last, NGHTTP3_STREAM_TYPE_CONTROL);

  fr.hd.type = NGHTTP3_FRAME_SETTINGS;
  fr.settings.niv = 0;

  nghttp3_write_frame(&buf, &fr);

  fr.hd.type = NGHTTP3_FRAME_PRIORITY;
  fr.priority.pt = NGHTTP3_PRI_ELEM_TYPE_CURRENT;
  fr.priority.dt = NGHTTP3_ELEM_DEP_TYPE_ROOT;
  fr.priority.weight = 1;

  nghttp3_write_frame(&buf, &fr);

  sconsumed = nghttp3_conn_read_stream(conn, 2, buf.pos, nghttp3_buf_len(&buf),
                                       /* fin = */ 0);

  CU_ASSERT(NGHTTP3_ERR_HTTP_MALFORMED_FRAME - NGHTTP3_FRAME_PRIORITY ==
            sconsumed);

  nghttp3_conn_del(conn);
}
