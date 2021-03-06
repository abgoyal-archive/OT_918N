
/* Handle the multiplexed channels, such as sessions, x11, agent connections */

#include "includes.h"
#include "session.h"
#include "packet.h"
#include "ssh.h"
#include "buffer.h"
#include "circbuffer.h"
#include "dbutil.h"
#include "channel.h"
#include "ssh.h"
#include "listener.h"

static void send_msg_channel_open_failure(unsigned int remotechan, int reason,
		const unsigned char *text, const unsigned char *lang);
static void send_msg_channel_open_confirmation(struct Channel* channel,
		unsigned int recvwindow, 
		unsigned int recvmaxpacket);
static void writechannel(struct Channel* channel, int fd, circbuffer *cbuf);
static void send_msg_channel_window_adjust(struct Channel *channel, 
		unsigned int incr);
static void send_msg_channel_data(struct Channel *channel, int isextended);
static void send_msg_channel_eof(struct Channel *channel);
static void send_msg_channel_close(struct Channel *channel);
static void remove_channel(struct Channel *channel);
static void delete_channel(struct Channel *channel);
static void check_in_progress(struct Channel *channel);
static unsigned int write_pending(struct Channel * channel);
static void check_close(struct Channel *channel);
static void close_chan_fd(struct Channel *channel, int fd, int how);

#define FD_UNINIT (-2)
#define FD_CLOSED (-1)

#define ERRFD_IS_READ(channel) ((channel)->extrabuf == NULL)
#define ERRFD_IS_WRITE(channel) (!ERRFD_IS_READ(channel))

/* Initialise all the channels */
void chaninitialise(const struct ChanType *chantypes[]) {

	/* may as well create space for a single channel */
	ses.channels = (struct Channel**)m_malloc(sizeof(struct Channel*));
	ses.chansize = 1;
	ses.channels[0] = NULL;
	ses.chancount = 0;

	ses.chantypes = chantypes;

#ifdef USING_LISTENERS
	listeners_initialise();
#endif

}

/* Clean up channels, freeing allocated memory */
void chancleanup() {

	unsigned int i;

	TRACE(("enter chancleanup"))
	for (i = 0; i < ses.chansize; i++) {
		if (ses.channels[i] != NULL) {
			TRACE(("channel %d closing", i))
			remove_channel(ses.channels[i]);
		}
	}
	m_free(ses.channels);
	TRACE(("leave chancleanup"))
}

/* Create a new channel entry, send a reply confirm or failure */
struct Channel* newchannel(unsigned int remotechan, 
		const struct ChanType *type, 
		unsigned int transwindow, unsigned int transmaxpacket) {

	struct Channel * newchan;
	unsigned int i, j;

	TRACE(("enter newchannel"))
	
	/* first see if we can use existing channels */
	for (i = 0; i < ses.chansize; i++) {
		if (ses.channels[i] == NULL) {
			break;
		}
	}

	/* otherwise extend the list */
	if (i == ses.chansize) {
		if (ses.chansize >= MAX_CHANNELS) {
			TRACE(("leave newchannel: max chans reached"))
			return NULL;
		}

		/* extend the channels */
		ses.channels = (struct Channel**)m_realloc(ses.channels,
				(ses.chansize+CHAN_EXTEND_SIZE)*sizeof(struct Channel*));

		ses.chansize += CHAN_EXTEND_SIZE;

		/* set the new channels to null */
		for (j = i; j < ses.chansize; j++) {
			ses.channels[j] = NULL;
		}

	}
	
	newchan = (struct Channel*)m_malloc(sizeof(struct Channel));
	newchan->type = type;
	newchan->index = i;
	newchan->sent_close = newchan->recv_close = 0;
	newchan->sent_eof = newchan->recv_eof = 0;

	newchan->remotechan = remotechan;
	newchan->transwindow = transwindow;
	newchan->transmaxpacket = transmaxpacket;
	
	newchan->typedata = NULL;
	newchan->writefd = FD_UNINIT;
	newchan->readfd = FD_UNINIT;
	newchan->errfd = FD_CLOSED; /* this isn't always set to start with */
	newchan->initconn = 0;
	newchan->await_open = 0;
	newchan->flushing = 0;

	newchan->writebuf = cbuf_new(RECV_MAXWINDOW);
	newchan->extrabuf = NULL; /* The user code can set it up */
	newchan->recvwindow = RECV_MAXWINDOW;
	newchan->recvdonelen = 0;
	newchan->recvmaxpacket = RECV_MAXPACKET;

	ses.channels[i] = newchan;
	ses.chancount++;

	TRACE(("leave newchannel"))

	return newchan;
}

