#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "aml.h"

struct posix_state {
	struct aml* aml;

	struct pollfd* fds;
	void** userdata;

	uint32_t max_fds;
	uint32_t num_fds;
};

static void* posix_new_state(struct aml* aml)
{
	struct posix_state* self = calloc(1, sizeof(*self));
	if (!self)
		return NULL;

	self->aml = aml;
	self->max_fds = 128;
	self->fds = malloc(sizeof(*self->fds) * self->max_fds);
	self->userdata = malloc(sizeof(*self->userdata) * self->max_fds);
	if (!self->fds || !self->userdata) {
		free(self->fds);
		free(self->userdata);
		goto failure;
	}

	return self;

failure:
	free(self);
	return NULL;
}

static int posix__find_fd(struct posix_state* self, int fd)
{
	for (uint32_t i = 0; i < self->num_fds; ++i)
		if (self->fds[i].fd == fd)
			return i;

	return -1;
}

void posix_del_state(void* state)
{
	struct posix_state* self = state;

	free(self->userdata);
	free(self->fds);
	free(self);
}

int posix_poll(void* state, int timeout)
{
	struct posix_state* self = state;

	int nfds = poll(self->fds, self->num_fds, timeout);
	if (nfds <= 0)
		return nfds;

	for (uint32_t i = 0; i < self->num_fds; ++i)
		if (self->fds[i].revents) {
			struct pollfd* pfd = &self->fds[i];
			void* ud = &self->userdata[i];

			aml_emit(self->aml, ud, pfd->revents);
		}

	return nfds;
}

int posix_add_fd(void* state, const struct aml_fd_event* fdev)
{
	struct posix_state* self = state;

	if (self->num_fds >= self->max_fds) {
		uint32_t new_max = self->max_fds * 2;
		struct pollfd* fds = realloc(self->fds, sizeof(*fds) * new_max);
		void** uds = realloc(self->userdata, sizeof(*uds) * new_max);
		if (!fds || !uds) {
			free(fds);
			free(uds);
			return -1;
		}

		self->fds = fds;
		self->userdata = uds;
		self->max_fds = new_max;
	}

	struct pollfd* event = &self->fds[self->num_fds];
	event->events = fdev->event_mask;
	event->revents = 0;
	event->fd = fdev->fd;

	self->userdata[self->num_fds] = fdev->userdata;

	self->num_fds++;

	return 0;
}

int posix_mod_fd(void* state, const struct aml_fd_event* fdev)
{
	struct posix_state* self = state;

	int index = posix__find_fd(self, fdev->fd);
	if (index < 0)
		return -1;

	self->fds[index].events = fdev->event_mask;
	self->userdata[index] = fdev->userdata;

	return 0;
}

int posix_del_fd(void* state, int fd)
{
	struct posix_state* self = state;

	int index = posix__find_fd(self, fd);
	if (index < 0)
		return -1;

	self->num_fds--;

	self->fds[index] = self->fds[self->num_fds];
	self->userdata[index] = self->userdata[self->num_fds];

	return 0;
}

const struct aml_backend posix_backend = {
	.new_state = posix_new_state,
	.del_state = posix_del_state,
	.poll = posix_poll,
	.add_fd = posix_add_fd,
	.mod_fd = posix_mod_fd,
	.del_fd = posix_del_fd,
};
