FROM ubuntu:20.04
SHELL ["/bin/bash", "-c"]

ARG USER=runner
ENV DEBIAN_FRONTEND=noninteractive
ENV CI_SCRIPT=ON

ENV TZ=Europe/Bucharest
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
RUN apt-get update && \
	apt-get -y upgrade && \
	apt-get install -y apt-utils sudo tzdata keyboard-configuration software-properties-common

RUN groupadd -g 1000 -r $USER && \
	useradd -u 1000 -g 1000 --create-home -r $USER

#Change password
RUN echo "$USER:$USER" | chpasswd

#Make sudo passwordless
RUN echo "${USER} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/90-$USER && \
	usermod -aG sudo $USER && \
	usermod -aG plugdev $USER

USER $USER
WORKDIR /home/${USER}/scripts
COPY install_deps.sh .
RUN sudo chown -R $USER:$USER /home/${USER}/scripts
RUN ./install_deps.sh install_apt_pkgs install_gtkdatabox install_libserialport install_libiio install_libad9361 install_libad9166
WORKDIR /home/${USER}

# Clean image
RUN sudo rm -rf /home/${USER}/scripts
RUN sudo rm -rf /var/lib/apt/lists/*
FROM scratch
COPY --from=0 / /