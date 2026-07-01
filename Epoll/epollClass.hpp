# pragma once

# include <iostream>
# include <sys/epoll.h>
# include <exception>
# include <vector>
# include <string.h>
# include <cerrno>
# include <unistd.h>

# define MAX_REQUEST	4096
# define MIN_MAX	64

class Arole;

class Epoll {

	int				_fd;
	std::vector<struct epoll_event>	_events;

	Epoll(Epoll const &);
	Epoll & operator=(Epoll const &);

	Epoll(void);
	~Epoll(void);

public:

	static Epoll&	createEpoll(void);

	int					getFD(void) const;
	std::vector<struct epoll_event> const &	getEvents(void) const;

	int	wait(void);
	void	add(Arole *);
	void	mod(Arole *);
	void	del(Arole *);

	class epollError : public std::exception {
		virtual const char * what() const throw() {
			return (strerror(errno));
		}
	};
};
