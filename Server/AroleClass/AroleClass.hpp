# pragma once

# include <sys/epoll.h>
# include <sys/socket.h>
# include <fcntl.h>
# include <string.h>
# include <vector>
# include <map>
# include <string>
# include <ctime>
# include <unistd.h>
# include <ostream>
# include <cerrno>
# include <stdint.h>

class Client;
class Epoll;
struct ServerConfig;

class Arole {

protected:

	static std::map<int, std::map<int, Client *> > _activeClient;;	
	static unsigned int				_client;
	static unsigned int				_acceptor;
	
	int			_fd;
	struct epoll_event	_ev;

public:
	
	Arole(void);
	Arole(int fd);
	virtual ~Arole(void);

	virtual void	handle_event(Epoll & manager, uint32_t events) = 0;

	static void		launchActiveRoutine(Epoll &);
	static unsigned int	getAllClients(void);
	static unsigned int	getAllAcceptators(void);

	int			getFD(void) const;
	struct epoll_event &	getStructEvent(void);
};


class Acceptor : public Arole {

	bool					_active;
	std::vector<const ServerConfig*>	_configs;
public:

	Acceptor(void);
	Acceptor(int fd);
	~Acceptor(void);

	virtual void	handle_event(Epoll & manager, uint32_t events);

	unsigned int	getNbClient(void) const;

	void		setFD(int);
	void		addServerConfig(const ServerConfig * cfg);

	void		start(Epoll &);
	void		closeAll(Epoll &);
};

std::ostream &	operator<<(std::ostream & o,Acceptor  const & acceptor);

class Client : public Arole {

	int		_acceptorfd;
	
	std::string	_request;
	std::string	_response;

	unsigned int	_bytes_send;
	
	bool		_complete;

	time_t		_lastActivity;
	
	Client(void) {};
	
public:

	Client(int fd, int servfd);
	~Client(void);

	virtual void	handle_event(Epoll & manager, uint32_t events);

	bool	handleRead(Epoll & manager);
	void	handleWrite(Epoll & manager);

	void	genResponse(void);
	void	cleanRequest(void);
	void	cleanResponse(void);

	bool	getInactiveTime(time_t) const;
	
	void	closeConnection(Epoll &);
};
