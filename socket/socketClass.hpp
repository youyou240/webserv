# pragma once

# include <sys/socket.h>
# include <sys/epoll.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <ostream>
# include <exception>
# include <cerrno>
# include <unistd.h>
# include <vector>
# include <map>
# include "sockaddrClass.hpp"
# include "AroleClass/AroleClass.hpp"

class Socket {

	int				_sockfd;
	Sockaddr			_addr;
	Epoll				_manager;
	Server				_serv;

	Socket(Socket const &);
	Socket& operator=(Socket const &);

public:
	Socket(void);
	Socket(unsigned short int port);
	~Socket(void);


	int&		getSockfd(void) const;
	Sockaddr&	getAddr(void) const;
	Epoll&		getEpoll(void) const;

	void		run(void);
	void		closeAll(void);
	
	class badExec : public std::exception {
		virtual const char * what() const throw() {return(strerror(errno));};
	};

};

std::ostream &	operator<<(std::ostream &, Socket const &);
