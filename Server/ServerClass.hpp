# pragma once

# include "socket/socketClass.hpp"
# include "AroleClass/AroleClass.hpp"
# include "Epoll/epollClass.hpp"
# include <vector>

class Server {

	std::map<unsigned short int, Socket *>	_sockets;

	Epoll					_epoll;

	Server(Server const &);
	Server& operator=(Server const &);
	
public:
	Server(void);
	Server(int, std::vector<unsigned short int> &);
	~Server(void);

	size_t		getOpenSocket(void) const;
	Epoll const &	getEpoll(void) const;

	void	run(void);
	void	start(void);
	void	start(unsigned short int);
	void	stop(void);
	void	stop(unsigned short int);
	void	exit(unsigned short int);

	class toMuchSocket : public std::exception {
		virtual const char * what() const throw() {
			return ("The number of socket per server need to be between 0 (out) and 10 (in)");
		};
	};
};

std::ostream &	operator<<(std::ostream &, Server const &);