static struct Channel* getchannel_msg(const char* kind) {

	unsigned int chan;

	chan = buf_getint(ses.payload);
	if (chan >= ses.chansize || ses.channels[chan] == NULL) {
		if (kind) {
			dropbear_exit("%s for unknown channel %d", kind, chan);
		} else {
			dropbear_exit("Unknown channel %d", chan);
		}
	}
	return ses.channels[chan];
}

struct Channel* getchannel() {
	return getchannel_msg(NULL);
}

/* Iterate through the channels, performing IO if available */
void channelio(fd_set *readfds, fd_set *writefds) {

	struct Channel *channel;
	unsigned int i;

	/* foreach channel */
	for (i = 0; i < ses.chansize; i++) {

		channel = ses.channels[i];
		if (channel == NULL) {
			/* only process in-use channels */
			continue;
		}

		/* read data and send it over the wire */
		if (channel->readfd >= 0 && FD_ISSET(channel->readfd, readfds)) {
			TRACE(("send normal readfd"))
			send_msg_channel_data(channel, 0);
		}

		/* read stderr data and send it over the wire */
		if (ERRFD_IS_READ(channel) && channel->errfd >= 0 
			&& FD_ISSET(channel->errfd, readfds)) {
				TRACE(("send normal errfd"))
				send_msg_channel_data(channel, 1);
		}

		/* write to program/pipe stdin */
		if (channel->writefd >= 0 && FD_ISSET(channel->writefd, writefds)) {
			if (channel->initconn) {
				/* XXX should this go somewhere cleaner? */
				check_in_progress(channel);
				continue; /* Important not to use the channel after
							 check_in_progress(), as it may be NULL */
			}
			writechannel(channel, channel->writefd, channel->writebuf);
		}
		
		/* stderr for client mode */
		if (ERRFD_IS_WRITE(channel)
				&& channel->errfd >= 0 && FD_ISSET(channel->errfd, writefds)) {
			writechannel(channel, channel->errfd, channel->extrabuf);
		}
	
		/* handle any channel closing etc */
		check_close(channel);

	}

	/* Listeners such as TCP, X11, agent-auth */
#ifdef USING_LISTENERS
	handle_listeners(readfds);
#endif
}


static unsigned int write_pending(struct Channel * channel) {

	if (channel->writefd >= 0 && cbuf_getused(channel->writebuf) > 0) {
		return 1;
	} else if (channel->errfd >= 0 && channel->extrabuf && 
			cbuf_getused(channel->extrabuf) > 0) {
		return 1;
	}
	return 0;
}


/* EOF/close handling */
static void check_close(struct Channel *channel) {

	TRACE(("check_close: writefd %d, readfd %d, errfd %d, sent_close %d, recv_close %d",
				channel->writefd, channel->readfd,
				channel->errfd, channel->sent_close, channel->recv_close))
	TRACE(("writebuf size %d extrabuf size %d",
				cbuf_getused(channel->writebuf),
				channel->extrabuf ? cbuf_getused(channel->extrabuf) : 0))

	if (!channel->flushing && channel->type->check_close
		&& channel->type->check_close(channel))
	{
		channel->flushing = 1;
	}

	if (channel->recv_close && !write_pending(channel)) {
		if (!channel->sent_close) {
			TRACE(("Sending MSG_CHANNEL_CLOSE in response to same."))
			send_msg_channel_close(channel);
		}
		remove_channel(channel);
		return;
	}

	if (channel->recv_eof && !write_pending(channel)) {
		close_chan_fd(channel, channel->writefd, SHUT_WR);
	}

	/* Special handling for flushing read data after an exit. We
	   read regardless of whether the select FD was set,
	   and if there isn't data available, the channel will get closed. */
	if (channel->flushing) {
		TRACE(("might send data, flushing"))
		if (channel->readfd >= 0 && channel->transwindow > 0) {
			TRACE(("send data readfd"))
			send_msg_channel_data(channel, 0);
		}
		if (ERRFD_IS_READ(channel) && channel->errfd >= 0 
			&& channel->transwindow > 0) {
			TRACE(("send data errfd"))
			send_msg_channel_data(channel, 1);
		}
	}

	/* If we're not going to send any more data, send EOF */
	if (!channel->sent_eof
			&& channel->readfd == FD_CLOSED 
			&& (ERRFD_IS_WRITE(channel) || channel->errfd == FD_CLOSED)) {
		send_msg_channel_eof(channel);
	}

	/* And if we can't receive any more data from them either, close up */
	if (!channel->sent_close
			&& channel->readfd == FD_CLOSED
			&& (ERRFD_IS_WRITE(channel) || channel->errfd == FD_CLOSED)
			&& !write_pending(channel)) {
		TRACE(("sending close, readfd is closed"))
		send_msg_channel_close(channel);
	}
}

