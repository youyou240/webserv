# pragma once

# include <string>
# include <vector>

# include "../Http/HttpRequestClass.hpp"
# include "../Config/LocationConfigClass.hpp"

class Upload {

	Upload(void);

public:

	struct Result {
		bool				ok;
		int				errorCode;
		std::vector<std::string>	files;

		Result(void);
	};

	static Result	handle(HttpRequest const & req, LocationConfig const & loc);
};
