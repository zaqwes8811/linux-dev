#include "user_kern.h"

#include <event2/event.h>

#include <vector>
#include <iostream>
#include <map>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

using namespace std;

#define ERR(...) fprintf( stderr, "\7" __VA_ARGS__ ), exit( EXIT_FAILURE )

#define LEN_MSG 256

class Pollable
{

};

static map<int, Pollable*> handlers;

// Свободная функция, и это уже проблема на самом деле
// Лучше самому написать более ООПэшно
// Нужно сразу делать глобалные переменные или списки дескрипторов
//
// С другой стороны не так и плохо, можно не писать в ручную
//
void event_callback( evutil_socket_t fd, short what, void * )
{
	static int id = 0;

	id++;
	if( (what & EV_WRITE) ){
		img_hndl_t h;
		h.id = id;
		int count = sizeof(struct img_hndl_t);
		ssize_t res = write(fd, (void*) &h, count);

		cout << "tick:" << fd << endl;
	}
}

int main( int argc, char *argv[] )
{
	char name[] = "/dev/stream_dvr0";
	int i;
	int fd = -1;
	if( (fd = open(name, O_RDWR)) < 0 ){
		ERR("open device error: %m\n");
	}
	int cur_flg = fcntl(fd, F_GETFL);
	if( -1 == fcntl(fd, F_SETFL, cur_flg | O_NONBLOCK) ){
		ERR("fcntl device error: %m\n");
	}

	if( true ){
		struct event_base *base = NULL;
		base = event_base_new();

		event* e = event_new(base, -1, EV_TIMEOUT | EV_PERSIST, event_callback,
		NULL);

		event* e1 = event_new(base, fd, EV_WRITE | EV_PERSIST, event_callback,
		NULL);

		timeval ts = { 1, 0 };
		event_add(e, &ts);
		event_add(e1, NULL);
		event_base_dispatch(base);
	}
	else{
		struct pollfd fds[1];
		fds[0].fd = fd;
		fds[0].events = POLLOUT | POLLWRNORM // | POLLIN | POLLRDNORM,
		;

		vector<img_hndl_t> hndls;
		for( int i = 0; i < 10; i++ ){
			img_hndl_t h;
			h.id = i;
			hndls.push_back(h);
		}

		int ptr = 0;
		while( true ){
			if( ptr == hndls.size() ){
				break;
			}

			int status = poll(fds, 1, 500);

			int count = sizeof(struct img_hndl_t);
			ssize_t res = write(fd, (void*) &hndls[ptr], count);
			if( res < 0 ){
				continue;
			}
			else if( 0 == res ){
				if( errno == EAGAIN ){
					continue;
				}
			}
			else if( count == res ){
				++ptr;
			}
		}

	}
	close(fd);
	return EXIT_SUCCESS;
}