static void check_in_progress(struct Channel *channel) {

	int val;
	socklen_t vallen = sizeof(val);

	TRACE(("enter check_in_progress"))

	if (getsockopt(channel->writefd, SOL_SOCKET, SO_ERROR, &val, &vallen)
			|| val != 0) {
		send_msg_channel_open_failure(channel->remotechan,
				SSH_OPEN_CONNECT_FAILED, "", "");
		close(channel->writefd);
		delete_channel(channel);
		TRACE(("leave check_in_progress: fail"))
	} else {
		send_msg_channel_open_confirmation(channel, channel->recvwindow,
				channel->recvmaxpacket);
		channel->readfd = channel->writefd;
		channel->initconn = 0;
		TRACE(("leave check_in_progress: success"))
	}
}


/* Send the close message and set the channel as closed */
static void send_msg_channel_close(struct Channel *channel) {

	TRACE(("enter send_msg_channel_close"))
	if (channel->type->closehandler) {
		channel->type->closehandler(channel);
	}
	
	CHECKCLEARTOWRITE();

	buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_CLOSE);
	buf_putint(ses.writepayload, channel->remotechan);

	encrypt_packet();

	channel->sent_eof = 1;
	channel->sent_close = 1;
	close_chan_fd(channel, channel->readfd, SHUT_RD);
	close_chan_fd(channel, channel->errfd, SHUT_RDWR);
	close_chan_fd(channel, channel->writefd, SHUT_WR);
	TRACE(("leave send_msg_channel_close"))
}

/* call this when trans/eof channels are closed */
static void send_msg_channel_eof(struct Channel *channel) {

	TRACE(("enter send_msg_channel_eof"))
	CHECKCLEARTOWRITE();

	buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_EOF);
	buf_putint(ses.writepayload, channel->remotechan);

	encrypt_packet();

	channel->sent_eof = 1;

	TRACE(("leave send_msg_channel_eof"))
}

static void writechannel(struct Channel* channel, int fd, circbuffer *cbuf) {

	int len, maxlen;

	TRACE(("enter writechannel fd %d", fd))

	maxlen = cbuf_readlen(cbuf);

	/* Write the data out */
	len = write(fd, cbuf_readptr(cbuf, maxlen), maxlen);
	if (len <= 0) {
		TRACE(("errno %d len %d", errno, len))
		if (len < 0 && errno != EINTR) {
			close_chan_fd(channel, fd, SHUT_WR);
		}
		TRACE(("leave writechannel: len <= 0"))
		return;
	}
	TRACE(("writechannel wrote %d", len))

	cbuf_incrread(cbuf, len);
	channel->recvdonelen += len;

	/* Window adjust handling */
	if (channel->recvdonelen >= RECV_WINDOWEXTEND) {
		/* Set it back to max window */
		send_msg_channel_window_adjust(channel, channel->recvdonelen);
		channel->recvwindow += channel->recvdonelen;
		channel->recvdonelen = 0;
	}

	dropbear_assert(channel->recvwindow <= RECV_MAXWINDOW);
	dropbear_assert(channel->recvwindow <= cbuf_getavail(channel->writebuf));
	dropbear_assert(channel->extrabuf == NULL ||
			channel->recvwindow <= cbuf_getavail(channel->extrabuf));
	
	TRACE(("leave writechannel"))
}

