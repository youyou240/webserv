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

	bool			resolveFsPath(LocationConfig const & loc, std::string & out) const;
	static bool		mapPath(LocationConfig const & loc, std::string const & path, std::string & out);
	HttpResponse		handleGet(LocationConfig const & loc, std::string const & fsPath) const;
	HttpResponse		handlePost(LocationConfig const & loc) const;
	HttpResponse		handleDelete(std::string const & fsPath) const;
	HttpResponse		serveFile(std::string const & fsPath) const;
	HttpResponse		autoIndex(std::string const & fsPath) const;
	HttpResponse		makeRedirect(LocationConfig const & loc) const;
	HttpResponse		makeError(int code) const;

public:

	Router(HttpRequest const & req, ServerConfig const & config);
	~Router(void);

	HttpResponse	run(void);

	struct CgiTarget {
		bool		isCgi;
		bool		valid;
		std::string	scriptPath;
		std::string	interpreter;

		CgiTarget(void);
	};

	static LocationConfig const *	matchLocation(ServerConfig const & config, std::string const & path);
	static CgiTarget		resolveCgi(ServerConfig const & config, std::string const & path);
};
