# pragma once

# include <sys/socket.h>
# include <sys/epoll.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <stream>
# include <exception>
# include <cerrno>
# include <unistd.h>
# include <vector>
# include <map>
# include "socketaddrClass.hpp"
# include "AroleClass/AroleClass.hpp"

class Socket {

	int				_sockfd;
	Sockaddr			_addr;
	Epoll				_manager;
	Server				_serv;
	std::vector<unsigned int>	_fdTable(500);

public:
	Socket(void);
	Socket(unsigned short int port);
	Socket(Socket const &);
	~Socket(void);

	Socket& operator=(Socket const &);

	int&		getSockfd(void) const;
	Sockaddr&	getAddr(void) const;
	Epoll&		getEpoll(void) const;
	
	void		closeAll(void);
	
	class badExec : public std::exception {
		virtual char * const what() const throw() {return(strerror(errno));};
	};

};

std::ostream &	operator<<(std::ostream &, Socket const &);