void setchannelfds(fd_set *readfds, fd_set *writefds) {
	
	unsigned int i;
	struct Channel * channel;
	
	for (i = 0; i < ses.chansize; i++) {

		channel = ses.channels[i];
		if (channel == NULL) {
			continue;
		}

		/* Stuff to put over the wire */
		if (channel->transwindow > 0) {

			if (channel->readfd >= 0) {
				FD_SET(channel->readfd, readfds);
			}
			
			if (ERRFD_IS_READ(channel) && channel->errfd >= 0) {
					FD_SET(channel->errfd, readfds);
			}
		}

		/* Stuff from the wire */
		if ((channel->writefd >= 0 && cbuf_getused(channel->writebuf) > 0 )
				|| channel->initconn) {
				FD_SET(channel->writefd, writefds);
		}

		if (ERRFD_IS_WRITE(channel) && channel->errfd >= 0 
				&& cbuf_getused(channel->extrabuf) > 0 ) {
				FD_SET(channel->errfd, writefds);
		}

	} /* foreach channel */

#ifdef USING_LISTENERS
	set_listener_fds(readfds);
#endif

}

void recv_msg_channel_eof() {

	struct Channel * channel;

	TRACE(("enter recv_msg_channel_eof"))

	channel = getchannel_msg("EOF");

	channel->recv_eof = 1;

	check_close(channel);
	TRACE(("leave recv_msg_channel_eof"))
}


/* Handle channel closure(), respond in kind and close the channels */
void recv_msg_channel_close() {

	struct Channel * channel;

	TRACE(("enter recv_msg_channel_close"))

	channel = getchannel_msg("Close");

	channel->recv_eof = 1;
	channel->recv_close = 1;

	check_close(channel);
	TRACE(("leave recv_msg_channel_close"))
}

static void remove_channel(struct Channel * channel) {

	TRACE(("enter remove_channel"))
	TRACE(("channel index is %d", channel->index))

	cbuf_free(channel->writebuf);
	channel->writebuf = NULL;

	if (channel->extrabuf) {
		cbuf_free(channel->extrabuf);
		channel->extrabuf = NULL;
	}


	/* close the FDs in case they haven't been done
	 * yet (they might have been shutdown etc) */
	TRACE(("CLOSE writefd %d", channel->writefd))
	close(channel->writefd);
	TRACE(("CLOSE readfd %d", channel->readfd))
	close(channel->readfd);
	TRACE(("CLOSE errfd %d", channel->errfd))
	close(channel->errfd);

	channel->typedata = NULL;

	delete_channel(channel);

	TRACE(("leave remove_channel"))
}

/* Remove a channel entry */
static void delete_channel(struct Channel *channel) {

	ses.channels[channel->index] = NULL;
	m_free(channel);
	ses.chancount--;
	
}


void recv_msg_channel_request() {

	struct Channel *channel;

	TRACE(("enter recv_msg_channel_request"))
	
	channel = getchannel();

	if (channel->type->reqhandler) {
		channel->type->reqhandler(channel);
	} else {
		send_msg_channel_failure(channel);
	}

	TRACE(("leave recv_msg_channel_request"))

}

