# include "AroleClass.hpp"
# include "../ServerClass.hpp"
# include "../../Epoll/epollClass.hpp"
# include "../../Http/HttpResponseClass.hpp"
# include "../../Router/RouterClass.hpp"
# include "../../Cgi/CgiClass.hpp"
# include <sys/epoll.h>

std::map<int, std::map<int, Client *> > Arole::_activeClient;
unsigned int Arole::_client = 0;

static std::string	toLowerCopy(std::string const & s)
{
	std::string	out(s);

	for (size_t i = 0; i < out.size(); ++i)
		if (out[i] >= 'A' && out[i] <= 'Z')
			out[i] += 32;
	return (out);
}

Client::Client(int fd, Acceptor const & parent) : Arole(fd), _acceptor(&parent), _acceptorfd(parent.getFD()), _cgi(NULL), _bytes_send(0), _draining(false), _closeAfterWrite(false), _limitApplied(false), _continueHandled(false), _sendingContinue(false), _lastActivity(time(NULL)), _requestStart(0)
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
	_continueHandled = false;
	_sendingContinue = false;
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

	ServerConfig const &	cfg = srv->getConfig();
	LocationConfig const *	loc = Router::matchLocation(cfg, _request.getPath());

	if (loc != NULL)
		return (loc->clientMaxBodySize);
	return (cfg.clientMaxBodySize);
}

bool	Client::wantsContinue(void) const
{
	if (_request.getVersion() != "HTTP/1.1")
		return (false);
	return (toLowerCopy(_request.getHeader("expect")).find("100-continue") != std::string::npos);
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

bool	Client::tryStartCgi(Epoll & manager)
{
	std::string	method = _request.getMethod();
	if (method != "GET" && method != "POST")
		return (false);

	const Server *	srv = this->selectVhost();
	if (srv == NULL)
		return (false);

	Router::CgiTarget	target = Router::resolveCgi(srv->getConfig(), _request.getPath());
	if (!target.isCgi)
		return (false);

	if (!target.valid)
	{
		this->genErrorResponse(404);
		_closeAfterWrite = true;
		_ev.events = EPOLLOUT;
		manager.mod(this);
		return (true);
	}

	_cgi = new Cgi(*this, _request, target.scriptPath, target.interpreter);
	if (!_cgi->start(manager))
	{
		delete _cgi;
		_cgi = NULL;
		this->genErrorResponse(500);
		_closeAfterWrite = true;
		_ev.events = EPOLLOUT;
		manager.mod(this);
		return (true);
	}
	_ev.events = 0;
	manager.mod(this);
	return (true);
}

void	Client::deliverCgiResponse(std::string const & response, Epoll & manager)
{
	_cgi = NULL;
	_response = response;
	_bytes_send = 0;
	_closeAfterWrite = !_request.isKeepAlive();
	_ev.events = EPOLLOUT;
	manager.mod(this);
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
		if (this->tryStartCgi(manager))
			return (false);
		this->genResponse();
		_closeAfterWrite = !_request.isKeepAlive();
		_ev.events = EPOLLOUT;
		manager.mod(this);
		return (false);
	}
	if (_request.headersDone() && !_continueHandled && this->wantsContinue())
	{
		_continueHandled = true;
		_response = "HTTP/1.1 100 Continue\r\n\r\n";
		_bytes_send = 0;
		_sendingContinue = true;
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
		if (_sendingContinue)
		{
			_sendingContinue = false;
			this->cleanResponse();
			_ev.events = EPOLLIN;
			manager.mod(this);
			return ;
		}
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
	if (_cgi != NULL)
	{
		Cgi *	c = _cgi;
		_cgi = NULL;
		c->clientGone(manager);
	}
	manager.del(this);
	delete this;
}
