# pragma once

# include <map>
# include <vector>
# include <ostream>
# include <exception>
# include <csignal>
# include <cstdlib>

class Epoll;
class Socket;

class Server {

	std::map<unsigned short int, Socket *>	_sockets;

	Epoll&					_epoll;

	Server(Server const &);
	Server& operator=(Server const &);
	
	
	void		sigManager(void);

public:
	Server(Epoll &);
	Server(int, std::vector<unsigned short int> &, Epoll &);
	~Server(void);

	void		start(unsigned short int);
	void		stop(unsigned short int);
	
	int		alreadyUsePort(unsigned short int);

	size_t		getOpenSocket(void) const;
	int		getIsActive(void) const;

	void		allSocketDebug(void) const;

	class toMuchSocket : public std::exception {
		virtual const char * what() const throw() {
			return ("The number of socket per server need to be between 0 (out) and 10 (in)");
		};
	};
};

std::ostream &	operator<<(std::ostream &, Server const &);
