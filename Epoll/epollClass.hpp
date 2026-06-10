# pragma once

# include "../myzod/safeInputClass.hpp"
# include <iostream>
# include <sys/epoll.h>
# include <exception>
# include <vector>
# include <string.h>
# include <ostream>
# include <cerrno>
# include <unistd.h>

# define MAX_REQUEST	4096
# define MIN_MAX	64

class Arole;
class Server;

class Epoll {

	int				_fd;
	std::vector<struct epoll_event>	_events;
	std::vector<Server*>		_servers;

	int				_active;

	Epoll(Epoll const &);
	Epoll & operator=(Epoll const &);

	Epoll(void);
	~Epoll(void);

	void	sigManager(void);
	int	alreadyUsePort(unsigned short int, int serv = 0);
	
public:


	static Epoll& createEpoll(void);

	int					getFD(void) const;
	std::vector<struct epoll_event> const &	getEvents(void) const;

	int	wait(void);
	void	add(Arole *);
	void	mod(Arole *);
	void	del(Arole *);

	void	addServer(Server *);

	void	show(void);
	
	void		run(void);
	static void	handleSigEvent(int signum);

	class epollError : public std::exception {
		virtual const char * what() const throw() {
			return (strerror(errno));
		}
	};
};

std::ostream &	operator<<(std::ostream &, Epoll const &);
