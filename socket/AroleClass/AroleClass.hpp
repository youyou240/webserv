# pragma once

# include "../epollClass.hpp"
# include <sys/epoll.h>
# include <sys/socket.h>
# include <fcntl.h>
# include <string.h>
# include <vector>
# include <string>
# include <ctime>

class Client;

class Arole {

protected:
	
	static std::vector<Client *>	_activeClient;
	static unsigned int		_client;
	static unsigned int		_server;
	
	int			_fd;
	struct epoll_event	_ev;

public:
	
	Arole(void);
	Arole(int fd);
	virtual ~Arole(void);
	
	virtual void	handle_event(Epoll & manager) = 0;

	static void	launchActiveRoutine(void);

	int&			getFD(void);
	struct epoll_event &	getStructEvent(void);
};


class Server : public Arole {

public:
	
	Server(void);
	Server(int fd);
	~Server(void);

	virtual void	handle_event(Epoll & manager);
	
	unsigned int	getNbServer(void) const;
	unsigned int	getNbClient(void) const;
	void		closeAll(Epoll const &);
};


class Client : public Arole {
	
	std::string	_request;
	std::string	_response;

	unsigned int	_bytes_send;
	
	bool		_complete;

	time_t		_lastActivity;
	
	Client(void) {};
	
public:

	Client(int fd);
	~Client(void);
	
	virtual void	handle_event(Epoll & manager);

	void	genResponse(void);
	void	cleanRequest(void);
	void	cleanResponse(void);
	
	void	closeConnection(Epoll &);
};