static void send_msg_channel_data(struct Channel *channel, int isextended) {

	int len;
	size_t maxlen, size_pos;
	int fd;

	CHECKCLEARTOWRITE();

	TRACE(("enter send_msg_channel_data"))
	dropbear_assert(!channel->sent_close);

	if (isextended) {
		fd = channel->errfd;
	} else {
		fd = channel->readfd;
	}
	TRACE(("enter send_msg_channel_data isextended %d fd %d", isextended, fd))
	dropbear_assert(fd >= 0);

	maxlen = MIN(channel->transwindow, channel->transmaxpacket);
	/* -(1+4+4) is SSH_MSG_CHANNEL_DATA, channel number, string length, and 
	 * exttype if is extended */
	maxlen = MIN(maxlen, 
			ses.writepayload->size - 1 - 4 - 4 - (isextended ? 4 : 0));
	TRACE(("maxlen %d", maxlen))
	if (maxlen == 0) {
		TRACE(("leave send_msg_channel_data: no window"))
		return;
	}

	buf_putbyte(ses.writepayload, 
			isextended ? SSH_MSG_CHANNEL_EXTENDED_DATA : SSH_MSG_CHANNEL_DATA);
	buf_putint(ses.writepayload, channel->remotechan);
	if (isextended) {
		buf_putint(ses.writepayload, SSH_EXTENDED_DATA_STDERR);
	}
	/* a dummy size first ...*/
	size_pos = ses.writepayload->pos;
	buf_putint(ses.writepayload, 0);

	/* read the data */
	len = read(fd, buf_getwriteptr(ses.writepayload, maxlen), maxlen);
	if (len <= 0) {
		if (len == 0 || errno != EINTR) {
			/* This will also get hit in the case of EAGAIN. The only
			time we expect to receive EAGAIN is when we're flushing a FD,
			in which case it can be treated the same as EOF */
			close_chan_fd(channel, fd, SHUT_RD);
		}
		ses.writepayload->len = ses.writepayload->pos = 0;
		TRACE(("leave send_msg_channel_data: len %d read err %d or EOF for fd %d", 
					len, errno, fd))
		return;
	}
	buf_incrwritepos(ses.writepayload, len);
	/* ... real size here */
	buf_setpos(ses.writepayload, size_pos);
	buf_putint(ses.writepayload, len);

	channel->transwindow -= len;

	encrypt_packet();
	
	/* If we receive less data than we requested when flushing, we've
	   reached the equivalent of EOF */
	if (channel->flushing && len < (ssize_t)maxlen)
	{
		TRACE(("closing from channel, flushing out."))
		close_chan_fd(channel, fd, SHUT_RD);
	}
	TRACE(("leave send_msg_channel_data"))
}

/* We receive channel data */
void recv_msg_channel_data() {

	struct Channel *channel;

	channel = getchannel();

	common_recv_msg_channel_data(channel, channel->writefd, channel->writebuf);
}

void common_recv_msg_channel_data(struct Channel *channel, int fd, 
		circbuffer * cbuf) {

	unsigned int datalen;
	unsigned int maxdata;
	unsigned int buflen;
	unsigned int len;

	TRACE(("enter recv_msg_channel_data"))

	if (channel->recv_eof) {
		dropbear_exit("received data after eof");
	}

 	if (fd < 0) {
		/* If we have encountered failed write, the far side might still
		 * be sending data without having yet received our close notification.
		 * We just drop the data. */
		return;
	}

	datalen = buf_getint(ses.payload);
	TRACE(("length %d", datalen))

	maxdata = cbuf_getavail(cbuf);

	/* Whilst the spec says we "MAY ignore data past the end" this could
	 * lead to corrupted file transfers etc (chunks missed etc). It's better to
	 * just die horribly */
	if (datalen > maxdata) {
		dropbear_exit("Oversized packet");
	}

	/* We may have to run throught twice, if the buffer wraps around. Can't
	 * just "leave it for next time" like with writechannel, since this
	 * is payload data */
	len = datalen;
	while (len > 0) {
		buflen = cbuf_writelen(cbuf);
		buflen = MIN(buflen, len);

		memcpy(cbuf_writeptr(cbuf, buflen), 
				buf_getptr(ses.payload, buflen), buflen);
		cbuf_incrwrite(cbuf, buflen);
		buf_incrpos(ses.payload, buflen);
		len -= buflen;
	}

	dropbear_assert(channel->recvwindow >= datalen);
	channel->recvwindow -= datalen;
	dropbear_assert(channel->recvwindow <= RECV_MAXWINDOW);

	TRACE(("leave recv_msg_channel_data"))
}

void recv_msg_channel_window_adjust() {

	struct Channel * channel;
	unsigned int incr;
	
	channel = getchannel();
	
	incr = buf_getint(ses.payload);
	TRACE(("received window increment %d", incr))
	incr = MIN(incr, MAX_TRANS_WIN_INCR);
	
	channel->transwindow += incr;
	channel->transwindow = MIN(channel->transwindow, MAX_TRANS_WINDOW);

}

