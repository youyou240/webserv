# include "AroleClass.hpp"
# include "../ServerClass.hpp"
# include "../../Epoll/epollClass.hpp"
# include "../../Http/HttpResponseClass.hpp"
# include "../../Router/RouterClass.hpp"
# include <sys/epoll.h>

std::map<int, std::map<int, Client *> > Arole::_activeClient;
unsigned int Arole::_client = 0;

static std::string	toLowerCopy(std::string const & s)
{
	std::string	out(s);

	for (size_t i = 0; i < out.size(); ++i)
		if (out[i] >= 'A' && out[i] <= 'Z')
			out[i] = out[i] - 'A' + 'a';
	return (out);
}

Client::Client(int fd, Acceptor const & parent) : Arole(fd), _acceptor(&parent), _acceptorfd(parent.getFD()), _bytes_send(0), _draining(false), _closeAfterWrite(false), _limitApplied(false), _lastActivity(time(NULL)), _requestStart(0)
{
	this->_client += 1;
	_activeClient[_acceptorfd].insert(std::pair<int, Client *>(_fd, this));
	_ev.events = EPOLLIN;
	_ev.data.ptr = this;
}

Client::~Client(void)
{
	this->_client -= 1;
	_activeClient[this->_acceptorfd].erase(this->_fd);
}

void	Client::cleanRequest(void)
{
	_request.reset();
	_requestStart = 0;
	_limitApplied = false;
	_draining = false;
}

void	Client::cleanResponse(void)
{
	_response.clear();
	_bytes_send = 0;
}

const Server *	Client::selectVhost(void) const
{
	std::vector<const Server*> const &	vhosts = _acceptor->getServers();

	if (vhosts.empty())
		return (NULL);
	std::string	host = toLowerCopy(_request.getHeader("host"));
	size_t		colon = host.find(':');

	if (colon != std::string::npos)
		host.erase(colon);
	for (size_t i = 0; i < vhosts.size(); ++i)
	{
		std::vector<std::string> const &	names = vhosts[i]->getConfig().serverNames;

		for (size_t n = 0; n < names.size(); ++n)
			if (toLowerCopy(names[n]) == host)
				return (vhosts[i]);
	}
	return (vhosts[0]);
}

size_t	Client::resolveMaxBodySize(void) const
{
	const Server *	srv = this->selectVhost();

	if (srv == NULL)
		return (DEFAULT_CLIENT_MAX_BODY_SIZE);
	return (srv->getConfig().clientMaxBodySize);
}

void	Client::genResponse(void)
{
	const Server *	srv = this->selectVhost();

	if (srv == NULL)
	{
		this->genErrorResponse(500);
		return ;
	}

	Router		router(_request, srv->getConfig());
	HttpResponse	resp = router.run();

	resp.setHeader("Connection", _request.isKeepAlive() ? "keep-alive" : "close");
	_response = resp.serialize();
	_bytes_send = 0;
}

void	Client::genErrorResponse(int code)
{
	HttpResponse	resp;

	resp.setStatus(code);
	resp.setBody(HttpResponse::defaultErrorPage(code), "text/html");
	resp.setHeader("Connection", "close");
	_response = resp.serialize();
	_bytes_send = 0;
}

void	Client::handle_event(Epoll & manager, uint32_t events)
{
	if (events & (EPOLLERR | EPOLLHUP))
	{
		this->closeConnection(manager);
		return ;
	}
	if (events & EPOLLIN)
	{
		if (!this->handleRead(manager))
			return ;
	}
	if (events & EPOLLOUT)
		this->handleWrite(manager);
}

bool	Client::handleRead(Epoll & manager)
{
	char	buffer[MAX_REQUEST];
	int	bytes_read = recv(this->_fd, buffer, sizeof(buffer), 0);

	if (bytes_read == 0)
	{
		if (_draining)
		{
			_ev.events = EPOLLOUT;
			manager.mod(this);
			return (false);
		}
		this->closeConnection(manager);
		return (false);
	}
	if (bytes_read < 0)
		return (true);
	_lastActivity = time(NULL);
	if (_draining)
		return (true);
	if (_requestStart == 0)
		_requestStart = time(NULL);
	_request.append(buffer, bytes_read);
	if (!_limitApplied && _request.headersDone())
	{
		_request.setMaxBodySize(this->resolveMaxBodySize());
		_limitApplied = true;
	}
	if (_request.isError())
	{
		this->genErrorResponse(_request.getErrorCode());
		_draining = true;
		_closeAfterWrite = true;
		_ev.events = EPOLLIN | EPOLLOUT;
		manager.mod(this);
		return (false);
	}
	if (_request.isComplete())
	{
		this->genResponse();
		_closeAfterWrite = !_request.isKeepAlive();
		_ev.events = EPOLLOUT;
		manager.mod(this);
		return (false);
	}
	return (true);
}

void	Client::handleWrite(Epoll & manager)
{
	const char*	data_to_send = _response.c_str() + _bytes_send;
	int		bytes_left_to_send = _response.length() - _bytes_send;
	int		bytes_sent_now = send(this->_fd, data_to_send, bytes_left_to_send, 0);

	if (bytes_sent_now <= 0)
		return ;
	_bytes_send += bytes_sent_now;
	_lastActivity = time(NULL);
	if (_bytes_send == _response.length())
	{
		if (_closeAfterWrite)
		{
			this->closeConnection(manager);
			return ;
		}
		this->cleanResponse();
		this->cleanRequest();
		_ev.events = EPOLLIN;
		manager.mod(this);
		return ;
	}
}

bool	Client::getInactiveTime(time_t now) const
{
	if ((now - _lastActivity) >= IDLE_TIMEOUT)
		return (true);
	if (_requestStart != 0)
	{
		if (!_request.headersDone() && (now - _requestStart) >= HEADER_TIMEOUT)
			return (true);
		if ((now - _requestStart) >= REQUEST_TIMEOUT)
			return (true);
	}
	return (false);
}

void	Client::closeConnection(Epoll & manager)
{
	manager.del(this);
	delete this;
}
