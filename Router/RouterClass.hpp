# pragma once

# include <string>

# include "../Http/HttpRequestClass.hpp"
# include "../Http/HttpResponseClass.hpp"
# include "../Config/ServerConfigClass.hpp"
# include "../Config/LocationConfigClass.hpp"

class Router {

	HttpRequest const &	_req;
	ServerConfig const &	_config;

	Router(void);
	Router(Router const &);
	Router &	operator=(Router const &);

	LocationConfig const *	matchLocation(void) const;
	bool			resolveFsPath(LocationConfig const & loc, std::string & out) const;
	HttpResponse		handleGet(LocationConfig const & loc, std::string const & fsPath) const;
	HttpResponse		serveFile(std::string const & fsPath) const;
	HttpResponse		makeRedirect(LocationConfig const & loc) const;
	HttpResponse		makeError(int code) const;

public:

	Router(HttpRequest const & req, ServerConfig const & config);
	~Router(void);

	HttpResponse	run(void);
};
