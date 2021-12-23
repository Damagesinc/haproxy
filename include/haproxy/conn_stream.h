/*
 * include/haproxy/conn_stream.h
 * This file contains conn-stream function prototypes
 *
 * Copyright 2021 Christopher Faulet <cfaulet@haproxy.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, version 2.1
 * exclusively.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _HAPROXY_CONN_STREAM_H
#define _HAPROXY_CONN_STREAM_H

#include <haproxy/api.h>
#include <haproxy/connection.h>
#include <haproxy/conn_stream-t.h>
#include <haproxy/obj_type.h>
#include <haproxy/pool-t.h>

extern struct pool_head *pool_head_connstream;

#define IS_HTX_CS(cs)     (cs_conn(cs) && IS_HTX_CONN(cs_conn(cs)))

struct conn_stream *cs_new(struct connection *conn, void *target);
void cs_free(struct conn_stream *cs);

/*
 * Initializes all required fields for a new conn_strema.
 */
static inline void cs_init(struct conn_stream *cs, struct connection *conn)
{
	cs->obj_type = OBJ_TYPE_CS;
	cs->flags = CS_FL_NONE;
	cs->conn = conn;
	cs->ctx = conn;
}

/* Returns the conn from a cs. If cs is NULL, returns NULL */
static inline struct connection *cs_conn(const struct conn_stream *cs)
{
	return cs ? cs->conn : NULL;
}

/* Returns the mux of the connection from a cs if the endpoint is a
 * connection. Otherwise NULL is returned.
 */
static inline const struct mux_ops *cs_conn_mux(const struct conn_stream *cs)
{
	const struct connection *conn = cs_conn(cs);

	return (conn ? conn->mux : NULL);
}

/* Attaches a conn_stream to a data layer and sets the relevant callbacks */
static inline void cs_attach(struct conn_stream *cs, void *data, const struct data_cb *data_cb)
{
	cs->data_cb = data_cb;
	cs->data = data;
}

/* Detach the conn_stream from the connection, if any. If a mux owns the
 * connection ->detach() callback is called. Otherwise, it means the conn-stream
 * owns the connection. In this case the connection is closed and released. The
 * conn-stream is not released.
 */
static inline void cs_detach(struct conn_stream *cs)
{
	struct connection *conn;
	struct appctx *appctx;

	if ((conn = cs_conn(cs))) {
		if (conn->mux)
			conn->mux->detach(cs);
		else {
			/* It's too early to have a mux, let's just destroy
			 * the connection
			 */
			conn_stop_tracking(conn);
			conn_full_close(conn);
			if (conn->destroy_cb)
				conn->destroy_cb(conn);
			conn_free(conn);
		}
	}
	cs_init(cs, NULL);
}

/* Release a conn_stream */
static inline void cs_destroy(struct conn_stream *cs)
{
	cs_detach(cs);
	cs_free(cs);
}

static inline const char *cs_get_data_name(const struct conn_stream *cs)
{
	if (!cs || !cs->data_cb)
		return "NONE";
	return cs->data_cb->name;
}

/* shut read */
static inline void cs_shutr(struct conn_stream *cs, enum cs_shr_mode mode)
{
	const struct mux_ops *mux;

	if (!cs_conn(cs) || cs->flags & CS_FL_SHR)
		return;

	/* clean data-layer shutdown */
	mux = cs_conn_mux(cs);
	if (mux && mux->shutr)
		mux->shutr(cs, mode);
	cs->flags |= (mode == CS_SHR_DRAIN) ? CS_FL_SHRD : CS_FL_SHRR;
}

/* shut write */
static inline void cs_shutw(struct conn_stream *cs, enum cs_shw_mode mode)
{
	const struct mux_ops *mux;

	if (!cs_conn(cs) || cs->flags & CS_FL_SHW)
		return;

	/* clean data-layer shutdown */
	mux = cs_conn_mux(cs);
	if (mux && mux->shutw)
		mux->shutw(cs, mode);
	cs->flags |= (mode == CS_SHW_NORMAL) ? CS_FL_SHWN : CS_FL_SHWS;
}

/* completely close a conn_stream (but do not detach it) */
static inline void cs_close(struct conn_stream *cs)
{
	cs_shutw(cs, CS_SHW_SILENT);
	cs_shutr(cs, CS_SHR_RESET);
}

/* completely close a conn_stream after draining possibly pending data (but do not detach it) */
static inline void cs_drain_and_close(struct conn_stream *cs)
{
	cs_shutw(cs, CS_SHW_SILENT);
	cs_shutr(cs, CS_SHR_DRAIN);
}

/* sets CS_FL_ERROR or CS_FL_ERR_PENDING on the cs */
static inline void cs_set_error(struct conn_stream *cs)
{
	if (cs->flags & CS_FL_EOS)
		cs->flags |= CS_FL_ERROR;
	else
		cs->flags |= CS_FL_ERR_PENDING;
}

/* Retrieves any valid conn_stream from this connection, preferably the first
 * valid one. The purpose is to be able to figure one other end of a private
 * connection for purposes like source binding or proxy protocol header
 * emission. In such cases, any conn_stream is expected to be valid so the
 * mux is encouraged to return the first one it finds. If the connection has
 * no mux or the mux has no get_first_cs() method or the mux has no valid
 * conn_stream, NULL is returned. The output pointer is purposely marked
 * const to discourage the caller from modifying anything there.
 */
static inline const struct conn_stream *cs_get_first(const struct connection *conn)
{
	if (!conn || !conn->mux || !conn->mux->get_first_cs)
		return NULL;
	return conn->mux->get_first_cs(conn);
}

#endif /* _HAPROXY_CONN_STREAM_H */
