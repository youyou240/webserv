FROM ubuntu:jammy

RUN apt update && apt-get install -y bash g++ make git nginx valgrind \
					telnet curl python3 vim clangd\
					&& rm -rf /var/lib/apt/lists/*
				
RUN curl -fLo /root/.vim/autoload/plug.vim --create-dirs \
    https://raw.githubusercontent.com/junegunn/vim-plug/master/plug.vim

COPY /vimrc-main/.vimrc /root/.vimrc

RUN vim +PlugInstall +qall

WORKDIR /home

ENTRYPOINT ["/bin/bash"]
