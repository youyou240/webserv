.DEFAULT_GOAL = all

NAME = Webserv

CPPFLAGS = -Wall -Wextra -Werror -g3 -std=c++98

SRCS = main.cpp Server/ServerClass.cpp	\
		Server/ServerManagerClass.cpp \
		Server/socket/socketClass.cpp \
		Server/socket/sockaddrClass.cpp \
		Epoll/epollClass.cpp	\
		Server/AroleClass/AroleClass.cpp \
		Server/AroleClass/ClientClass.cpp \
		Server/AroleClass/AcceptorClass.cpp \
		Config/ServerConfigClass.cpp \
		Config/LocationConfigClass.cpp \
		Config/ConfigParserClass.cpp \
		Config/ConfigParserDirectives.cpp \
		Http/HttpRequestClass.cpp \
		Http/HttpResponseClass.cpp \
		Router/RouterClass.cpp \
		Router/MimeTypesClass.cpp \
		Router/UploadClass.cpp \
		Cgi/CgiClass.cpp \
		myzod/safeInputClass.cpp

OBJS = $(SRCS:%.cpp=%.o)

CGI_BIN = www/cgi-bin/hello.cgi

$(NAME) : $(OBJS)
	c++ $(CPPFLAGS) $(OBJS) -o $(NAME)

$(CGI_BIN) : www/cgi-bin/hello.c
	cc -Wall -Wextra -o $(CGI_BIN) www/cgi-bin/hello.c

.PHONY: all
all : $(NAME) $(CGI_BIN)


.PHONY: clean
clean:
	$(RM) $(SRCS:%.cpp=%.o)

.PHONY: fclean
fclean: clean
	$(RM) $(NAME)
	$(RM) $(CGI_BIN)

.PHONY: re
re: fclean all