static void send_msg_channel_window_adjust(struct Channel* channel, 
		unsigned int incr) {

	TRACE(("sending window adjust %d", incr))
	CHECKCLEARTOWRITE();

	buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_WINDOW_ADJUST);
	buf_putint(ses.writepayload, channel->remotechan);
	buf_putint(ses.writepayload, incr);

	encrypt_packet();
}
	
/* Handle a new channel request, performing any channel-type-specific setup */
void recv_msg_channel_open() {

	unsigned char *type;
	unsigned int typelen;
	unsigned int remotechan, transwindow, transmaxpacket;
	struct Channel *channel;
	const struct ChanType **cp;
	const struct ChanType *chantype;
	unsigned int errtype = SSH_OPEN_UNKNOWN_CHANNEL_TYPE;
	int ret;


	TRACE(("enter recv_msg_channel_open"))

	/* get the packet contents */
	type = buf_getstring(ses.payload, &typelen);

	remotechan = buf_getint(ses.payload);
	transwindow = buf_getint(ses.payload);
	transwindow = MIN(transwindow, MAX_TRANS_WINDOW);
	transmaxpacket = buf_getint(ses.payload);
	transmaxpacket = MIN(transmaxpacket, MAX_TRANS_PAYLOAD_LEN);

	/* figure what type of packet it is */
	if (typelen > MAX_NAME_LEN) {
		goto failure;
	}

	/* Get the channel type. Client and server style invokation will set up a
	 * different list for ses.chantypes at startup. We just iterate through
	 * this list and find the matching name */
	for (cp = &ses.chantypes[0], chantype = (*cp); 
			chantype != NULL;
			cp++, chantype = (*cp)) {
		if (strcmp(type, chantype->name) == 0) {
			break;
		}
	}

	if (chantype == NULL) {
		TRACE(("No matching type for '%s'", type))
		goto failure;
	}

	TRACE(("matched type '%s'", type))

	/* create the channel */
	channel = newchannel(remotechan, chantype, transwindow, transmaxpacket);

	if (channel == NULL) {
		TRACE(("newchannel returned NULL"))
		goto failure;
	}

	if (channel->type->inithandler) {
		ret = channel->type->inithandler(channel);
		if (ret == SSH_OPEN_IN_PROGRESS) {
			/* We'll send the confirmation later */
			goto cleanup;
		}
		if (ret > 0) {
			errtype = ret;
			delete_channel(channel);
			TRACE(("inithandler returned failure %d", ret))
			goto failure;
		}
	}

	/* success */
	send_msg_channel_open_confirmation(channel, channel->recvwindow,
			channel->recvmaxpacket);
	goto cleanup;

failure:
	TRACE(("recv_msg_channel_open failure"))
	send_msg_channel_open_failure(remotechan, errtype, "", "");

cleanup:
	m_free(type);

	TRACE(("leave recv_msg_channel_open"))
}

/* Send a failure message */
void send_msg_channel_failure(struct Channel *channel) {

	TRACE(("enter send_msg_channel_failure"))
	CHECKCLEARTOWRITE();

	buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_FAILURE);
	buf_putint(ses.writepayload, channel->remotechan);

	encrypt_packet();
	TRACE(("leave send_msg_channel_failure"))
}

/* Send a success message */
void send_msg_channel_success(struct Channel *channel) {

	TRACE(("enter send_msg_channel_success"))
	CHECKCLEARTOWRITE();

	buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_SUCCESS);
	buf_putint(ses.writepayload, channel->remotechan);

	encrypt_packet();
	TRACE(("leave send_msg_channel_success"))
}

static void send_msg_channel_open_failure(unsigned int remotechan, 
		int reason, const unsigned char *text, const unsigned char *lang) {

	TRACE(("enter send_msg_channel_open_failure"))
	CHECKCLEARTOWRITE();
	
	buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_OPEN_FAILURE);
	buf_putint(ses.writepayload, remotechan);
	buf_putint(ses.writepayload, reason);
	buf_putstring(ses.writepayload, text, strlen((char*)text));
	buf_putstring(ses.writepayload, lang, strlen((char*)lang));

	encrypt_packet();
	TRACE(("leave send_msg_channel_open_failure"))
}

