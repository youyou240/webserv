# pragma once

# include "AroleClass/AroleClass.hpp"
# include <sys/epoll.h>
# include <exception>
# include <vector>
# include <string.h>
# include <cerrno>

# define MAX_REQUEST	4096
# define MIN_MAX	64

class Epoll {

	int				_fd;
	std::vector<struct epoll_event>	_events;

	Epoll(Epoll const &);
	Epoll & operator=(Epoll const &);

public:
	Epoll(void);
	~Epoll(void);


	int&					getFD(void) const;
	std::vector<struct epoll_event>&	getEvents(void) const;

	void	close(void);

	int	wait(void);
	int	add(int fd, struct epoll_event);
	int	mod(int fd, struct epoll_event);
	int	del(int fd, struct epoll_event);

	class epollError : public std::exception {
		virtual const char * what() const throw() {
			return (strerror(errno));
		}
	};
}
