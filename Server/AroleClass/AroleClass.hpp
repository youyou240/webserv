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

# include "../../Http/HttpRequestClass.hpp"

# define IDLE_TIMEOUT 60
# define HEADER_TIMEOUT 30
# define REQUEST_TIMEOUT 300

class Client;
class Epoll;
class Server;
class Cgi;

class Arole {

protected:

	static std::map<int, std::map<int, Client *> > _activeClient;	
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
	std::vector<const Server*>		_vhosts;
public:

	Acceptor(void);
	Acceptor(int fd);
	~Acceptor(void);

	virtual void	handle_event(Epoll & manager, uint32_t events);

	unsigned int	getNbClient(void) const;

	void		setFD(int);
	void		addServer(const Server * srv);
	void		removeServer(const Server * srv);
	std::vector<const Server*> const &	getServers(void) const;

	void		start(Epoll &);
	void		closeAll(Epoll &);
};

std::ostream &	operator<<(std::ostream & o,Acceptor  const & acceptor);

class Client : public Arole {

	Acceptor const *	_acceptor;
	int			_acceptorfd;

	HttpRequest	_request;
	std::string	_response;
	Cgi *		_cgi;

	unsigned int	_bytes_send;

	bool		_draining;
	bool		_closeAfterWrite;
	bool		_limitApplied;
	bool		_continueHandled;
	bool		_sendingContinue;

	time_t		_lastActivity;
	time_t		_requestStart;

	Client(void);

	const Server *	selectVhost(void) const;
	size_t		resolveMaxBodySize(void) const;
	bool		wantsContinue(void) const;
	bool		tryStartCgi(Epoll & manager);

public:

	Client(int fd, Acceptor const & parent);
	~Client(void);

	virtual void	handle_event(Epoll & manager, uint32_t events);

	bool	handleRead(Epoll & manager);
	void	handleWrite(Epoll & manager);

	void	genResponse(void);
	void	genErrorResponse(int code);
	void	deliverCgiResponse(std::string const & response, Epoll & manager);
	void	cleanRequest(void);
	void	cleanResponse(void);

	bool	getInactiveTime(time_t) const;

	void	closeConnection(Epoll &);
};