static void send_msg_channel_open_confirmation(struct Channel* channel,
		unsigned int recvwindow, 
		unsigned int recvmaxpacket) {

	TRACE(("enter send_msg_channel_open_confirmation"))
	CHECKCLEARTOWRITE();

	buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_OPEN_CONFIRMATION);
	buf_putint(ses.writepayload, channel->remotechan);
	buf_putint(ses.writepayload, channel->index);
	buf_putint(ses.writepayload, recvwindow);
	buf_putint(ses.writepayload, recvmaxpacket);

	encrypt_packet();
	TRACE(("leave send_msg_channel_open_confirmation"))
}

/* close a fd, how is SHUT_RD or SHUT_WR */
static void close_chan_fd(struct Channel *channel, int fd, int how) {

	int closein = 0, closeout = 0;

	if (channel->type->sepfds) {
		TRACE(("SHUTDOWN(%d, %d)", fd, how))
		shutdown(fd, how);
		if (how == 0) {
			closeout = 1;
		} else {
			closein = 1;
		}
	} else {
		TRACE(("CLOSE some fd %d", fd))
		close(fd);
		closein = closeout = 1;
	}

	if (closeout && (fd == channel->readfd)) {
		channel->readfd = FD_CLOSED;
	}
	if (closeout && ERRFD_IS_READ(channel) && (fd == channel->errfd)) {
		channel->errfd = FD_CLOSED;
	}

	if (closein && fd == channel->writefd) {
		channel->writefd = FD_CLOSED;
	}
	if (closein && ERRFD_IS_WRITE(channel) && (fd == channel->errfd)) {
		channel->errfd = FD_CLOSED;
	}

	/* if we called shutdown on it and all references are gone, then we 
	 * need to close() it to stop it lingering */
	if (channel->type->sepfds && channel->readfd == FD_CLOSED 
		&& channel->writefd == FD_CLOSED && channel->errfd == FD_CLOSED) {
		TRACE(("CLOSE (finally) of %d", fd))
		close(fd);
	}
}


#if defined(USING_LISTENERS) || defined(DROPBEAR_CLIENT)
int send_msg_channel_open_init(int fd, const struct ChanType *type) {

	struct Channel* chan;

	TRACE(("enter send_msg_channel_open_init()"))
	chan = newchannel(0, type, 0, 0);
	if (!chan) {
		TRACE(("leave send_msg_channel_open_init() - FAILED in newchannel()"))
		return DROPBEAR_FAILURE;
	}

	/* set fd non-blocking */
	setnonblocking(fd);

	chan->writefd = chan->readfd = fd;
	ses.maxfd = MAX(ses.maxfd, fd);

	chan->await_open = 1;

	/* now open the channel connection */
	CHECKCLEARTOWRITE();

	buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_OPEN);
	buf_putstring(ses.writepayload, type->name, strlen(type->name));
	buf_putint(ses.writepayload, chan->index);
	buf_putint(ses.writepayload, RECV_MAXWINDOW);
	buf_putint(ses.writepayload, RECV_MAXPACKET);

	TRACE(("leave send_msg_channel_open_init()"))
	return DROPBEAR_SUCCESS;
}

void recv_msg_channel_open_confirmation() {

	struct Channel * channel;
	int ret;

	TRACE(("enter recv_msg_channel_open_confirmation"))

	channel = getchannel();

	if (!channel->await_open) {
		dropbear_exit("unexpected channel reply");
	}
	channel->await_open = 0;

	channel->remotechan =  buf_getint(ses.payload);
	channel->transwindow = buf_getint(ses.payload);
	channel->transmaxpacket = buf_getint(ses.payload);
	
	TRACE(("new chan remote %d local %d", 
				channel->remotechan, channel->index))

	/* Run the inithandler callback */
	if (channel->type->inithandler) {
		ret = channel->type->inithandler(channel);
		if (ret > 0) {
			remove_channel(channel);
			TRACE(("inithandler returned failure %d", ret))
		}
	}

	
	TRACE(("leave recv_msg_channel_open_confirmation"))
}

/* Notification that our channel open request failed */
void recv_msg_channel_open_failure() {

	struct Channel * channel;

	channel = getchannel();

	if (!channel->await_open) {
		dropbear_exit("unexpected channel reply");
	}
	channel->await_open = 0;

	remove_channel(channel);
}
#endif /* USING_LISTENERS */
