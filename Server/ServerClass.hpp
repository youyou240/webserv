# pragma once

# include "socket/socketClass.hpp"
# include "AroleClass/AroleClass.hpp"
# include "Epoll/epollClass.hpp"
# include <vector>
# include <csignal>
# include <cstdlib>


class Server {

	std::map<unsigned short int, Socket *>	_sockets;

	Epoll					_epoll;

	int					_active;
	Server(Server const &);
	Server& operator=(Server const &);
	
	void	start(unsigned short int);
	void	stop(unsigned short int);
	
	void		sigManager(void);

public:
	Server(void);
	Server(int, std::vector<unsigned short int> &);
	~Server(void);

	size_t		getOpenSocket(void) const;
	Epoll const &	getEpoll(void) const;
	int		getIsActive(void) const;

	void	run(void);

	void	allSocketDebug(void) const;

	static void	handleSigEvent(int signum);

	class toMuchSocket : public std::exception {
		virtual const char * what() const throw() {
			return ("The number of socket per server need to be between 0 (out) and 10 (in)");
		};
	};
};

std::ostream &	operator<<(std::ostream &, Server const &);
