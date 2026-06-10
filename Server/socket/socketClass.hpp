# pragma once

# include <sys/socket.h>
# include <sys/epoll.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <ostream>
# include <iostream>
# include <exception>
# include <cerrno>
# include <string.h>
# include <unistd.h>
# include <map>
# include "sockaddrClass.hpp"
# include "../AroleClass/AroleClass.hpp"

class Epoll;

class Socket {

	int		_sockfd;
	Sockaddr	_addr;
	Acceptor	_acceptor;

	Socket(Socket const &);
	Socket& operator=(Socket const &);

public:
	Socket(void);
	Socket(unsigned short int port);
	~Socket(void);
	
	int			getSockfd(void) const;
	Sockaddr const &	getAddr(void) const;
	Acceptor const &	getAcceptor(void) const;
	
	void 		start(Epoll &);
	void 		stop(Epoll &);

	class badExec : public std::exception {
		virtual const char * what() const throw() {return(strerror(errno));};
	};

};

std::ostream &	operator<<(std::ostream &, Socket const &);
